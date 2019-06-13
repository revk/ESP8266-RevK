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
// GPIO16 is not supported as we use fast GPIO register access directly and GPIO16 is special
//
// There is a single tx and single rx buffer(RS485MAX bytes long)
// You can call Tx at any time, if a message is being sent you block until done.
// Calling Tx as master causes send once ASAP
// Calling Tx as slave sets the message to be sent in reply to any received message
// As slave, if you call Tx fast enough after received message you can reply directly, else previously set tx is sent anyway
// Any received messages starting with your address byte are received and show as Available()
// You can call Rx to get last message, error codes for missed messages or errors
//
// TODO - logically we could have multiple instances if all same Baud rate

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
static int8_t de = -1;          // Drive enable pin(often also Not Receive Enable)
static int8_t tx = -1;          // Tx pin
static int8_t rx = -1;          // Rx pin(can be same as Tx pin)
static int8_t clk = -1;         // Debug clock
// Settings
static byte gap = 10;           // Gap after which we assume end of message (bits)
static byte txpre = 1;          // Pre tx drive(high) (bits)
static byte txpost = 1;         // Post tx drive(high) (bits)
static byte address = 0;        // First byte of message is target address
static boolean slave = false;   // If we are not master we reply when polled
static boolean running = false; // Are we even running
static unsigned int baud = 9600;        // Baud rate
// Status
// Tx
static boolean txrx;            // Mode, true for rx , false for tx //Tx
static boolean txdue;           // We are due to send a message
static boolean txhold;          // We are in app Tx call and need to hold of sending as copying to buffer
static unsigned int txgap;      // We are sending pre / post drive
static byte txlen;              // The length of tx buf
static volatile byte txpos;     // Position in tx buf we are sending(checked by app level)
static byte txdata[RS485MAX];   // The tx message
// Rx
static boolean rxignore;        // This message is not for us so being ignored
static int8_t rxerr,            // The current in progress message rx error
  rxerrorreport;                // The rxerror of the last stored message
volatile byte rxpos;            // Where we are in rx buf
static byte rxlen,              // Length of last received mesage in rxbuf
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
   rxpos = 0;
   txpos = 0;
   if (running && baud > 0 && rx >= 0 && tx >= 0 && de >= 0)
   {
      //Start interrupts
      debugf ("RS485 Baud %d rx %d tx %d de %d", baud, rx, tx, de);
      unsigned int rate = 1000000 / baud / 3;
      //3 ticks per bit
      digitalWrite (de, LOW);
      pinMode (de, OUTPUT);
      pinMode (rx, INPUT_PULLUP);
      if (tx != rx)
      {                         // Separate tx
         digitalWrite (tx, HIGH);
         pinMode (tx, OUTPUT);
      }
      if (clk >= 0)
         pinMode (clk, OUTPUT); // Debug clock output
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
   static byte subbit = 0;      // Sub bit count, 3 interrupts per bit for rx alignment
   static byte bit = 0;         // Bit number
   static byte shift = 0;       // Byte (shifting in/out)
   if (txrx)
   {                            // Rx
      int v = (GPIO_REG_READ (GPIO_IN_ADDRESS) & (1 << rx));
      if (!v && !bit)
      {                         // Idle, and low, so this is start of start bit
         subbit = 1;            // Centre of start bit (+/- 1/6 of a bit)
         bit = 10;
      }
      if (subbit--)
         return;
      subbit = 2;               // Three sub bits per bit
      if (clk >= 0)
         GPIO_REG_WRITE ((bit & 1) ? GPIO_OUT_W1TS_ADDRESS : GPIO_OUT_W1TC_ADDRESS, 1 << clk);  // Debug clock output
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
               if (slave)
                  txdue = true; // Send reply as we are slave
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
            bit = 0; // Back to idle
         }
         return;
      }
      if (bit)
      {                         // Shift in
         shift >>= 1;
         if (v)
            shift |= 0x80;
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
            rxsum++; // 1's comp
         rxsum += l;
      }
      if (!rxpos && shift != address)
         rxignore = true;       // Not addressed to us, ignore
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
   if (subbit--)
      return;
   subbit = 2;                  // Three sub bits per bit
   if (clk >= 0)
   {
      static boolean flip = 0;
      flip = 1 - flip;
      GPIO_REG_WRITE (flip ? GPIO_OUT_W1TS_ADDRESS : GPIO_OUT_W1TC_ADDRESS, 1 << clk);  // Debug clock output
   }
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
   GPIO_REG_WRITE (t ? GPIO_OUT_W1TS_ADDRESS : GPIO_OUT_W1TC_ADDRESS, 1 << tx);
}

static void ICACHE_RAM_ATTR
rs485_mode_rx ()
{                               // Switch to rx mode
   if (tx == rx)
      GPIO_REG_WRITE (GPIO_ENABLE_W1TC_ADDRESS, 1 << rx);       // Switch to input
   GPIO_REG_WRITE (GPIO_OUT_W1TC_ADDRESS, 1 << de);     // DE low, for rx
   txrx = true;                 // Rx mode
}

static void ICACHE_RAM_ATTR
rs485_mode_tx ()
{                               // Switch to tx mode
   GPIO_REG_WRITE (GPIO_OUT_W1TS_ADDRESS, 1 << de);     // DE high, for tx
   if (tx == rx)
      GPIO_REG_WRITE (GPIO_ENABLE_W1TS_ADDRESS, 1 << rx);       // Switch to output
   txgap = txpre;
   txdue = false;
   txrx = false;                // Tx mode
}

RS485::RS485 (byte setaddress, boolean slave, int setde, int settx, int setrx, int setbaud)
{
   SetTiming ();                // Defaults
   SetAddress (setaddress, slave);
   SetBaud (setbaud);
   SetPins (setde, settx, setrx);
}

RS485::~RS485 ()
{
   running = false;
   rs485_setup ();
}

void
RS485::Start ()
{
   running = true;
   rs485_setup ();
}

void
RS485::Stop ()
{
   running = false;
   rs485_setup ();
}

void
RS485::SetAddress (byte setaddress, boolean setslave)
{
   address = setaddress;
   slave = setslave;
   rs485_setup ();
}

void
RS485::SetPins (int8_t setde, int8_t settx, int8_t setrx, int8_t setclk)
{
   de = setde;
   tx = settx;
   rx = setrx;
   clk = setclk;
   rs485_setup ();
}

void
RS485::SetTiming (byte setgap, byte settxpre, byte settxpost)
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
RS485::Available ()
{                               // If Rx available
   return rxdue != rxseq;
}

void
RS485::Tx (int len, byte data[])
{                               // Message to send(sent right away if master)
   if (len >= RS485MAX)
      return;
   txhold = true;               // Stop sending starting whilst we are loading
   while (txpos)
      delay (1);                // Don't overwrite whilst sending - block until sent
   byte c = 0xAA;
   int p;
   for (p = 0; p < len; p++)
   {
      txdata[p] = data[p];
      if ((int) c + data[p] > 0xFF)
         c++; // 1 's comp
      c += data[p];
   }
   txdata[p++] = c; // Checksum
   txlen = p;
   if (!slave)
      txdue = true;             // Send now (if slave we send when polled)
   txhold = false;              // Allow sending
}

int
RS485::Rx (int max, byte data[])
{                               // Get last message received
   if (rxdue == rxseq)
      return 0;                 // Nothing ready
   rxdue++;
   if (rxdue != rxseq)
      return RS485MISSED;       // Missed one
   if (!rxlen)
      return 0;                 // Uh?
   if (rxerrorreport)
      return rxerrorreport;     // Bad rx
   if (rxlen > max)
      return RS485TOOBIG;       // No space
   int p;
   for (p = 0; p < rxlen - 1; p++)
      data[p] = rxdata[p];
   if (rxpos || rxdue != rxseq)
      return RS485MISSED;       // Missed one whilst reading data !
   return p;
}
