/* secrets.h
   
  This is a place to store information not in the repository.

  Seth McNeill
  2026 August 17
*/

#define MOD_NAME "MyModule"
#define SSID "ssid"
#define WIFI_PWD "password"
#define MQTT_USER "myuser"
#define MQTT_PWD "password2"
#define MQTT_SERVER "example.com"
#define MQTT_PATH "/mqtt"
#define MQTT_TOPIC "rpicow/test"
#define STATUS_TIME 600000

// Let's Encrypt ISRG Root X1 CA Certificate (Clean, Multi-line Version)
// Clean, exact raw string representation of the Let's Encrypt Root CA
const char* root_ca = R"EOF(-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
more goes here
-----END CERTIFICATE-----)EOF";
