#include "PN532_SPI.h"
#include "PN532_HSU.h"
#include "PN532RevK.h"
#include "ESPRevK.h"
#include "AESLib.h"
#include <ESP8266TrueRandom.h>

#define HAL(func)   (_interface->func)
#define	nfctimeout	60      // Note the system has an internal timeout too, see begin function

#ifdef REVKDEBUG
void
dump (const char *prefix, unsigned int len, const byte * data)
{
   Serial.printf ("%-10s", prefix);
   int n;
   for (n = 0; n < len; n++)
      Serial.printf (" %02X", data[n]);
   Serial.printf ("\n");
}
#else
#define dump(p,l,d)
#endif

PN532RevK::PN532RevK (PN532Interface & interface)
{
   _interface = &interface;
}

void
PN532RevK::set_interface (PN532Interface & interface)
{
   _interface = &interface;
}

uint32_t PN532RevK::begin (byte p3, unsigned int timeout)
{                               // Begin, and get s/w version (0=bad)
   debug ("PN532 GetFirmwareVersion");
   HAL (begin) ();
   HAL (wakeup) ();

   uint8_t
      buf[8];
   buf[0] = 0x02;               // GetFirmwareVersion
   if (HAL (writeCommand) (buf, 1) || HAL (readResponse) (buf, sizeof (buf), timeout) < 4)
      return 0;
   uint32_t
      ver = (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3];
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 5;                  // Config item 5 (MaxRetries)
   buf[2] = 0xFF;               // MxRtyATR (default = 0xFF)
   buf[3] = 0x01;               // MxRtyPSL (default = 0x01)
   buf[4] = 0x01;               // MxRtyPassiveActivation (default 0xFF)
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
   buf[3] = p3;                 // Define output bits
   buf[4] = 0xFF;               // P3
   buf[5] = 0xB0;               // P3
   buf[6] = 0xFF;               // All high
   if (HAL (writeCommand) (buf, 7) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
#if 1
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 0x04;               // MaxRtyCOM
   buf[2] = 0;                  // Retries (default 0)
   if (HAL (writeCommand) (buf, 3) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
#endif
#if 1
   buf[0] = 0x32;               // RFConfiguration
   buf[1] = 0x02;               // Various timings (100*2^(n-1))us
   buf[2] = 0x00;               // RFU
   buf[3] = 0x0B;               // Default (102.4 ms)
   buf[4] = 0x0A;               // Default is 0x0A (51.2 ms)
   if (HAL (writeCommand) (buf, 5) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0;
#endif
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

uint8_t PN532RevK::led (uint8_t led, unsigned int timeout)
{                               // Set LED (GPIO bits)
   led |= 0x14;                 // P32 and P34 set as per Elechouse library?
   uint8_t
      buf[3];
   buf[0] = 0x0E;               // WriteGPIO
   buf[1] = (0x80 | led);       // P3 set
   buf[2] = 0;                  // P7 unchanged
   if (HAL (writeCommand) (buf, 3) || HAL (readResponse) (buf, sizeof (buf), timeout) < 0)
      return 0xFF;
   return 0;
}

uint8_t
PN532RevK::cardsPresent (unsigned int timeout)
{                               // Return number of cards being handled, 0 if none or error
   uint8_t buf[10];
   buf[0] = 0x04;               // GetGeneralStatus
   if (HAL (writeCommand) (buf, 1) || HAL (readResponse) (buf, sizeof (buf), timeout) < 5)
      return 0;
   return buf[0];
}

uint8_t PN532RevK::inField (unsigned int timeout)
{                               // 0 if OK and card(s) in field still, else status
   uint8_t
      buf[20];
   buf[0] = 0x00;               // Diagnose
   buf[1] = 6;                  // Test 6 Attention Request Test or ISO/IEC14443-4 card presence detection
   if (HAL (writeCommand) (buf, 2) || HAL (readResponse) (buf, sizeof (buf), timeout) < 1)
      return 0xFF;
   return buf[0];               // Status
}

unsigned int
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

void
PN532RevK::desfire_cmac (byte cmacout[16], unsigned int len, byte * data)
{                               // Process message for CMAC, using and updating IV in AES A, return cmac
   // Note, cmacout is final IV, but also used as scratchpad/tmp. Safe to pass cmacout as data
   // Initial blocks
   while (len > 16)
   {
      A.cbc_encrypt (data, cmacout, 1);
      data += 16;
      len -= 16;
   }
   // Final block
   unsigned int p;
   if (len < 16)
   {                            // Padded
      for (p = 0; p < len; p++)
         cmacout[p] = data[p] ^ sk2[p];
      cmacout[p] = 0x80 ^ sk2[p];
      for (p++; p < 16; p++)
         cmacout[p] = sk2[p];
   } else                       // Unpadded
      for (p = 0; p < len; p++)
         cmacout[p] = data[p] ^ sk1[p];
   A.cbc_encrypt (cmacout, cmacout, 1);
}

#define MAXTX 55
// DESFire data exchange, including encryption and CMAC and multi-part messages
// See include file for details description
int
PN532RevK::desfire_dx (byte cmd, unsigned int max, byte * data, unsigned int len, byte txenc, byte rxenc, int timeout)
{                               // Data exchange
   byte temp[16];
   // Sending
   if (cmd)
      data[0] = cmd;            // For convenience
   else
      cmd = data[0];
   dump ("Tx", len, data);
   if (cmd == 0x5A || cmd == 0xAA || cmd == 0x1A || cmd == 0x0A)
      authenticated = false;    // Authentication and select application loses authentication
   // Pre process
   if (authenticated)
   {                            // We are authenticated
      if (txenc)
      {                         // Encrypted sending (updates IV)
         if (txenc + ((len + 4 - txenc) | 15) + 1 > max)
            return -999;
         if (cmd != 0xC4)
         {                      // Add CRC
            unsigned int c = desfire_crc (len, data);
            data[len++] = c;
            data[len++] = c >> 8;
            data[len++] = c >> 16;
            data[len++] = c >> 24;
         }
         while ((len - txenc) & 15)
            data[len++] = 0;    // Padding
         A.cbc_encrypt (data + txenc, data + txenc, (len - txenc) / 16);
         dump ("Tx(raw)", len, data);
      } else                    // Update CMAC
         desfire_cmac (temp, len, data);
   }
   {                            // Send the data
      byte *p = data,
         *e = data + len;
      while (p < e)
      {
         if (p > data)
            *--p = 0xAF;
         int l = e - p;
         if (l > MAXTX)
            l = MAXTX;
         temp[0] = 0x40;        // InDataExchange
         temp[1] = Tg1;
         if (HAL (writeCommand) (temp, 2, data, l))
            return -101;
         p += l;
         if (p < e)
         {                      // Expect AF response asking for more data
            int r = HAL (readResponse) (temp, 3, timeout);
            if (r < 0)
               return r;
            if (r < 2)
            {
               dump ("Rx(raw)", r, temp);
               return -1000;
            }
            if (*temp)
               return -1000 - *temp;    // Bad PN532 status
            if (temp[1] != 0xAF)
               return -2000 - temp[1];  // Bad DESFire status
         }
      }
   }
   {                            // Get the response
      byte *p = data,
         *e = data + max;
      while (p < e)
      {
         int r = HAL (readResponse) (p, e - p, timeout),
            i;
         if (r < 0)
            return r;
         if (r < 2)
         {
            dump ("Rx(raw)", r, p);
            return -1000;
         }
         if (*p)
            return -1000 - *p;  // Bad PN532 status
         if (p == data)
         {                      // First, copy back one byte
            for (i = 0; i < r - 1; i++)
               p[i] = p[i + 1];
         } else
         {                      // More data, move status and copy back 2 bytes
            *data = p[1];       // status
            for (i = 0; i < r - 2; i++)
               p[i] = p[i + 2];
         }
         p += i;
         if (*data != 0xAF || cmd == 0xAA || cmd == 0x1A || cmd == 0x0A)
            break;
         // Expect more data
         temp[0] = 0x40;        // InDataExchange
         temp[1] = Tg1;
         temp[2] = 0xAF;        // Additional frame
         if (HAL (writeCommand) (temp, 3))
            return -101;
      }
      len = p - data;
   }
   dump ("Rx(raw)", len, data);
   // Post process response
   if (*data && *data != 0xAF)
   {
      authenticated = false;
      return -2000 - *data;     // Bad status byte
   }
   if (!authenticated)
   {
      if (rxenc && rxenc != len)
         return -999;           // Not expected length
      return len;               // Length
   }
   if (rxenc)
   {                            // Decrypt and check
      if ((len % 16) != 1)
         return -999;           // Bad length
      A.cbc_decrypt (data + 1, data + 1, (len - 1) / 16);
      if (len != ((rxenc + 3) | 15) + 2)
         return -999;           // No space for CRC
      unsigned int crc = data[rxenc] + (data[rxenc + 1] << 8) + (data[rxenc + 2] << 16) + (data[rxenc + 3] << 24);
      data[rxenc] = data[0];    // CRC is of status at end
      if (desfire_crc (rxenc, data + 1) != crc)
         return -998;
      dump ("Rx", len, data);
      return rxenc;             // Logical length
   }
   // Check CMAC
   if (len < 9)
      return -999;              // No space for CMAC
   len -= 8;
   byte c1 = data[len];
   data[len] = data[0];
   desfire_cmac (temp, len, data + 1);
   data[len] = c1;
   if (memcmp (data + len, temp, 8))
      return -997;
   dump ("Rx", len, data);
   return len;
}

int
PN532RevK::desfire (byte cmd, int len, byte * buf, unsigned int maxlen, String & err, int timeout)
{                               // Simple command, takes len (after command) and returns len (after status)
   int l = desfire_dx (cmd, maxlen, buf, len + 1, 0, 0, timeout);
   if (l < 1)
   {
      byte status = buf[1];
      sprintf_P ((char *) buf, "Failed cmd %02X status %02X (%d)", cmd, status, l);
      err = String ((char *) buf);
      buf[1] = status;
   }
   return l + 1;
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
PN532RevK::getID (String & id, String & err, unsigned int timeout, byte * bid)
{                               // Return tag id
   //debug ("PN532 InListPassiveTarget");
   secure = false;
   id = String ();              // defaults
   err = String ();
   Tg1 = 0;
   uint8_t buf[128];
   buf[0] = 0x4A;               // InListPassiveTarget
   buf[1] = 2;                  // 2 tags (we only report 1)
   buf[2] = 0;                  // 106 kbps type A (ISO/IEC14443 Type A)
   int timed = micros ();
   if (HAL (writeCommand) (buf, 3))
      return 0;
   int l = HAL (readResponse) (buf, sizeof (buf), timeout); // Measured 48ms
   timed = micros () - timed;
   if (l < 6)
      return 0;
   byte tags = buf[0];
   Tg1 = buf[1];
   if (tags < 1)
      return tags;
   byte cid[10], cidlen;
   if (buf[5] > sizeof (cid))
   {                            // ID too big
      strcpy_P ((char *) buf, PSTR ("ID too long"));
      err = String ((char *) buf);
      return 0;
   }
   memcpy ((void *) cid, (void *) (buf + 6), cidlen = buf[5]);
   if (aidset)
   {
      // Select AID
      buf[1] = aid[0];
      buf[2] = aid[1];
      buf[3] = aid[2];
      timed = micros ();
      l = desfire_dx (0x5A, sizeof (buf), buf, 4, 0, 0, nfctimeout); // Measured 11ms
      timed = micros () - timed;
      if (l == PN532_TIMEOUT)
         return 0;              // Try again
      if (l == 1)
      {                         // Application exists
         // Key ID
         timed = micros ();
         buf[1] = 0x01;         // key 1
         l = desfire_dx (0x64, sizeof (buf), buf, 2, 0, 0, nfctimeout); // Measured 9ms
         timed = micros () - timed;
#ifdef REVKDEBUG
         Serial.printf ("64 time %u\n", timed);
#endif
         if (l == 2)
         {
            // AES exchange
            buf[1] = 0x01;      // key 1
            timed = micros ();
            l = desfire_dx (0xAA, sizeof (buf), buf, 2, 0, 0, nfctimeout); // Measured 18ms
            timed = micros () - timed;
            if (l != 17 || *buf != 0xAF)
            {
               byte status = buf[1];
               sprintf_P ((char *) buf, PSTR ("AA1 fail %d %02X %dus"), l, status, timed);
               err = String ((char *) buf);
               return 0;        // Retry, i.e. don't see this ID
            }
            A.set_key (aes, 16);
            A.set_IV (0);
            A.cbc_decrypt (buf + 1, sk2, 1);
            ESP8266TrueRandom.memfill ((char *) sk1, 16);
            memcpy ((void *) buf + 1, (void *) sk1, 16);
            memcpy ((void *) buf + 1 + 16, (void *) sk2 + 1, 15);
            buf[1 + 31] = sk2[0];
            A.cbc_encrypt (buf + 1, buf + 1, 2);
            timed = micros ();
	    // Measured us taking 12ms
            l = desfire_dx (0xAF, sizeof (buf), buf, 33, 0, 0, nfctimeout); // Measured 31ms
            timed = micros () - timed;
#ifdef REVKDEBUG
            Serial.printf ("AA time %u\n", timed);
#endif
            if (timed > nfctimeout * 1000)
            {
               sprintf_P ((char *) buf, PSTR ("AA2 slow %dus"), timed);
               err = String ((char *) buf);
            } else if (l != 17 || *buf)
            {
               sprintf_P ((char *) buf, PSTR ("AA2 fail %d %02X"), l, *buf);
               err = String ((char *) buf);
            } else
            {
               A.cbc_decrypt (buf + 1, buf + 1, 1);
               if (memcmp ((void *) buf + 1, sk1 + 1, 15) || buf[1 + 15] != sk1[0])
               {
                  strcpy_P ((char *) buf, PSTR ("AA AES fail"));
                  err = String ((char *) buf);
               } else
               {
                  authenticated = true;
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
                  // Get real ID
                  l = desfire_dx (0x51, sizeof (buf), buf, 1, 0, 8, nfctimeout); // Measured 19ms
                  if (l != 8)
                  {             // Failed (including failure of CRC check)
                     sprintf_P ((char *) buf, PSTR ("51 fail %d %02X"), l, *buf);
                     err = String ((char *) buf);
                  } else
                  {
                     secure = true;
                     memcpy (cid, buf + 1, cidlen = 7);
                  }
               }
            }
         }
      }
   }
   if (*err.c_str ())
      secure = false;
   if (cidlen)
   {                            // Set ID
      int n;
      for (n = 0; n < cidlen; n++)
         sprintf_P ((char *) buf + n * 2, PSTR ("%02X"), cid[n]);
      if (secure)
         strcpy_P ((char *) buf + n * 2, PSTR ("+"));   // Indicate that it is secure
      //sprintf_P ((char *) buf + n * 2, PSTR ("%s (%uus)"), secure ? "+" : "", timed);   // TODO
      id = String ((char *) buf);
      if (bid)
      {                         // Binary ID, padded with 0x00 to 10 character
         for (n = 0; n < cidlen; n++)
            bid[n] = cid[n];
         for (; n < sizeof (cid); n++)
            bid[n] = 0;
      }
   }
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
   int l = desfire (0x3B, 17, buf, sizeof (buf), err, timeout); // Measured 30ms
   if (l < 0)
      return l;
   buf[1] = 0x02;
   buf[2] = 1;                  // Credit 1
   buf[3] = 0;
   buf[4] = 0;
   buf[5] = 0;
   l = desfire (0x0C, 5, buf, sizeof (buf), err, timeout); // Measured 21ms
   if (l < 0)
      return l;
   return desfire (0xC7, 0, buf, sizeof (buf), err, timeout); // Measured 30ms
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

uint8_t
PN532RevK::release (unsigned int timeout)
{
   if (!Tg1)
      return 0;                 // Released as not set
   uint8_t buf[2];
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
