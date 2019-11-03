Example1

This example has been tested using an Arduino Uno and an ESP32 Master as a Slave.

On the Slave there are 3 Inputs (Temp1, Hum1, Level1) and an Output (Led1).

Which are replicated on the Master.

Automatically the inputs are sent to the Master at each change of value, in this case the master configures two instead to be read in polling while the Temp1 is left as default in order to be sent at each new value ...

The master controls Led1 ...

There are also callbacks just to see when they are called ...
