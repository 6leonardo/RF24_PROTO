// arduino uno
#include "proto.h"
#include "printf.h"
//#undef asser(x)
//#define assert(x) Serial.print(#x); Serial.println(x); //assert(x)
RF24 radio(8, 9);
Proto uno{radio, "Slave1", 10, 5, false};
bool CommandLed1(Device& led) {
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
  radio.setAutoAck(false);
  radio.setChannel(112);
  Serial.print("isChipConnected: ");
  Serial.println(radio.isChipConnected());
  radio.setRetries(3, 5); // delay, count
  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  Serial.print("2.");
  uno.deviceIndex.addDevice(temperature);
  Serial.print("3.");
  uno.deviceIndex.addDevice(humidity);
  Serial.print("4.");
  uno.deviceIndex.addDevice(level);
  Serial.print("5.");
  //uno.setNewSlave(newSlaveConnected);
  //uno.setNewDevice(newDeviceConnected);
  //radio.printDetails();
  radio.txDelay=128;
  uno.setup();
  radio.printDetails();
  Serial.print("6.");
}

void loop()
{
  static unsigned long t = millis();
  static bool first = true;
  //Serial.print("7.");
  //uno.execute();
  //Serial.println("8.");
  if (millis() - t > 60000 || first)
  {
    Serial.println("set");
    first = false;
    float x = rand() * 1000;
    Serial.print("temp ");
    Serial.println(x);
    temperature.setAnalogFloat(x);
    x = rand() * 1000;
    Serial.print("humidity ");
    Serial.println(x);
    humidity.setAnalogFloat(x);
    int16_t l = rand() * 256;
    Serial.print("level ");
    Serial.println(l);
    level.setAnalogInt16(l);
    t = millis();
  }
  uno.loop();

}
