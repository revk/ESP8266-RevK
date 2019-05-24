#include "PN532_SPI.h"
#include "PN532RevK.h"
#include "ESPRevK.h"

#define HAL(func)   (_interface->func)

PN532RevK::PN532RevK (PN532Interface & interface)
{
   _interface = &interface;
}

uint32_t PN532RevK::begin (unsigned int timeout)
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

uint8_t PN532RevK::getID (String & id1, unsigned int timeout)
{                               // Return tag id
   //debug ("PN532 InListPassiveTarget");
   id1 = String ();             // defaults
   Tg1 = 0;
   uint8_t
      buf[64];
   buf[0] = 0x4A;               // InListPassiveTarget
   buf[1] = 1;                  // 1 tag
   buf[2] = 0;                  // 106 kbps type A (ISO/IEC14443 Type A)
   if (HAL (writeCommand) (buf, 3))
      return 0;
   int
      l = HAL (readResponse) (buf, sizeof (buf), timeout);
   if (l < 0 || l > 1)
      debugf ("PN532 InListPassiveTarget Tags=%d len=%d", buf[0], l);
   if (l < 6)
      return 0;
   int
      tags = buf[0];
   Tg1 = buf[1];
   if (tags < 1)
      return tags;
   char
      cid[21];
   int
      n;
   for (n = 0; n < 10 && n < buf[5]; n++)
      sprintf_P (cid + n * 2, PSTR ("%02X"), buf[6 + n]);
   id1 = String (cid);
   return tags;
}

uint8_t PN532RevK::data (uint8_t txlen, uint8_t * tx, uint8_t & rxlen, uint8_t * rx, unsigned int timeout)
{                               // Data exchange, fills in data with status byte
   uint8_t
      rxspace = rxlen;
   rxlen = 0;
   if (!Tg1)
      return 0xFF;              // No tag
   uint8_t
      buf[2];
   buf[0] = 0x40;               // InDataExchange
   buf[1] = Tg1;
   int
      len;
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

uint8_t PN532RevK::target (unsigned int timeout)
{
   if (Tg1)
      release (timeout);
   uint8_t
      buf[38],
      n;
   for (n = 0; n < sizeof (buf); n++)
      buf[n] = 0;
   buf[0] = 0x8C;               // TgInitAsTarget
   buf[1] = 0x05;               // PICC+passive
   uint32_t
      c = ESP.getChipId ();
   buf[4] = (c >> 16);          // Mifare NFCID1
   buf[5] = (c >> 8);
   buf[6] = c;
   buf[7] = 0x20;               // Mifare SEL_RES (ISO/IEC14443-4 PICC emulation)
// TODO way more shit to code here

}
