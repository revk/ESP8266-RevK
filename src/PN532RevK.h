#ifndef __PN532RevK_H__
#define __PN532RevK_H__

#include <PN532Interface.h>

class PN532RevK
{
  public:
    PN532RevK(PN532Interface &interface);
    uint32_t begin(unsigned int timeout=1000); // Start, get version (0=bad)
    uint8_t cardsPresent(unsigned int timeout=100);	// return number of cards (0=error or none)
    uint8_t inField (unsigned int timeout=100);		// return if in field (0=OK, else status)
    uint8_t getID(String &id1,unsigned int timeout=1000); // Get the ID (returns number of cards, i.e. 0 or 1)
    uint8_t data(uint8_t txlen,uint8_t *tx,uint8_t &rxlen,uint8_t *rx,unsigned int timeout=1000); // Exchange data, return 0 if OK (status), rx has status byte at start
    uint8_t release(unsigned int timeout=100); // Release target
    uint8_t target(unsigned int timeout=100); // Start as target
    uint8_t led(uint8_t led=0,unsigned int timeout=100);	// Set GPIO output (e.g. LED)
    int p3(unsigned int timeout=100);
    void set_aid(const uint8_t *aid);	 // Set AID (3 bytes)
    void set_aes(const uint8_t *aes);	 // Set AES (8 bytes)

  private:
    PN532Interface *_interface;
    byte Tg1; // Tag ID
    byte aid[3]; // AID for security checks
    byte aes[16];	// AES for security checks
};

#endif
