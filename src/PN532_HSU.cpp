
#include "PN532_HSU.h"
#include "PN532_debug.h"


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
PN532_HSU::wakeup ()
{
   _serial->write (0x55);
   _serial->write (0x55);
   _serial->write (0);
   _serial->write (0);
   _serial->write (0);

    /** dump serial buffer */
   if (_serial->available ())
   {
      DMSG ("Dump serial buffer: ");
      while (_serial->available ())
      {
         uint8_t ret = _serial->read ();
         DMSG_HEX (ret);
      }
   }

}

int8_t
PN532_HSU::writeCommand (const uint8_t * header, uint8_t hlen, const uint8_t * body, uint8_t blen)
{

    /** dump serial buffer */
   if (_serial->available ())
   {
      DMSG ("Dump serial buffer: ");
      while (_serial->available ())
      {
         uint8_t ret = _serial->read ();
         DMSG_HEX (ret);
      }
   }

   command = header[0];

   _serial->write (PN532_PREAMBLE);
   _serial->write (PN532_STARTCODE1);
   _serial->write (PN532_STARTCODE2);

   uint8_t length = hlen + blen + 1;    // length of data field: TFI + DATA
   _serial->write (length);
   _serial->write (~length + 1);        // checksum of length

   _serial->write (PN532_HOSTTOPN532);
   uint8_t sum = PN532_HOSTTOPN532;     // sum of TFI + DATA

   DMSG ("\nWrite: ");

   _serial->write (header, hlen);
   for (uint8_t i = 0; i < hlen; i++)
   {
      sum += header[i];

      DMSG_HEX (header[i]);
   }

   _serial->write (body, blen);
   for (uint8_t i = 0; i < blen; i++)
   {
      sum += body[i];

      DMSG_HEX (body[i]);
   }

   uint8_t checksum = ~sum + 1; // checksum of TFI + DATA
   _serial->write (checksum);
   _serial->write (PN532_POSTAMBLE);

   return readAckFrame ();
}

int16_t
PN532_HSU::readResponse (uint8_t buf[], uint8_t len, uint16_t timeout)
{
   uint8_t tmp[3];

   DMSG ("\nRead:  ");

   // Only using timeout for initial bytes, rest should come in at full speed so timeout is only if something goes wrong

    /** Frame Preamble and Start Code */
   if (receive (tmp, 3, timeout) != 3)
      return PN532_TIMEOUT;

   if (tmp[0] || tmp[1] || tmp[2] != 0xFF)
   {
      DMSG ("Preamble error");
      return PN532_INVALID_FRAME;
   }

    /** receive length and check */
   uint8_t length[2];
   if (receive (length, 2, 2) != 2)
      return PN532_TIMEOUT;

   if ((uint8_t) (length[0] + length[1]))
   {
      DMSG ("Length error");
      return PN532_INVALID_FRAME;
   }

   length[0] -= 2;
   if (length[0] > len)
      return PN532_NO_SPACE;

    /** receive command byte */
   uint8_t cmd = command + 1;   // response command
   if (receive (tmp, 2, 2) != 2)
      return PN532_TIMEOUT;

   if (tmp[0] != PN532_PN532TOHOST || tmp[1] != cmd)
   {
      DMSG ("Command error");
      return PN532_INVALID_FRAME;
   }

   if (receive (buf, length[0], 2) != length[0])
      return PN532_TIMEOUT;

   uint8_t sum = PN532_PN532TOHOST + cmd;
   for (uint8_t i = 0; i < length[0]; i++)
      sum += buf[i];

    /** checksum and postamble */
   if (receive (tmp, 2, 2) != 2)
      return PN532_TIMEOUT;

   if ((uint8_t) (sum + tmp[0]) || tmp[1])
   {
      DMSG ("Checksum error");
      return PN532_INVALID_FRAME;
   }

   return length[0];
}

int8_t
PN532_HSU::readAckFrame ()
{
   const uint8_t PN532_ACK[] = { 0, 0, 0xFF, 0, 0xFF, 0 };
   uint8_t ackBuf[sizeof (PN532_ACK)];

   DMSG ("\nAck: ");

   if (receive (ackBuf, sizeof (PN532_ACK), PN532_ACK_WAIT_TIME) != sizeof (PN532_ACK))
   {
      DMSG ("Timeout\n");
      return PN532_TIMEOUT;
   }

   if (memcmp (ackBuf, PN532_ACK, sizeof (PN532_ACK)))
   {
      DMSG ("Invalid\n");
      return PN532_INVALID_ACK;
   }
   return 0;
}

/**
    @brief receive data .
    @param buf --> return value buffer.
           len --> length expect to receive.
           timeout --> time of receiving
    @retval number of received bytes, 0 means no data received.
*/
int8_t
PN532_HSU::receive (uint8_t * buf, int len, uint16_t timeout)
{
   int read_bytes = 0;
   int ret;
   unsigned long start_millis;

   if (!timeout)
      timeout = 1000;           // Ensure we do give up eventually

   // Wait for response
   start_millis = millis ();
   while (!_serial->available ())
   {
      delay (1);                // Allow for wifi, etc, to work
      if (!_serial->available () && (millis () - start_millis) >= timeout)
         return PN532_TIMEOUT;
   }

   while (read_bytes < len)
   {
      start_millis = millis ();
      do
      {
         ret = _serial->read ();
         if (ret >= 0)
            break;
         delay (1);             // Allow for wifi, etc, to work
      } while ((millis () - start_millis) < 2);

      if (ret < 0)
         ret = _serial->read ();        // Try again just in case delay caused us to exit

      if (ret < 0)
      {
         if (read_bytes)
            return read_bytes;
         return PN532_TIMEOUT;
      }
      buf[read_bytes] = (uint8_t) ret;
      DMSG_HEX (ret);
      read_bytes++;
   }
   return read_bytes;
}
