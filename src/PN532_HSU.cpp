// PN532, High Speed UART working
// Re-worked by RevK 2019

#include "PN532_HSU.h"

PN532_HSU::PN532_HSU (HardwareSerial & serial)
{
   _serial = &serial;
   command = 0;
}

void
PN532_HSU::begin ()
{
   _serial->begin (115200);
}

void
PN532_HSU::flush ()
{                               // Flush received data
   while (_serial->available ())
      _serial->read ();
}

int
PN532_HSU::read ()
{                               // Read next byte from serial, allow 1ms if nothing available
   if (!_serial->available ())
   {
      delay (1);
      if (!_serial->available ())
         return -1;
   }
   return _serial->read ();
}

void
PN532_HSU::wakeup ()
{
   _serial->write (0x55);
   _serial->write (0x55);
   _serial->write (0);
   _serial->write (0);
   _serial->write (0);
   _serial->write (0);
   _serial->write (0);
   _serial->write (0);
   _serial->flush ();
   delay (2);
   flush ();
}

int8_t PN532_HSU::writeCommand (const byte * header, byte hlen, const byte * body, byte blen)
{
   flush ();

   command = header[0];

   _serial->write (PN532_PREAMBLE);
   _serial->write (PN532_STARTCODE1);
   _serial->write (PN532_STARTCODE2);

   int
      length = (int) hlen + blen + 1;   // length of data field: TFI + DATA
   if (length >= 0x100)
   {                            // Extended
      _serial->write (0xFF);
      _serial->write (0xFF);
      _serial->write (length >> 8);
      _serial->write ((byte) length);
      _serial->write ((byte) (-length - (length >> 8)));        // checksum of length
   } else
   {                            // Normal
      _serial->write (length);
      _serial->write ((byte) (-length));        // checksum of length
   }

   byte
      sum = 0;
   _serial->write (sum = PN532_HOSTTOPN532);

   _serial->write (header, hlen);
   for (byte i = 0; i < hlen; i++)
      sum += header[i];

   if (blen)
   {
      _serial->write (body, blen);
      for (byte i = 0; i < blen; i++)
         sum += body[i];
   }

   _serial->write ((byte) (-sum));
   _serial->write (PN532_POSTAMBLE);

   // Get ACK
   int
      timeout = 10;
   int
      c;
   // Wait and consume 00 preambles
   while ((c = read ()) <= 0 && timeout--);
   if (c < 0)
      return PN532_TIMEOUT;
   if (c != 0xFF)
      return PN532_INVALID_ACK; // Bad start
   if ((c = read ()))
      return PN532_INVALID_ACK; // Not ACK
   if ((c = read ()) != 0xFF)
      return PN532_INVALID_ACK; // Not ACK
   if ((c = read ()))
      return PN532_INVALID_ACK; // Bad postamble

   return 0;                    // OK
}

int16_t PN532_HSU::readResponse (byte buf[], byte len, uint16_t timeout)
{
   if (timeout <= 0)
      timeout = 1000;           // Always exit eventually

   int
      c;
   // Wait and consume 00 preambles
   while ((c = read ()) <= 0 && timeout--);
   if (c < 0)
      return PN532_TIMEOUT;
   if (c != 0xFF)
      return PN532_INVALID_FRAME;       // Bad start

   int
      l;                        // length
   if ((l = read ()) < 0)
      return PN532_TIMEOUT;     // Length
   if ((c = read ()) < 0)
      return PN532_TIMEOUT;     // Length checksum
   if (l == 0xFF && c == 0xFF)
   {                            // Extended
      if ((c = read ()) < 0)
         return PN532_TIMEOUT;  // Length high
      l = c;
      if ((c = read ()) < 0)
         return PN532_TIMEOUT;  // Length low
      l = (l << 8) + c;
      if ((c = read ()) < 0)
         return PN532_TIMEOUT;  // Length checksum
   }
   if (l < 2 || (byte) ((l >> 8) + l + c))
      return PN532_INVALID_FRAME;       // Bad checksum

   l -= 2;                      // We don't include the TFI and response byte in the data we store
   if (l > len)
      return PN532_NO_SPACE;    // Too long

   byte
      sum = 0;                  // Checksum
   if ((c = read ()) < 0)
      return PN532_TIMEOUT;     // TFI
   if (c != PN532_PN532TOHOST)
      return PN532_INVALID_FRAME;       // Bad message type
   sum += c;

   if ((c = read ()) < 0)
      return PN532_TIMEOUT;     // response code
   if (c != command + 1)
      return PN532_INVALID_FRAME;       // Not the response we expected to our command
   sum += c;

   int
      p = 0;
   while (p < l)
   {
      if ((c = read ()) < 0)
         return PN532_TIMEOUT;  // data
      buf[p++] = c;
      sum += c;
   }

   if ((c = read ()) < 0)
      return PN532_TIMEOUT;     // checksum
   sum += c;
   if (sum)
      return PN532_INVALID_FRAME;       // Bad checksum

   if ((c = read ()) < 0)
      return PN532_TIMEOUT;     // Postamble
   if (c)
      return PN532_INVALID_FRAME;       // Bad postamble

   return l;
}
