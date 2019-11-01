//ESP32
#include "proto.h"

RF24 radio(22, 21);
Proto Master(radio, "Master", 10, 5, true);

int sonda1 = 0;
int sonda2 = 0;
bool newTemp1(Device &temp1)
{
	debug(F("New Temp1: "));
	debugn(temp1.getAnalogFloat());
	return true;
}
void newSlaveConnected(Slave &slave)
{
	debug(F("newslave "));
	debugn(slave.name);
	if (slave.compareName("Slave1"))
		sonda1 = slave.radioId;
	if (slave.compareName("Slave2"))
		sonda2 = slave.radioId;
}

void newDeviceConnected(Device &device)
{
	debug(F("newdevice "));
	debugn(device.name);
	if (device.radioId == sonda1 && device.compareName("Temp1"))
		device.setNewDataFunc(newTemp1);
}

void setup()
{
	Serial.begin(115200);
	printf_begin();
	SPI.begin();
	radio.begin();
	radio.setAutoAck(false);
	radio.setChannel(112);
	Serial.print("isChipConnected: ");
	Serial.println(radio.isChipConnected());
	radio.setRetries(3, 5); // delay, count
	//radio.setPALevel(RF24_PA_MIN);
	radio.setDataRate(RF24_250KBPS);
	Master.setNewSlave(newSlaveConnected);
	Master.setNewDevice(newDeviceConnected);
	Serial.print("2.");
	radio.txDelay = 128;
	Master.setup();
	radio.printDetails();
	Serial.print("2.");
}

void loop()
{
	static unsigned long t = millis();

	//Master.execute();
	Master.loop();
	if (millis() - t > 60000)
	{

		// put your main code here, to run repeatedly:
		if (sonda1 != 0)
		{
			Slave *slave = Master.slaveIndex.getSlave(sonda1);
			if (slave != NULL)
				if (slave->status & IsActive)
				{
					Device *temp = Master.deviceIndex.getDevice("temp1", sonda1);
					if (temp != NULL)
						Serial.println(temp->getAnalogFloat());
					//Device* led=Master.deviceIndex.getDevice("led",sonda1);
					//led->setDigital(true);
				}
		}
	}

	if (millis() - t > 2 * 60000)
	{

		// put your main code here, to run repeatedly:
		if (sonda1 != 0)
		{
			Slave *slave = Master.slaveIndex.getSlave(sonda1);
			if (slave != NULL)
				if (slave->status & IsActive)
				{
					Device *temp = Master.deviceIndex.getDevice("Temp1", sonda1);
					if (temp != NULL)
						Serial.println(temp->getAnalogFloat());
					/*
                Device* led=Master.deviceIndex.getDevice("led",sonda1);
                led->setDigital(false);
                */
					t = millis();
				}
		}
	}
}
