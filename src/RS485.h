
#ifndef __RS485_H
#define __RS485_H

#define RS485MAX	64	// Max message length
#define RS485DEFGAP	20

class RS485 {
public:
	RS485(int address=-1,int de=-1,int tx=-1,int rx=-1,int baud=9600);
	~RS485();

	void SetAddress(int address=-1);
	void SetPins(int de=-1,int tx=-1,int rx=-1);
	void SetBaud(int baud=9600);
	void SetTiming(int gap=RS485DEFGAP,int txpre=RS485DEFGAP,int txpost=RS485DEFGAP);

	int available(); // If Rx available
	void Tx(int len,byte data[]); // Message to send (sent right away if master)
	int Rx(int max,byte data[]); // Get last message received

    
private:
};

#endif
