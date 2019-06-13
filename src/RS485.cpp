// RS485 interface logic
// This is designed to work with an Galaxy alarm panel or peripherals on a 9600 Baud RS485 bus
// This library provides a means to send and receive messages on the bus
// It handles enabling the bus driver, and can use separate or common Tx / Rx pins
// It uses hardware timer interrupts, so supports on bus only
// Messages are a sequence of bytes with no gaps, appended with a 1 's comp checksum starting 0xAA, followed by a gap
//
//Pin choices
// DE(Driver enable) should be low on start up, so ideally a pull down resistor.
// This makes GPIO15 on an ESP8266 ideal.
// -RE will either be tied low or tied to DE, so Rx will be an active input making GPIO0 / 2 unsuitable as they have to be high on startup.
// Combined Tx and Rx allowed, but switched to pull up before changing DE, so GPIO16 not suitable for combined Tx / Rx
// Tx can typically be on any pin.
// Suggested GPIO15 with 10 k to GND for DE, and GPIO13 for combined Tx / Rx
//
// There is a single tx and single rx buffer(RS485MAX bytes long)
// You pre - set tx as slave, and send as response to any incoming message.It can be change as needed(blocks if mid sending previous)
// You can send tx as needed as master(set address - 1 for master)
// Master rx all packets, slave only those that match address

#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "gpio.h"
#include "Arduino.h"
#include "ESPRevK.h"
#include "RS485.h"
#define FRC1_ENABLE_TIMER  	BIT7
#define FRC1_AUTO_LOAD     	BIT6
#define DIVDED_BY_1		0       //timer clock
#define DIVDED_BY_16		4       //divided by 16
#define DIVDED_BY_256		8       //divided by 256
#define TM_LEVEL_INT 		1       // level interrupt
#define TM_EDGE_INT   		0       //edge interrupt
                        //              Pins
static int de = -1;             // Drive enable pin(often also Not Receive Enable)
static int tx = -1;             // Tx pin
static int rx = -1;             // Rx pin(can be same as Tx pin)
// Settings
static byte gap = 10;           // Gap after which we assume end of message (bits)
static byte txpre = 1;          // Pre tx drive(high) (bits)
static byte txpost = 1;         // Post tx drive(high) (bits)
static int address = -1;        // First byte of message is target address.If this is >= 0 we are a slave on this address
static int baud = 9600;         // Baud rate
// Status
static byte subbit;             // Sub bit count, 3 interrupts per bit for rx alignment
static byte bit;		// Bit number
static byte shift;		// Byte (shifting in/out)
// Tx
static boolean txrx;            // Mode, true for rx , false for tx //Tx
static boolean txdue;           // We are due to send a message
static boolean txhold;          // We are in app Tx call and need to hold of sending as copying to buffer
static unsigned int txgap;      // We are sending pre / post drive
static byte txlen;                        // The length of tx buf
static volatile byte txpos;     // Position in tx buf we are sending(checked by app level)
static byte txdata[RS485MAX];   // The tx message
// Rx
static boolean rxignore;        // This message is not for us so being ignored
static int8_t rxerr,            // The current in progress message rx error
  rxerrorreport;                // The rxerror of the last stored message
volatile byte rxpos;		// Where we are in rx buf
static byte rxlen,                        // Length of last received mesage in rxbuf
  rxdue,                        // The expected message sequence
  rxsum,                        // The current checksum
  rxgap;                        // The remaining end of message timeout
static volatile byte rxseq;     // The last received message sequence
static byte rxdata[RS485MAX];   // The Rx data

#define US_TO_RTC_TIMER_TICKS(t)          \
    ((t) ?                                   \
     (((t) > 0x35A) ?                   \
      (((t)>>2) * ((APB_CLK_FREQ>>4)/250000) + ((t)&0x3) * ((APB_CLK_FREQ>>4)/1000000))  :    \
      (((t) *(APB_CLK_FREQ>>4)) / 1000000)) :    \
     0)

static void ICACHE_RAM_ATTR rs485_mode_tx ();
static void ICACHE_RAM_ATTR rs485_mode_rx ();
static void ICACHE_RAM_ATTR rs485_bit ();

static void
rs485_setup ()
{
   //Set up interrupts
   ETS_FRC1_INTR_DISABLE ();
   TM1_EDGE_INT_DISABLE ();
   txlen = 0;
   rxlen = 0;
   rxseq = 0;
   rxdue = 0;
   txhold = false;
   txdue = false;
   subbit=0;
   rxpos=0;
   txpos=0;
   bit=0;
   if (baud > 0 && rx >= 0 && tx >= 0 && de >= 0)
   {
      //Start interrupts
      debugf ("RS485 Baud %d rx %d tx %d de %d", baud, rx, tx, de);
      unsigned int rate = 1000000 / baud / 3;
      //3 ticks per bit
      digitalWrite (de, LOW);
      pinMode (de, OUTPUT);
      if (tx != rx)
      {
         //Separate tx
         digitalWrite (tx, LOW);
         pinMode (tx, OUTPUT);
      }
#ifdef REVKDEBUG
      pinMode (0, OUTPUT);
#endif
      ETS_FRC_TIMER1_INTR_ATTACH (rs485_bit, NULL);
      RTC_REG_WRITE (FRC1_LOAD_ADDRESS, US_TO_RTC_TIMER_TICKS (rate));
      RTC_REG_WRITE (FRC1_CTRL_ADDRESS, FRC1_AUTO_LOAD | DIVDED_BY_16 | FRC1_ENABLE_TIMER | TM_EDGE_INT);
      TM1_EDGE_INT_ENABLE ();
      ETS_FRC1_INTR_ENABLE ();
      rs485_mode_rx ();
   }
}

static void ICACHE_RAM_ATTR
rs485_bit ()
{                               // Interrupt for tx / rx per bit
#if 1
   // Bodge, delay startup regardless - crashy crashy for some reason.
   static long init = 10000;
   if (init)
   {
      init--;
      return;
   }
#endif
   if (txrx)
   {                            // Rx
      int v = digitalRead (rx);
      if (!v && !bit)
      {                         // Idle, and low, so this is start of start bit
         subbit = 2;	// Centre of start bit, ish
         bit = 10;
      }
      if (--subbit)
         return;
      subbit = 3;
#ifdef REVKDEBUG
      static boolean flip = 0;
      digitalWrite (0, bit & 1);
#endif
      if (!bit)
      {                         // Idle
         if (rxgap)
            rxgap--;
         else
         {                      // End of rx
            rxignore = false;
            if (rxpos)
            {
               // Message received
               if (rxsum != rxdata[rxpos - 1])
                  rxerr = RS485CHECKSUM;
               rxlen = rxpos;
               rxerrorreport = rxerr;
               rxseq++;
               if (address >= 0)
                  txdue = true;
               rxpos = 0;       // ready for next message
            }
            if (txdue)
               rs485_mode_tx ();        // Can start tx
         }
         return;
      }
      bit--;
      if (bit == 9)
      {                         // Start bit
         if (v)
         {                      // Missing start bit
            rxerr = RS485STARTBIT;
            bit = 0;
         }
         return;
      }
      if (bit)
      {                         // Shift in
         shift = (shift >> 1) | (v ? 0x80 : 0);
         return;
      }
      if (!rxpos)
         rxerr = 0;             // Clear errors, new message
      // Stop bit
      if (!v)
         rxerr = RS485STOPBIT;  // Missing stop bit
      rxgap = gap;              // Look for end of message
      // Checksum logic
      if (!rxpos)
         rxsum = 0xAA;
      else
      {
         byte l = rxdata[rxpos - 1];
         if ((int) rxsum + l > 0xFF)
            rxsum++;
         rxsum += l;
      }
      if (!rxpos && address >= 0 && shift != address)
         rxignore = true;       // We are slave, this is not addressed to us, ignore
      if (rxignore)
         return;                // Not for us
      // End of byte
      if (rxpos >= RS485MAX)
         rxerr = RS485TOOBIG;
      else
         rxdata[rxpos++] = shift;
      return;
   }
   // Tx
   if (--subbit)
      return;
   subbit = 3;
#if 0
#ifdef REVKDEBUG
   static boolean flip = 0;
   flip = 1 - flip;
   digitalWrite (0, flip);
#endif
#endif
   byte t = 1;
   if (txgap)
   {
      t = 1;
      txgap--;
      if (!txgap)
      {                         // End of gap
         if (txpos)
         {                      // End of message
            txpos = 0;
            rs485_mode_rx ();   // Switch back to rx
            return;
         }
         // Start of message
         if (txhold)
            txgap++;            // Wait, app is writing new message
         else
         {                      // Start sending
            bit = 9;
            shift = txdata[txpos++];
         }
      }
   } else if (bit)
   {
      if (bit == 9)
         t = 0;                 // Start bit
      else
      {                         // Data bit
         t = (shift & 1);
         shift >>= 1;
      }
      bit--;
   } else
   {                            // Stop bit and prep next byte
      if (txpos < txlen)
      {
         shift = txdata[txpos++];
         bit = 9;
      } else
         txgap = txpost;        // End of message
   }
   digitalWrite (tx, t);
}

static void ICACHE_RAM_ATTR
rs485_mode_rx ()
{ // Switch to rx mode
   if (tx == rx)
      pinMode (rx, INPUT_PULLUP);
   digitalWrite (de, LOW);
   txrx = 1;                    // Rx mode
}

static void ICACHE_RAM_ATTR
rs485_mode_tx ()
{ // Switch to tx mode
   digitalWrite (de, HIGH);
   if (tx == rx)
   {
      digitalWrite (tx, HIGH);
      pinMode (tx, OUTPUT);
   }
   txgap = txpre;
   txdue = false;
   txrx = 0;                    // Tx mode
}

RS485::RS485 (int setaddress, int setde, int settx, int setrx, int setbaud)
{
   SetTiming (); // Defaults
   SetAddress (setaddress);
   SetBaud (setbaud);
   SetPins (setde, settx, setrx);
}

RS485::~RS485 ()
{
   SetBaud (-1);
}

void
RS485::SetAddress (int a)
{
   address = a;
   rs485_setup ();
}

void
RS485::SetPins (int setde, int settx, int setrx)
{
   de = setde;
   tx = settx;
   rx = setrx;
   rs485_setup ();
}

void
RS485::SetTiming (int setgap, int settxpre, int settxpost)
{
   gap = setgap;
   txpre = settxpre;
   txpost = settxpost;
}


void
RS485::SetBaud (int setbaud)
{
   baud = setbaud;
   rs485_setup ();
}

int
RS485::available ()
{ // If Rx available
   return rxdue != rxseq;
}

void
RS485::Tx (int len, byte data[])
{ // Message to send(sent right away if master)
   if (len >= RS485MAX)
      return;
   txhold = true; // Stop sending starting whilst we are loading
   while (txpos)
      delay (1); // Don 't overwrite whilst sending
   byte c = 0xAA;
   int p;
   for (p = 0; p < len; p++)
   {
      txdata[p] = data[p];
      if ((int) c + data[p] > 0xFF)
         c++;
      //1 's comp
      c += data[p];
   }
   txdata[p++] = c;
   txlen = p;
   if (address < 0)
      txdue = true; // Send now(if slave we send when polled)
   txhold = false; // Allow sending
}

int
RS485::Rx (int max, byte data[])
{ // Get last message received
   if (rxdue == rxseq)
      return 0; // Nothing ready
   rxdue++;
   if (rxdue != rxseq)
      return RS485MISSED; // Missed one
   if (!rxlen)
      return 0;
   if (rxlen > max)
      return RS485TOOBIG; // No space
   if (rxerrorreport)
      return rxerrorreport; // Bad rx
   int p;
   for (p = 0; p < rxlen - 1; p++)
      data[p] = rxdata[p];
   if (rxpos || rxdue != rxseq)
      return RS485MISSED; // Missed one whilst reading data !
   return p;
}
