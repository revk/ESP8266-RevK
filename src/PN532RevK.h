#ifndef __PN532RevK_H__
#define __PN532RevK_H__

#include <PN532Interface.h>
#include <AES.h>

class PN532RevK
{
  public:
    PN532RevK(PN532Interface &interface);
    uint32_t begin(unsigned int timeout=1000); // Start, get version (0=bad)
    uint8_t cardsPresent(unsigned int timeout=100);	// return number of cards (0=error or none)
    uint8_t inField (unsigned int timeout=100);		// return if in field (0=OK, else status)
    uint8_t data(uint8_t txlen,uint8_t *tx,uint8_t &rxlen,uint8_t *rx,unsigned int timeout=1000); // Exchange data, return 0 if OK (status), rx has status byte at start
    uint8_t release(unsigned int timeout=100); // Release target
    uint8_t target(unsigned int timeout=100); // Start as target
    uint8_t led(uint8_t led=0,unsigned int timeout=100);	// Set GPIO output (e.g. LED)
    int p3(unsigned int timeout=100);

    // DESFire low level functions
    void set_aid(const uint8_t *aid);	 // Set AID (3 bytes)
    void set_aes(const uint8_t *aes);	 // Set AES (8 bytes)

    unsigned int desfire_crc(unsigned int len, byte*data);		// Calculate DESFire CRC
    void desfire_cmac(byte cmacout[16],unsigned int len,byte*data);	// Updated DESFire CMAC and return

    // Whilst there are few high level DESFire functions, most can be done easily using this
    // Data Exchange function.

    // Data exchange, sends a command and receives a response
    // Note that data[] is used for command and response, and is max bytes long - allow at least 19 spare bytes at end for CRC and padding
    // Command:
    //  The command is in data, starting with command byte in data[0], and is txlen bytes long
    //  Note that cmd arg is for convenience and if non 0 is simply stored in data[0]
    //  If the command is long, it is split and sent using AF process
    //  If not authenticated the command is sent, plain
    //  If authenticated and txenc set, then it is sent encrypted
    //   - A CRC is added to the end (at txlen), adding 4 bytes
    //   - The command is padded as needed (up to 16 bytes)
    //   - The command is encrypted from byte txenc (i.e. txenc bytes not encrypted)
    //   - The encryption updates the AES A IV used for CMAC checking
    //  If authenticated and txenc is 0, then the command is sent plain
    //   - The command is CMAC processed to update the AES A IV for checking
    // Response
    //  If response has AF status, multiple response payloads are concatenated with final status at start.
    //  If not authenticated and rxenc is set, this is the number of bytes expected, else error
    //   - The return value is the length of response including status byte at data[0]
    //  If authenticated and rxenc is set then this is expected to be an encrypted message
    //   - The length has to be a multiple of 16 bytes (after status byte)
    //   - The payload is decrypted (i.e. all data after status byte)
    //   - A CRC is expected at byte rxenc, this is checked as well and length checked
    //   - The AES A IV is updated for CMAC checking
    //   - The return value is rxenc, i.e. rxenc includes status byte in count
    //  If autenticated and rxenc is 0 then an 8 byte CMAC is expected
    //   - The 8 bytes are removed
    //   - The CMAC process is done on response (payload+status) and checked
    //   - The return value is the length without the 8 byte CMAC
    //  If case of any error the return value is -ve
    // Special cases
    //  Receive concatenation is not done for cmd AA, 1A or 0A. The AF response is treated as good.
    //  Send with txenc and cmd C4 does not add the CRC. ChangeKey has an extra CRC and padding you need to do first.
    // Examples
    //  Cmd 54 with txenc 1, rxenc 0, and len 2, adds CRC and encrypts from byte 1, returns 1 (status byte)
    //  Cmd 51 with txenc 0, rxenc 8, and len 1, sends 51, receives 17 bytes, decrypts and checks CRC at byte 8, returns 8 (status + 7 byte UID)
    int desfire_dx(byte cmd,unsigned int max,byte*data,unsigned int txlen,byte txenc=0,byte rxenc=0,int timeout=0);
    // Simplified (len and return are byte count after cmd/status)
    int desfire (byte cmd, int len, byte * buf, unsigned int maxlen, String & err, int timeout);


    // DEFire Higher level functions
    // This gets the card ID as a string, but if aid is set it authenticates and gets ID and appends + to string
    uint8_t getID(String &id,String &err,unsigned int timeout=1000);
    boolean secure; // If we have secure ID confirmed
    boolean aidset;			 // If we have an AID for secure use
    // Add a log record: chipID and timestamp, 10 byte record to file 1, and credit value on file 2 by 1
    int desfire_log (String &err,int timeout=0);

  private:
    PN532Interface *_interface;
    byte Tg1; // Tag ID
    AES A;			// AES for secure desfire comms, holding current IV
    byte sk1[16],sk2[16];	 // Sub keys for secure desfire comms
    byte aid[3]; // AID for security checks
    byte aes[16];	// AES for security checks
    boolean authenticated;		 // We are authenticated to the card
};

#endif
