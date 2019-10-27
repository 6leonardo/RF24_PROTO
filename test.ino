#include "proto.h"

RF24 radio(8,9);
Proto Master(radio,"Master",10,5,true);

int sonda1=0;
int sonda2=0;

void newSlaveConnected(Slave& slave) {
	if(slave.compareName("sonda 1"))
		sonda1=slave.radioId;
	if(slave.compareName("sonda 2"))
		sonda2=slave.radioId;
}

void newDeviceConnected(Device& device) {
	
}


void setup() {
	Serial.begin(115200);
  	printf_begin();
 	radio.begin();
   	radio.setAutoAck(false);
   	radio.setChannel(112);
    radio.setRetries(3,5); // delay, count
  	//radio.setPALevel(RF24_PA_MIN);
  	radio.setDataRate(RF24_250KBPS);
  	Master.setNewSlave(newSlaveConnected);
  	Master.setNewDevice(newDeviceConnected);
	Master.setup();
}

void loop() {
    static unsigned long t=millis();

    if(millis()-t>60000) {

    	// put your main code here, to run repeatedly:
    	if(sonda1!=0) {
    		Slave* slave=Master.slaveIndex.getSlave(sonda1);
    		if(slave->status&IsActive) {
                Device* temp=Master.deviceIndex.getDevice("temp",sonda1);
                Serial.println(temp->getAnalogFloat());
                Device* led=Master.deviceIndex.getDevice("led",sonda1);
                led->setDigital(true);
            }
    	}
    }

    if(millis()-t>2*60000) {

    	// put your main code here, to run repeatedly:
    	if(sonda1!=0) {
    		Slave* slave=Master.slaveIndex.getSlave(sonda1);
    		if(slave->status&IsActive) {
                Device* temp=Master.deviceIndex.getDevice("temp",sonda1);
                Serial.println(temp->getAnalogFloat());
                Device* led=Master.deviceIndex.getDevice("led",sonda1);
                led->setDigital(false);
                t=millis();
            }
    	}
    }



}
