
#ifndef __RS485_H
#define __RS485_H

#define RS485MAX	64
#define	RS485MISSED	-1
#define	RS485TOOBIG	-2
#define	RS485STARTBIT	-3
#define	RS485STOPBIT	-4
#define RS485CHECKSUM	-5


class RS485 {
public:
	RS485(byte address,boolean slave,int de=-1,int tx=-1,int rx=-1,int baud=9600);
	~RS485();

	void SetAddress(byte address,boolean slave);
	void SetPins(int8_t de=-1,int8_t tx=-1,int8_t rx=-1,int8_t clk=-1);
	void SetBaud(int baud=9600);
	void SetTiming(byte gap=10,byte txpre=50,byte txpost=40); // Keypad pre is 5ms before and 4ms after
	void Start(); // Start modem
	void Stop();

	int Available(); // If Rx available
	void Tx(int len,byte data[]); // Message to send (sent right away if master)
	int Rx(int max,byte data[]); // Get last message received

    
private:
};

#endif
