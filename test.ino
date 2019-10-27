#include "proto.h"

RF24 radio(8,9);
Proto Master(radio,"Master",10,5,true);

void setup() {
	Serial.begin(115200);
  	printf_begin();
 	radio.begin();
   	radio.setAutoAck(false);
   	radio.setChannel(112);
    radio.setRetries(3,5); // delay, count
  	//radio.setPALevel(RF24_PA_MIN);
  	radio.setDataRate(RF24_250KBPS);
	Master.setup();
}

void loop() {
	// put your main code here, to run repeatedly:

}
