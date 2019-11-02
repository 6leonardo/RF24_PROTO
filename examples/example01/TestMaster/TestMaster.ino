//ESP32
#include "proto.h"

RF24 radio(22, 21);
Proto Master(radio, "Master", 10, 5, true);

int sonda1 = 0;
int sonda2 = 0;
bool newTemp1(Device &temp1)
{
	Serial.print(F("New Temp1: "));
	Serial.println(temp1.getAnalogFloat());
	Serial.flush();
	return true;
}
bool newHum1(Device &hum1)
{
	Serial.print(F("New Hum1: "));
	Serial.println(hum1.getAnalogFloat());
	Serial.flush();
	return true;
}
bool newLevel1(Device &lev1)
{
	Serial.print(F("New Level1: "));
	Serial.println(lev1.getAnalogInt16());
	Serial.flush();
	return true;
}

void newSlaveConnecting(Slave &slave)
{
	Serial.print(F("newslave "));
	Serial.println(slave.name);
	if (slave.compareName("Slave1"))
		sonda1 = slave.radioId;
	if (slave.compareName("Slave2"))
		sonda2 = slave.radioId;
	Serial.flush();
}

void newSlaveConnected(Slave &slave)
{
	Serial.println("slave connected");
	if (slave.compareName("Slave1"))
	{
		Device *d[2];
		d[0] = Master.deviceIndex.getDevice(2, slave.radioId);
		d[1] = Master.deviceIndex.getDevice(3, slave.radioId);
		Serial.println("sendonpoll configure");
		if (d[0] != NULL && d[1] != NULL)
		{
			d[0]->setSendOn(SendOnPoll);
			d[1]->setSendOn(SendOnPoll);
			Master.sendSetDevicesFlags(slave.radioId, 2, d);
			Serial.println("sendonpoll configure sent");
		}
	}
}

void newDeviceConnected(Device &device)
{
	Serial.print(F("newdevice "));
	Serial.println(device.name);
	Serial.flush();
	if (device.radioId == sonda1)
	{
		if (device.compareName("Temp1"))
			device.setNewDataFunc(newTemp1);
		if (device.compareName("Hum1"))
			device.setNewDataFunc(newHum1);
		if (device.compareName("Level1"))
			device.setNewDataFunc(newLevel1);
	}
}

void setup()
{
	Serial.begin(115200);
	printf_begin();
	SPI.begin();
	radio.begin();
	radio.setAutoAck(true);
	radio.setChannel(112);
	Serial.print("isChipConnected: ");
	Serial.println(radio.isChipConnected());
	radio.setRetries(3, 5); // delay, count
	//radio.setPALevel(RF24_PA_MIN);
	radio.setDataRate(RF24_250KBPS);
	Master.setNewSlaveConnecting(newSlaveConnecting);
	Master.setNewDevice(newDeviceConnected);
	Master.setNewSlaveConnected(newSlaveConnected);
	Serial.print("2.");
	radio.txDelay = 128;
	Master.setup();
	radio.printDetails();
	Serial.print("2.");
}

void loop()
{
	static unsigned long tempo = millis();
	static int phase = 0;
	//Master.execute();
	Master.loop();
	if ((millis() - tempo) > 60000 && phase == 0)
	{
		// put your main code here, to run repeatedly:
		Slave *slave = NULL;
		if (sonda1 != 0 && (slave = Master.slaveIndex.getSlave(sonda1)) != NULL && slave->flags & IsActive)
		{
			Device *temp = Master.deviceIndex.getDevice("Temp1", sonda1);
			if (temp != NULL)
			{
				Serial.print(F("temp from loop: "));
				Serial.println(temp->getAnalogFloat());
			}
			Serial.println(F("now command led"));
			Device *led = Master.deviceIndex.getDevice("Led1", sonda1);
			if (led != NULL)
				led->setDigital(true);
			Serial.flush();
			phase = 1;
		}
		else
		{
			tempo = millis();
		}
	}

	if ((millis() - tempo) > 120000 && phase == 1)
	{
		Serial.print(F("if 2 "));
		Serial.println(millis() - tempo);
		// put your main code here, to run repeatedly:
		Slave *slave = NULL;
		if (sonda1 != 0 && (slave = Master.slaveIndex.getSlave(sonda1)) != NULL && slave->flags & IsActive)
		{
			Device *temp = Master.deviceIndex.getDevice("Temp1", sonda1);
			if (temp != NULL)
			{
				Serial.print(F("temp from loop: "));
				Serial.println(temp->getAnalogFloat());
			}
			Serial.println(F("now command led"));
			Device *led = Master.deviceIndex.getDevice("Led1", sonda1);
			if (led != NULL)
				led->setDigital(false);

			Serial.flush();
		}
		phase = 0;
		tempo = millis();
	}
}
