# MQTT Over WSS on a Raspberry Pi Pico W
This uses secure web sockets that checks the certificate of the 
destination to connect to an MQTT server at `https://MQTT_SERVER/MQTT_PATH` (note that the `/` between server and path needs to be included in the path)

Note that you do need to create a `secrets.h` file modeled after 
`secretsExample.h`. 