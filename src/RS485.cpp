// RS485 interface logic
// This is designed to work with an Galaxy alarm panel or peripherals on a 9600 Baud RS485 bus
// This library provides a means to send and receive messages on the bus
// It handles enabling the bus driver, and can use separate or common Tx/Rx pins
// It uses hardware timer interrupts, so supports on bus only
// Messages are a sequence of bytes with no gaps, appended with a 1's comp checksum starting 0xAA, followed by a gap
//
// Pin choices
// DE (Driver enable) should be low on start up, so ideally a pull down resistor. This makes GPIO15 on an ESP8266 ideal.
// -RE will either be tied low or tied to DE, so Rx will be an active input making GPIO0/2 unsuitable as they have to be high on startup.
// Combined Tx and Rx allowed, but switched to pull up before changing DE, so GPIO16 not suitable for combined Tx/Rx
// Tx can typically be on any pin.
// Suggested GPIO15 with 10k to GND for DE, and GPIO13 for combined Tx/Rx
//
// There is a single tx and single rx buffer (RS485MAX bytes long)
// You pre-set tx as slave, and send as response to any incoming message. It can be change as needed (blocks if mid sending previous)
// You can send tx as needed as master (set address -1 for master)
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

// Pins
static int de = -1;
static int tx = -1;
static int rx = -1;
// Timings (in bits)
static byte txpre = RS485DEFGAP;
static byte txpost = RS485DEFGAP;
static byte gap = RS485DEFGAP;
// Address on bus
static int address = -1;
// Baud rate
static unsigned int rate = 0;
static int baud = 9600;
// Ongoing status
static boolean txrx;
static boolean txdue;
static boolean txhold;
static unsigned int txgap;
static byte txbit,
  txbyte,
  txlen;
static volatile byte txpos;
static byte txdata[RS485MAX];
static boolean rxerr;
static unsigned int rxidle;
static byte rxbit,
  rxbyte,
  rxpos,
  rxlen,
  rxdue,
  rxsum;
static volatile byte rxseq;
static byte rxdata[RS485MAX];

#define US_TO_RTC_TIMER_TICKS(t)          \
    ((t) ?                                   \
     (((t) > 0x35A) ?                   \
      (((t)>>2) * ((APB_CLK_FREQ>>4)/250000) + ((t)&0x3) * ((APB_CLK_FREQ>>4)/1000000))  :    \
      (((t) *(APB_CLK_FREQ>>4)) / 1000000)) :    \
     0)

static void ICACHE_RAM_ATTR rs485_mode_tx ();
static void ICACHE_RAM_ATTR rs485_mode_rx ();
static void ICACHE_RAM_ATTR rs485_start_bit ();
static void ICACHE_RAM_ATTR rs485_bit ();
static void ICACHE_RAM_ATTR rs485_wait_start_bit ();

static void
rs485_setup ()
{                               // Set up interrupts
   ETS_FRC1_INTR_DISABLE ();
   TM1_EDGE_INT_DISABLE ();
   ETS_GPIO_INTR_DISABLE ();
   txlen = 0;
   rxlen = 0;
   rxseq = 0;
   rxdue = 0;
   txhold = false;
   txdue = false;
   if (baud > 0 && rx >= 0 && tx >= 0 && de >= 0)
   {                            // Start interrupts
      debugf ("RS485 Baud %d rx %d tx %d de %d", baud, rx, tx, de);
      rate = 1000000 / baud;
      digitalWrite (de, LOW);
      pinMode (de, OUTPUT);
      if (tx != rx)
      {                         // Separate tx
         digitalWrite (tx, LOW);
         pinMode (tx, OUTPUT);
      }
#ifdef REVKDEBUG
      pinMode (0, OUTPUT);
#endif
      ETS_FRC_TIMER1_INTR_ATTACH (rs485_bit, NULL);
      ETS_GPIO_INTR_ATTACH (rs485_start_bit, rx);
      RTC_REG_WRITE (FRC1_LOAD_ADDRESS, US_TO_RTC_TIMER_TICKS (rate));
      RTC_REG_WRITE (FRC1_CTRL_ADDRESS, FRC1_AUTO_LOAD | DIVDED_BY_16 | FRC1_ENABLE_TIMER | TM_EDGE_INT);
      TM1_EDGE_INT_ENABLE ();
      ETS_FRC1_INTR_ENABLE ();
      rs485_mode_rx ();
      ETS_GPIO_INTR_ENABLE ();
   }
}

static void ICACHE_RAM_ATTR
rs485_wait_start_bit ()
{
   gpio_pin_intr_state_set (GPIO_ID_PIN (rx), GPIO_PIN_INTR_NEGEDGE);   // Start of start bit
   rxbit = 0;
}

static void ICACHE_RAM_ATTR
rs485_start_bit ()
{                               // Initial edge of start bit - adjust int clock
   uint32 gpio_status = GPIO_REG_READ (GPIO_STATUS_ADDRESS);
   if (gpio_status & BIT (rx))
   {                            // Leading edge of start bit
      gpio_pin_intr_state_set (GPIO_ID_PIN (rx), GPIO_PIN_INTR_DISABLE);        // Not looking for start bit
      GPIO_REG_WRITE (GPIO_STATUS_W1TC_ADDRESS, BIT (rx));      // Clear status
      RTC_REG_WRITE (FRC1_LOAD_ADDRESS, US_TO_RTC_TIMER_TICKS (rate / 2));      // Assume we are half a bit off centre
      rxbit = 10;
   }
}

static void ICACHE_RAM_ATTR
rs485_bit ()
{                               // Interrupt for tx/rx per bit
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
#ifdef REVKDEBUG
      static boolean flip = 0;
      digitalWrite (0, rxbit & 1);
#endif
      int v = digitalRead (rx);
      if (!rxbit)
      {                         // Idle
         if (!v)
            rxerr = true;       // Should not happen - maybe break condition
         rxidle++;
         if (rxidle > gap)
         {                      // end of rx
            if (rxpos)
            {
               if (rxsum != rxdata[rxpos - 1])
                  rxerr = 1;
               rxlen = rxpos;   // new message received
               rxpos = 0;
               if (address >= 0)
               {
                  if (!rxerr && rxdata[0] == address)
                  {             // For us
                     txdue = true;      // Send reply as we are slave
                     rxseq++;   // Indicate rx message for us
                  }
               } else
                  rxseq++;      // receive all messages if we are master
            }
            if (txdue)
               rs485_mode_tx ();        // Can start tx
         }
         return;
      }
      rxbit--;
      if (rxbit == 9)
      {                         // Start bit
         RTC_REG_WRITE (FRC1_LOAD_ADDRESS, US_TO_RTC_TIMER_TICKS (rate));       // Start bit - put timer back
         if (v)
            rs485_wait_start_bit ();    // No start bit - try again
         return;
      }
      if (rxbit)
      {                         // Shift in
         rxbyte = (rxbyte >> 1) | (v ? 0x80 : 0);
         return;
      }
      // Stop bit
      if (!v)
         rxerr = true;          // Missing stop bit
      rxidle = 0;
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
      // End of byte
      if (rxpos >= RS485MAX)
         rxerr = true;
      else
         rxdata[rxpos++] = rxbyte;
      rs485_wait_start_bit ();
      return;
   }
   // Tx
#ifdef REVKDEBUG
   static boolean flip = 0;
   flip = 1 - flip;
   digitalWrite (0, flip);
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
         {
            txbit = 9;
            txbyte = txdata[txpos++];
         }
      }
   } else if (txbit)
   {
      if (txbit == 9)
         t = 0;                 // start bit
      else
      {
         t = (txbyte & 1);
         txbyte >>= 1;
      }
      txbit--;
   } else
   {                            // Stop bit and prep next byte
      if (txpos < txlen)
      {
         txbyte = txdata[txpos++];
         txbit = 9;
      } else
         txgap = txpost;        // End of message
   }
   digitalWrite (tx, t);
}

static void ICACHE_RAM_ATTR
rs485_mode_rx ()
{                               // Switch to rx mode
   rxidle = 0;
   rxerr = false;               // Ready for new message
   txpos = 0;
   rxlen = 0;
   if (tx == rx)
      pinMode (rx, INPUT_PULLUP);
   digitalWrite (de, LOW);
   txrx = 1;                    // Rx mode 
   rs485_wait_start_bit ();
}

static void ICACHE_RAM_ATTR
rs485_mode_tx ()
{                               // Switch to tx mode
   gpio_pin_intr_state_set (GPIO_ID_PIN (rx), GPIO_PIN_INTR_DISABLE);   // Not looking for start bit
   digitalWrite (de, HIGH);
   if (tx == rx)
   {
      digitalWrite (tx, HIGH);
      pinMode (tx, OUTPUT);
   }
   txgap = txpre;
   txpos = 0;
   txbit = 0;
   txdue = false;
   txrx = 0;                    // Tx mode
}

RS485::RS485 (int setaddress, int setde, int settx, int setrx, int setbaud)
{
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
{                               // If Rx available
   return rxdue != rxseq;
}

void
RS485::Tx (int len, byte data[])
{                               // Message to send (sent right away if master)
   if (len >= RS485MAX)
      return;
   txhold = true;               // Stop sending starting whilst we are loading
   while (txpos)
      delay (1);                // Don't overwrite whilst sending
   byte c = 0xAA;
   int p;
   for (p = 0; p < len; p++)
   {
      txdata[p] = data[p];
      if ((int) c + data[p] > 0xFF)
         c++;                   // 1's comp
      c += data[p];
   }
   txdata[p++] = c;
   txlen = p;
   if (address < 0)
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
      return 0;
   if (rxlen > max)
      return RS485TOOBIG;       // No space
   if (rxerr)
      return RS485ERROR;        // Bad rx
   int p;
   for (p = 0; p < rxlen - 1; p++)
      data[p] = rxdata[p];
   if (rxpos || rxdue != rxseq)
      return RS485MISSED;       // Missed one whilst reading data!
   return p;
}
