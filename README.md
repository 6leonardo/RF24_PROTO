# RF24_PROTO
A radio protocol for Arduino, RF24 radio 

Proto is a C ++ class for implementing a protocol of initiation between multiple Arduino through RF24 radios.

An Arduino will have the function of Master while all the others that of Slave.

On each Arduino it will be possible to create different Input / Ouput devices whose values ​​will be replicated on all the other devices.

The data can be sent either at each new value (automatically when the device changes value) or through a polling reading whose polling time can be set for each Slave ...

Moreover it is possible to write on the ouput devices from all the other Arduino present on the net (for now only from the Master).

The protocol is autoconfiguring, so that a new arduino connected to the network can receive an address from the master, to do this the master always listens on a special channel for the configuration of the new Arduino that connect ....

There is also a class for saving the configuration of the slaves and devices in EEPROM.

PHASE: ALPHA TESTING

The code compiles both on Arduino and on ESP32

The code is dependent on library

http://tmrh20.github.io/RF24/ for communication with the radio
