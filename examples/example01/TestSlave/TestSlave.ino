// arduino uno
#include "../../../proto.h"
//#include "printf.h"

RF24 radio(8,9);
Proto uno(radio,"Slave1",10,5,true);

Device temperature{Input,AnalogFloat,0,0,"Temp1"};
Device humidity{Input,AnalogFloat,0,0,"Hum1"};
Device level{Input,AnalogInt16,0,0,"Level1"};


void setup() {
	Serial.begin(115200);
  	printf_begin();
 	radio.begin();
   	radio.setAutoAck(false);
   	radio.setChannel(112);
    radio.setRetries(3,5); // delay, count
  	//radio.setPALevel(RF24_PA_MIN);
  	radio.setDataRate(RF24_250KBPS);
  	uno.deviceIndex.addDevice(temperature);
  	uno.deviceIndex.addDevice(humidity);
  	uno.deviceIndex.addDevice(level);
  	//uno.setNewSlave(newSlaveConnected);
  	//uno.setNewDevice(newDeviceConnected);
  	
	uno.setup();
}

void loop() {
    static unsigned long t=millis();

	uno.execute();

    if(millis()-t>60000) {
    	float x=rand()*1000;
    	Serial.print("temp "); Serial.println(x);
    	temperature.setAnalogFloat(x);
    	x=rand()*1000;
    	Serial.print("humidity "); Serial.println(x);
    	humidity.setAnalogFloat(x);
    	int16_t l=rand()*256;
    	level.setAnalogInt16(l);
    	Serial.print("level "); Serial.println(l);
    	t=millis();
    }
}
