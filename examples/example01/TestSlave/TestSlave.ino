// arduino uno
#include "proto.h"
#include "printf.h"
//#undef asser(x)
//#define assert(x) Serial.print(#x); Serial.println(x); //assert(x)
RF24 radio(8, 9);
Proto uno{radio, "Slave1", 10, 5, false};
bool CommandLed1(Device &led)
{
	Serial.print("led1 now is: ");
	Serial.println(led.getDigital());
	return true;
}
Device temperature{Input, AnalogFloat, 1, 0, "Temp1"};
Device humidity{Input, AnalogFloat, 2, 0, "Hum1"};
Device level{Input, AnalogInt16, 3, 0, "Level1"};
Device led{Output, Digital, 4, 0, "Led1", CommandLed1};

void setup()
{
	Serial.begin(115200);
	printf_begin();
	SPI.begin();
	Serial.print("1.");
	radio.begin();
	radio.setAutoAck(true);
	radio.setChannel(112);
	Serial.print("isChipConnected: ");
	Serial.println(radio.isChipConnected());
	radio.setRetries(5, 10); // delay, count
	radio.setPALevel(RF24_PA_MIN);
	radio.setDataRate(RF24_250KBPS);
	uno.deviceIndex.addDevice(temperature);
	uno.deviceIndex.addDevice(humidity);
	uno.deviceIndex.addDevice(level);
	uno.deviceIndex.addDevice(led);
	//uno.setNewSlave(newSlaveConnected);
	//uno.setNewDevice(newDeviceConnected);
	//radio.printDetails();
	radio.txDelay = 128;
	uno.setup();
	radio.printDetails();
}

void loop()
{
	static unsigned long t1 = millis();
	static unsigned long t2 = millis();
	static bool first = true;
	//Serial.print("7.");
	//uno.execute();
	//Serial.println("8.");
	if (millis() - t1 > 60000 || first)
	{
		float x = ((unsigned int)rand()) % 200 / 10.0;
		Serial.print("temp ");
		Serial.println(x);
		temperature.setAnalogFloat(x);
		t1 = millis();
	}
	if (millis() - t2 > 5000 || first)
	{
		first = false;
		float x = ((unsigned int)rand()) % 200 / 10.0;
		Serial.print("humidity ");
		Serial.println(x);
		humidity.setAnalogFloat(x);
		int16_t l = ((unsigned int)rand()) % 100;
		Serial.print("level ");
		Serial.println(l);
		level.setAnalogInt16(l);
		t2 = millis();
	}
	uno.loop();
}
