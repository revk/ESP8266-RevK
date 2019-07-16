
#ifndef __PN532_HSU_H__
#define __PN532_HSU_H__

#include "PN532Interface.h"
#include "Arduino.h"

#define PN532_HSU_DEBUG

class PN532_HSU : public PN532Interface {
public:
    PN532_HSU(HardwareSerial &serial);
    
    void begin();
    void wakeup();
    virtual int8_t writeCommand(const byte *header, byte hlen, const byte *body = 0, byte blen = 0);
    int16_t readResponse(byte buf[], byte len, uint16_t timeout);
    uint8_t available();
    int32_t waiting();
    
private:
    HardwareSerial* _serial;
    byte command;
    
    void flush();
    int read();

    int32_t lastsent;
};

#endif
