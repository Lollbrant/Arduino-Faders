I used a cheap clone of the Arduino Pro Micro, but any Arduino-compatible board with analog inputs should work just fine.

This project uses five faders, but the Arduino Pro Micro only has four analog inputs.\
To work around that, I multiplexed one of the analog pins to handle two faders using two digital I/O pins per fader.\
Only multiplexed faders require these additional control pins.\
Any fader connected directly to a dedicated analog input does not need extra pins—it can be read as-is. You can omit the ".pinA" and ".pinB" definitions for those faders.



#### Here is an example for how you can connect and configure the faders

| Fader | Analog Pin | Pin A | Pin B |
| ----- | ---------- | ----- | ----- |
| 1     | A0         | 2     | 3     |
| 2     | A0         | 4     | 5     |
| 3     | A1         | GND   | VCC   |
| 4     | A2         | GND   | VCC   |
| 5     | A3         | 6     | 7     |
