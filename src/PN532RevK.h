#ifndef __PN532RevK_H__
#define __PN532RevK_H__

#include <PN532Interface.h>

class PN532RevK
{
  public:
    PN532RevK(PN532Interface &interface);
    uint32_t begin(int timeout=1000); // Start, get version (0=bad)
    uint8_t cardsPresent(int timeout=1000);	// return number of cards (0=error or none)
    uint8_t inField (int timeout=1000);		// return if in field (0=OK, else status)
    uint8_t getID(String &id1,int timeout=1000); // Get the ID (returns number of cards, i.e. 0 or 1)
    uint8_t data(uint8_t txlen,uint8_t *tx,uint8_t &rxlen,uint8_t *rx,int timeout=1000); // Exchange data, return 0 if OK (status), rx has status byte at start
    uint8_t release(int timeout=1000); // Release target
    uint8_t target(int timeout=1000); // Start as target

  private:
    PN532Interface *_interface;
    uint8_t Tg1; // Tag ID
};

#endif
