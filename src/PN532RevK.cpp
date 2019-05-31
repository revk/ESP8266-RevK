#include "PN532_SPI.h"
#include "PN532RevK.h"
#include "ESPRevK.h"
#include "AESLib.h"
#include <ESP8266TrueRandom.h>

#define HAL(func)   (_interface->func)

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

int
PN532RevK::desfire_cmac (AES & A, byte cmd, int len, byte * buf, unsigned int maxlen, int timeout, byte * sk1, byte * sk2)
{                               // Send and receive, puts cmd in buf[2]. Len is additional bytes after cmd. buf[0]/buf[1] need to available
   buf[0] = 0x40;               // InDataExchange
   buf[1] = Tg1;
   buf[2] = cmd;
   if (HAL (writeCommand) (buf, len + 3))
      return -101;
   if (sk1 && sk2)
   {                            // Update CMAC
      unsigned int n = len + 1,
         p;
      if (!n || (n & 15))
      {                         // Padding
         if (n + 2 < maxlen)
            buf[2 + n++] = 0x80;
         while ((n & 15) && n + 2 < maxlen)
            buf[2 + n++] = 0;
         for (p = 0; p < 16; p++)
            buf[2 + n - 16 + p] ^= sk2[p];
      } else
         for (p = 0; p < 16; p++)
            buf[2 + n - 16 + p] ^= sk1[p];
      A.cbc_encrypt (buf + 2, buf + 2, n / 16); // Update CMAC
   }
   len = HAL (readResponse) (buf, maxlen, timeout);
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
      return -2000 - buf[1];    // Bad status from card
   if (sk1 && sk2)
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
PN532RevK::getID (String & id1, unsigned int timeout)
{                               // Return tag id
   //debug ("PN532 InListPassiveTarget");
   id1 = String ();             // defaults
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
   int tags = buf[0];
   Tg1 = buf[1];
   if (tags < 1)
      return tags;
   char cid[100];
   int n;
   for (n = 0; n < 10 && n < buf[5]; n++)
      sprintf_P (cid + n * 2, PSTR ("%02X"), buf[6 + n]);
   if (n == 7 && (aid[0] || aid[1] || aid[2]))
   {                            // 7 byte ID always the case on MIFARE
      n *= 2;
      AES A;
      // Select application
      buf[3] = aid[0];
      buf[4] = aid[1];
      buf[5] = aid[2];
      l = desfire_cmac (A, 0x5A, 3, buf, sizeof (buf), timeout, NULL, NULL);
      if (l != 0)
         sprintf_P (cid + n, PSTR ("*AID fail %d %02X"), l, buf[1]);
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
            sprintf_P (cid + n, PSTR ("*AA fail %d %02X"), l, buf[1]);
         else
         {
            A.set_key (aes, 16);
            A.set_IV (0);
            byte a[16], b[16];
            ESP8266TrueRandom.memfill ((char *) a, 16);
            A.cbc_decrypt (buf + 2, b, 1);
            memcpy ((void *) buf + 3, (void *) a, 16);
            memcpy ((void *) buf + 3 + 16, (void *) b + 1, 15);
            buf[3 + 31] = b[0];
            buf[0] = 0x40;      // InDataExchange
            buf[1] = Tg1;
            buf[2] = 0xAF;      // Additional frame
            A.cbc_encrypt (buf + 3, buf + 3, 2);
            if (HAL (writeCommand) (buf, 35))
               return 0;
            l = HAL (readResponse) (buf, sizeof (buf), timeout);
            if (l != 18 || *buf || buf[1])
               sprintf_P (cid + n, PSTR ("*AA fail %d %02X"), l, buf[1]);
            else
            {
               A.cbc_decrypt (buf + 2, buf + 2, 1);
               if (memcmp ((void *) buf + 2, a + 1, 15) || buf[2 + 15] != a[0])
                  strcpy_P (cid + n, PSTR ("*AA fail AES"));
               else
               {
                  memcpy ((void *) (a + 4), (void *) (b + 0), 4);       // Make a the new key
                  memcpy ((void *) (a + 8), (void *) (a + 12), 4);
                  memcpy ((void *) (a + 12), (void *) (b + 12), 4);
                  A.set_key (a, 16);    // Session key
#ifdef REVKDEBUG
                  Serial.printf ("Skey %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", a[0],
                                 a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
#endif
                  A.set_IV (0); // To work out the sub keys
                  memset ((void *) a, 0, 16);
                  A.cbc_encrypt (a, a, 1);
                  key_left (a); // a is now subkey1
#ifdef REVKDEBUG
                  Serial.printf ("SK1  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", a[0],
                                 a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
#endif
                  memcpy ((void *) b, (void *) a, 16);
                  key_left (b); // b is now subkey2
#ifdef REVKDEBUG
                  Serial.printf ("SK2  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", b[0],
                                 b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
#endif
                  A.set_IV (0); // ready to start CMAC messages
                  byte fn;
                  for (fn = 1; fn <= 2; fn++)
                  {             // Add file 1 and 2 to id
                     // Get file settings
                     buf[3] = fn;
                     l = desfire_cmac (A, 0xF5, 1, buf, sizeof (buf), timeout, a, b);
#ifdef REVKDEBUG
                     if (l != 7)
                     {
                        sprintf_P (cid + n, PSTR ("*File%d fail %d %02X"), fn, l, buf[1]);      // TODO debug
                        break;
                     }
#endif
                     if (l == 7)
                     {
                        unsigned int s = buf[6] + (buf[7] << 8) + (buf[8] << 16);
                        if (s > sizeof (cid) - 2 - n)
                           s = sizeof (cid) - 2 - n;
                        // Get file content
                        buf[3] = fn;    // File 1 (ID)
                        buf[4] = 0;     // Offset
                        buf[5] = 0;
                        buf[6] = 0;
                        buf[7] = s;
                        buf[8] = s >> 8;
                        buf[9] = s >> 16;
                        l = desfire_cmac (A, 0xBD, 7, buf, sizeof (buf), timeout, a, b);
#ifdef REVKDEBUG
                        if (l != s)
                        {
                           sprintf_P (cid + n, PSTR ("*Data%d fail %d %02X %d"), fn, l, buf[1], s);     // TODO debug
                           break;
                        }
#endif
                        if (l == s)
                        {       // ID file
                           cid[n++] = ' ';
                           memcpy ((void *) cid + n, (void *) buf + 2, s);
                           n += s;
                           cid[n] = 0;
                        }
                     }
                  }
               }
            }
         }
      }
   }
   id1 = String (cid);
   return tags;
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
}

void
PN532RevK::set_aes (const uint8_t * newaes)
{                               // Set AES (8 bytes)
   memcpy ((void *) aes, (void *) newaes, sizeof (aes));
}
