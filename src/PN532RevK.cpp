#include "PN532_SPI.h"
#include "PN532RevK.h"
#include "ESPRevK.h"
#include "AESLib.h"
#include <ESP8266TrueRandom.h>

#define HAL(func)   (_interface->func)
#define nfcslow	50000           // Max response time for AES handshake, us

PN532RevK::PN532RevK (PN532Interface & interface)
{
   _interface = &interface;
}

uint32_t
PN532RevK::begin (unsigned int timeout)
{                               // Begin, and get s/w version (0=bad)
   debug ("PN532 GetFirmwareVersion");
   HAL (begin) ();
   HAL (wakeup) ();

   uint8_t buf[8];
   buf[0] = 0x02;               // GetFirmwareVersion
   if (HAL (writeCommand) (buf, 1) || HAL (readResponse) (buf, sizeof (buf), timeout) < 4)
      return 0;
   uint32_t ver = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 5;                  // Config item 5 (MaxRetries)
   buf[2] = 0xFF;               // MxRtyATR (default = 0xFF)
   buf[3] = 0x01;               // MxRtyPSL (default = 0x01)
   buf[4] = 0x01;               // MxRtyPassiveActivation
   if (HAL (writeCommand) (buf, 5) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
   buf[0] = 0x14;               // SAMConfiguration
   buf[1] = 0x01;               // Normal
   buf[2] = 20;                 // *50ms timeout
   buf[3] = 0x01;               // Use IRQ
   if (HAL (writeCommand) (buf, 4) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
   buf[0] = 0x08;               // WriteRegister
   buf[1] = 0xFF;               // P3CFGB
   buf[2] = 0xFD;               // P3CFGB
   buf[3] = 0x03;               // P30/P31 push/pull
   buf[4] = 0xFF;               // P3
   buf[5] = 0xB0;               // P3
   buf[6] = 0xFF;               // All high
   if (HAL (writeCommand) (buf, 7) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
   // SetParameters - not necessary
   debugf ("PN532 GetFirmwareVersion=%08X", ver);
   return ver;
}

int
PN532RevK::p3 (unsigned int timeout)
{                               // Return state of P3 GPIO
   uint8_t buf[3];
   buf[0] = 0x0C;               // ReadGPIO
   if (HAL (writeCommand) (buf, 1) || HAL (readResponse) (buf, sizeof (buf), timeout) < 3)
      return -1;
   return buf[0];
}

uint8_t
PN532RevK::led (uint8_t led, unsigned int timeout)
{                               // Set LED (GPIO bits)
   led |= 0x14;                 // P32 and P34 set as per Elechouse library?
   uint8_t buf[3];
   buf[0] = 0x0E;               // WriteGPIO
   buf[1] = (0x80 | led);       // P3 set
   buf[2] = 0;                  // P7 unchanged
   if (HAL (writeCommand) (buf, 3) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0xFF;
   return 0;
}

uint8_t PN532RevK::cardsPresent (unsigned int timeout)
{                               // Return number of cards being handled, 0 if none or error
   uint8_t
      buf[10];
   buf[0] = 0x04;               // GetGeneralStatus
   if (HAL (writeCommand) (buf, 1) || HAL (readResponse) (buf, sizeof (buf), timeout) < 5)
      return 0;
   return buf[0];
}

uint8_t
PN532RevK::inField (unsigned int timeout)
{                               // 0 if OK and card(s) in field still, else status
   uint8_t buf[20];
   buf[0] = 0x00;               // Diagnose
   buf[1] = 6;                  // Test 6 Attention Request Test or ISO/IEC14443-4 card presence detection
   if (HAL (writeCommand) (buf, 2) || HAL (readResponse) (buf, sizeof (buf), timeout) < 1)
      return 0xFF;
   return buf[0];               // Status
}

unsigned long
PN532RevK::desfire_crc (unsigned int len, byte * data)
{
   unsigned int poly = 0xEDB88320;
   unsigned int crc = 0xFFFFFFFF;
   int n,
     b;
   for (n = 0; n < len; n++)
   {
      crc ^= data[n];
      for (b = 0; b < 8; b++)
         if (crc & 1)
            crc = (crc >> 1) ^ poly;
         else
            crc >>= 1;
   }
   return crc;
}

int
PN532RevK::desfire_tx (byte cmd, int len, byte * buf, int timeout)
{                               // Send message, updating CMAC (if secure). cmd is put in buf[0]. buf must have space for padding
   // TODO we should at least have CMAC mode for file commands, if not proper encrypted even...
   byte msg[3];
   msg[0] = 0x40;               // InDataExchange
   msg[1] = Tg1;
   buf[0] = cmd;
   if (cmd == 0x5A)
      secure = false;           // Select app clears security
   if (HAL (writeCommand) (msg, 2, buf, len + 1))
      return -101;
   if (secure)
   {                            // Update CMAC
      unsigned int n = len + 1,
         p;
      if (!n || (n & 15))
      {                         // Padding
         buf[n++] = 0x80;
         while (n & 15)
            buf[n++] = 0;
         for (p = 0; p < 16; p++)
            buf[n - 16 + p] ^= sk2[p];
      } else
         for (p = 0; p < 16; p++)
            buf[n - 16 + p] ^= sk1[p];
      A.cbc_encrypt (buf, buf, n / 16); // Update CMAC
   }
}

int
PN532RevK::desfire_rx (int maxlen, byte * buf, int timeout)
{                               // Get response. PN532 status in buf[0], and response status in buf[1]. Update and check CMAC if secure
   int len = HAL (readResponse) (buf, maxlen, timeout);
   if (len < 0)
      return len;
   if (len < 2)
      return -102;
   len -= 2;
   if (*buf)
      return -1000 - *buf;      // Bad status from PN532
   if (buf[1] == 0xAF)
   {                            // Additional frames
      // TODO - may not be needed
   }
   if (buf[1])
   {
      secure = false;
      return -2000 - buf[1];    // Bad status from card
   }
   if (secure)
   {                            // Update and check CMAC
      if (len < 8)
         return -103;
      len -= 8;                 // Lose CMAC
      unsigned int n = len,
         p;
      byte *sk = sk1;
      byte cmac[8];
      memcpy ((void *) cmac, (void *) (buf + 2 + len), 8);
      buf[2 + n++] = buf[1];    // Append status
      if (!n || (n & 15))
      {                         // Padding
         if (n + 2 < maxlen)
            buf[2 + n++] = 0x80;
         while ((n & 15) && n + 2 < maxlen)
            buf[2 + n++] = 0;
         sk = sk2;
      }
      for (p = 0; p < 16; p++)
         buf[2 + n - 16 + p] ^= sk[p];
      if (n & 15)
         return -105;           // No space
      byte temp[16],
        c = 0;
      while (c < n / 16)
         A.cbc_encrypt (buf + 2 + (c++) * 16, temp, 1); // Update CMAC
      // Undo, so we have a response you can use...
      for (p = 0; p < 16; p++)
         buf[2 + n - 16 + p] ^= sk[p];
      // Check CMAC
      if (memcmp (cmac, temp, 8))
         return -104;
   }
   return len;
}

int
PN532RevK::desfire_rxd (int maxlen, byte * buf, int timeout)
{                               // Get response. PN532 status in buf[0], and response status in buf[1]. Decrypt response
   int len = HAL (readResponse) (buf, maxlen, timeout);
   if (len < 0)
      return len;
   if (len < 2)
      return -102;
   len -= 2;
   if (*buf)
      return -1000 - *buf;      // Bad status from PN532
   if (buf[1] == 0xAF)
   {                            // Additional frames
      // TODO - may not be needed
   }
   if (buf[1])
   {
      secure = false;
      return -2000 - buf[1];    // Bad status from card
   }
   if (!len)
      return len;
   if (len & 15)
      return -107;
   A.cbc_decrypt (buf + 2, buf + 2, len / 16);
   return len;
}

int
PN532RevK::desfire (byte cmd, int len, byte * buf, unsigned int maxlen, String & err, int timeout)
{
   int l = desfire_tx (cmd, len, buf, timeout);
   if (l < 0)
      return l;
   l = desfire_rx (maxlen, buf, timeout);
   if (l < 0)
   {
      byte status = buf[1];
      sprintf_P ((char *) buf, "Failed cmd %02X status %02X (%d)", cmd, status, l);
      err = String ((char *) buf);
      buf[1] = status;
   }
   return l;
}

void
get_bcd_time (byte bcd[7])
{
   time_t now;
   struct tm *t;
   time (&now);
   // TODO how to find actual local time??
   //t = localtime (&now);
   t = gmtime (&now);
#define makebcd(x) ((x)/10*16+(x)%10)
   bcd[0] = makebcd ((t->tm_year + 1900) / 100);
   bcd[1] = makebcd ((t->tm_year + 1900) % 100);
   bcd[2] = makebcd (t->tm_mon + 1);
   bcd[3] = makebcd (t->tm_mday);
   bcd[4] = makebcd (t->tm_hour);
   bcd[5] = makebcd (t->tm_min);
   bcd[6] = makebcd (t->tm_sec);
#undef makebcd
}

void
key_left (byte * p)
{
   int n;
   byte x = ((p[0] & 0x80) ? 0x87 : 0);
   for (n = 0; n < 15; n++)
      p[n] = (p[n] << 1) + (p[n + 1] >> 7);
   p[15] = ((p[15] << 1) ^ x);
}

uint8_t
PN532RevK::getID (String & id, String & err, unsigned int timeout)
{                               // Return tag id
   //debug ("PN532 InListPassiveTarget");
   secure = false;
   id = String ();              // defaults
   err = String ();
   Tg1 = 0;
   uint8_t buf[64];
   buf[0] = 0x4A;               // InListPassiveTarget
   buf[1] = 1;                  // 1 tag
   buf[2] = 0;                  // 106 kbps type A (ISO/IEC14443 Type A)
   if (HAL (writeCommand) (buf, 3))
      return 0;
   int l = HAL (readResponse) (buf, sizeof (buf), timeout);
   if (l < 6)
      return 0;
   byte tags = buf[0];
   Tg1 = buf[1];
   if (tags < 1)
      return tags;
   byte cid[10], cidlen;
   boolean cidsecure = false;
   if (buf[5] > sizeof (cid))
   {                            // ID too big
      strcpy_P ((char *) buf, PSTR ("ID too long"));
      err = String ((char *) buf);
      return 0;
   }
   memcpy ((void *) cid, (void *) (buf + 6), cidlen = buf[5]);
   if (aidset)
   {
      // Select application (we expect that this could mean an err, no such app)
      buf[1] = aid[0];
      buf[2] = aid[1];
      buf[3] = aid[2];
      l = desfire_tx (0x5A, 3, buf, timeout);
      if (l < 0)
         return 0;
      l = desfire_rx (sizeof (buf), buf, timeout);
      if (!l)
      {                         // Application exists
         // AES exchange
         buf[0] = 0x40;         // InDataExchange
         buf[1] = Tg1;
         buf[2] = 0xAA;         // AES Authenticate
         buf[3] = 0x01;         // Key 1
         if (HAL (writeCommand) (buf, 4))
            return 0;
         l = HAL (readResponse) (buf, sizeof (buf), timeout);
         if (l != 18 || *buf || buf[1] != 0xAF)
         {
            byte status = buf[1];
            sprintf_P ((char *) buf, PSTR ("AA1 fail %d %02X"), l, status);
            err = String ((char *) buf);
         } else
         {
            A.set_key (aes, 16);
            A.set_IV (0);
            A.cbc_decrypt (buf + 2, sk2, 1);
            ESP8266TrueRandom.memfill ((char *) sk1, 16);
            memcpy ((void *) buf + 3, (void *) sk1, 16);
            memcpy ((void *) buf + 3 + 16, (void *) sk2 + 1, 15);
            buf[3 + 31] = sk2[0];
            buf[0] = 0x40;      // InDataExchange
            buf[1] = Tg1;
            buf[2] = 0xAF;      // Additional frame
            A.cbc_encrypt (buf + 3, buf + 3, 2);
            if (HAL (writeCommand) (buf, 35))
               return 0;
            int timed = micros ();
            l = HAL (readResponse) (buf, sizeof (buf), timeout);
            timed = micros () - timed;
            if (timed > nfcslow)
            {
               sprintf_P ((char *) buf, PSTR ("AA2 slow %dus"), timed);
               err = String ((char *) buf);
            } else if (l != 18 || *buf || buf[1])
            {
               byte status = buf[1];
               sprintf_P ((char *) buf, PSTR ("AA2 fail %d %02X"), l, status);
               err = String ((char *) buf);
            } else
            {
               A.cbc_decrypt (buf + 2, buf + 2, 1);
               if (memcmp ((void *) buf + 2, sk1 + 1, 15) || buf[2 + 15] != sk1[0])
               {
                  strcpy_P ((char *) buf, PSTR ("AA AES fail"));
                  err = String ((char *) buf);
               } else
               {
                  memcpy ((void *) (sk1 + 4), (void *) (sk2 + 0), 4);   // Make a the new key
                  memcpy ((void *) (sk1 + 8), (void *) (sk1 + 12), 4);
                  memcpy ((void *) (sk1 + 12), (void *) (sk2 + 12), 4);
                  A.set_key (sk1, 16);  // Session key
                  A.set_IV (0); // To work out the sub keys
                  memset ((void *) sk1, 0, 16);
                  A.cbc_encrypt (sk1, sk1, 1);
                  key_left (sk1);
                  memcpy ((void *) sk2, (void *) sk1, 16);
                  key_left (sk2);
                  A.set_IV (0); // ready to start CMAC messages
                  secure = true;
                  // Get real ID
                  l = desfire_tx (0x51, 0, buf, timeout);
                  if (l < 0)
                     return 0;
                  l = desfire_rxd (sizeof (buf), buf, timeout);
                  if (l != 16)
                  {
                     secure = false;
                     byte status = buf[1];
                     sprintf_P ((char *) buf, PSTR ("51 fail %d %02X"), l, status);
                     err = String ((char *) buf);
                  } else
                  {
                     unsigned long crc = buf[9] + (buf[10] << 8) + (buf[11] << 16) + (buf[12] << 24);
                     buf[9] = buf[1];
                     if (desfire_crc (8, buf + 2) != crc)
                     {
                        secure = false;
                        strcpy_P ((char *) buf, PSTR ("ID CRC fail"));
                        err = String ((char *) buf);
                     } else
                     {
                        memcpy (cid, buf + 2, cidlen = 7);
                        cidsecure = true;
                     }
                  }
               }
            }
         }
      }
   }
   if (cidlen)
   {                            // Set ID
      int n;
      for (n = 0; n < cidlen; n++)
         sprintf_P ((char *) buf + n * 2, PSTR ("%02X"), cid[n]);
      if (cidsecure)
         strcpy_P ((char *) buf + n * 2, PSTR ("+"));   // Indicate that it is secure
      id = String ((char *) buf);
   }
   if (*err.c_str ())
      secure = false;
   return tags;
}

int
PN532RevK::desfire_log (String & err, int timeout)
{
   if (!secure)
      return -1;
   uint8_t buf[32];
   buf[1] = 0x01;               // File 1
   buf[2] = 0;                  // Offset 0
   buf[3] = 0;
   buf[4] = 0;
   buf[5] = 10;                 // Length 10
   buf[6] = 0;
   buf[7] = 0;
   unsigned int ci = ESP.getChipId ();
   buf[8] = ci >> 16;
   buf[9] = ci >> 8;
   buf[10] = ci;
   get_bcd_time (buf + 11);
   int l = desfire (0x3B, 17, buf, sizeof (buf), err, timeout);
   if (l < 0)
      return l;
   buf[1] = 0x02;
   buf[2] = 1;                  // Credit 1
   buf[3] = 0;
   buf[4] = 0;
   buf[5] = 0;
   l = desfire (0x0C, 5, buf, sizeof (buf), err, timeout);
   if (l < 0)
      return l;
   return desfire (0xC7, 0, buf, sizeof (buf), err, timeout);
}

uint8_t
PN532RevK::data (uint8_t txlen, uint8_t * tx, uint8_t & rxlen, uint8_t * rx, unsigned int timeout)
{                               // Data exchange, fills in data with status byte
   uint8_t rxspace = rxlen;
   rxlen = 0;
   if (!Tg1)
      return 0xFF;              // No tag
   uint8_t buf[2];
   buf[0] = 0x40;               // InDataExchange
   buf[1] = Tg1;
   int len;
   if (HAL (writeCommand) (buf, 2, tx, txlen) || (len = HAL (readResponse) (rx, rxspace, timeout)) < 1)
      return 0xFF;
   rxlen = len;
   return rx[0];
}

uint8_t PN532RevK::release (unsigned int timeout)
{
   if (!Tg1)
      return 0;                 // Released as not set
   uint8_t
      buf[2];
   buf[0] = 0x52;               // InRelease
   buf[1] = Tg1;
   if (HAL (writeCommand) (buf, 2) || HAL (readResponse) (buf, sizeof (buf), timeout) < 1)
      return 0xFF;
   Tg1 = 0;
   return buf[0];
}

uint8_t
PN532RevK::target (unsigned int timeout)
{                               // Acting as a target with NDEF crap
   if (Tg1)
      release (timeout);
   uint8_t buf[38], n;
   for (n = 0; n < sizeof (buf); n++)
      buf[n] = 0;
   buf[0] = 0x8C;               // TgInitAsTarget
   buf[1] = 0x05;               // PICC+passive
   uint32_t c = ESP.getChipId ();
   buf[4] = (c >> 16);          // Mifare NFCID1
   buf[5] = (c >> 8);
   buf[6] = c;
   buf[7] = 0x20;               // Mifare SEL_RES (ISO/IEC14443-4 PICC emulation)
// TODO way more shit to code here
}

void
PN532RevK::set_aid (const uint8_t * newaid)
{                               // Set AID (3 bytes)
   memcpy ((void *) aid, (void *) newaid, sizeof (aid));
   aidset = (aid[0] || aid[1] || aid[2]);
}

void
PN532RevK::set_aes (const uint8_t * newaes)
{                               // Set AES (8 bytes)
   memcpy ((void *) aes, (void *) newaes, sizeof (aes));
}
