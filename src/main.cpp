/*
  This script demonstrates MQTT connection over secure websockets (WSS)
  It is designed for the Raspberry Pi Pico W

  Seth McNeill
  2026 October 11
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h> // Library: WebSockets by Links2004
#include <PubSubClient.h>     // Library: PubSubClient by Nick O'Leary
#include <ArduinoJson.h>      // JSON processing library
#include <time.h> // Required for time functions
#include "secrets.h"

const char* ssid = SSID;
const char* password = WIFI_PWD;
const int wss_port = 443;
X509List cert(root_ca);

// Instantiating our networking stack layers
WiFiClientSecure sslClient;
WebSocketsClient webSocket;

// --- CUSTOM STREAM WRAPPER FOR PUBSUBCLIENT ---
class WebSocketClientStream : public Client {
private:
    WebSocketsClient* _ws;
    uint8_t _rxBuffer[1024]; // Expanded to a stable 1KB buffer array
    size_t _rxLength = 0;
    size_t _rxIndex = 0;

public:
    WebSocketClientStream(WebSocketsClient& ws) : _ws(&ws) {}

    void injectData(uint8_t* payload, size_t length) {
        // Prevent buffer overflow
        if (_rxLength + length <= sizeof(_rxBuffer)) {
            memcpy(_rxBuffer + _rxLength, payload, length);
            _rxLength += length;
        } else {
          Serial.println("[Stream Buffer] Warning: Rx Buffer Overflow!");
        }
    }

    // --- REQUIRED WRITE VIRTUALS ---
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buf, size_t size) override {
        if (_ws->isConnected()) {
            _ws->sendBIN(buf, size); 
            _ws->loop();  // keep WS layer alive during transmissions
            return size;
        }
        return 0;
    }

    // --- REQUIRED READ & OBSERVATION VIRTUALS ---
    int available() override { 
        if (_rxLength - _rxIndex == 0) {
            _ws->loop(); // Poke the WS engine to fetch pending data if buffer is empty
        }
        return _rxLength - _rxIndex; 
    }
    
    int read() override {
        if (available() <= 0) return -1;
        int ret = _rxBuffer[_rxIndex++];
        if (_rxIndex >= _rxLength) { _rxIndex = 0; _rxLength = 0; } 
        return ret;
    }

    // FIX: Added the missing multi-byte read pure virtual implementation
    int read(uint8_t *buf, size_t size) override {
        if (available() <= 0) return -1;
        size_t toRead = min(size, (size_t)available());
        memcpy(buf, _rxBuffer + _rxIndex, toRead);
        _rxIndex += toRead;
        if (_rxIndex >= _rxLength) { _rxIndex = 0; _rxLength = 0; }
        return toRead;
    }

    int peek() override { return (available() > 0) ? _rxBuffer[_rxIndex] : -1; }
    void flush() override {}
    void stop() override { _ws->disconnect(); }
    uint8_t connected() override { return _ws->isConnected(); }
    operator bool() override { return _ws->isConnected(); }
    
    // --- REQUIRED CONNECTION VIRTUALS ---
    int connect(IPAddress ip, uint16_t port) override { return 1; }
    int connect(const char *host, uint16_t port) override { return 1; }
};

WebSocketClientStream wsStream(webSocket);
PubSubClient mqttClient(wsStream); // Bind PubSubClient strictly to our WebSocket wrapper

unsigned long lastMqttRetry = 0;

void syncClock() {
  // Configure NTP servers. 
  // configTime(UTC_Offset_Seconds, Daylight_Savings_Offset_Seconds, Server1, Server2)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync...");
  time_t now = time(nullptr);
  
  // 1672531200 represents Jan 1, 2023. Loop until the epoch time crosses this threshold.
  while (now < 1672531200) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized successfully!");

  // Optional: Print the current synchronized time to the Serial Monitor
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current UTC Time: ");
  Serial.println(asctime(&timeinfo));
}

// WebSocket Event Handler 
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WSS] Disconnected!");
      break;
    case WStype_CONNECTED:
      Serial.printf("[WSS] Connected to url: %s\n", payload);
      break;
    case WStype_TEXT:
    case WStype_BIN:
      // Pass incoming WebSocket data directly to the MQTT parser layer
      // This bridges the underlying connection to PubSubClient
      wsStream.injectData(payload, length);
      break;
  }
}

// MQTT Message Received Callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (unsigned long i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void connectToMqtt() {
  // Try to connect to MQTT broker every 5 seconds if not connected
  if (!mqttClient.connected() && (millis() - lastMqttRetry > 15000)) {
    lastMqttRetry = millis();
    Serial.print("Attempting MQTT connection...");
    
    // Create a unique client ID based on Pico W Mac Address
    String clientId = "PicoW-" + WiFi.macAddress();
    
    // if (mqttClient.connect(clientId.c_str())) {
      if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PWD)) {
      Serial.println("connected to MQTT!");
      mqttClient.subscribe(MQTT_TOPIC);
    } else {
      Serial.printf("failed, rc=%d. Will try again in 15s\n", mqttClient.state());
    }
  }
}


void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Starting wifi https");
  Serial.printf("Connecting to %s", ssid);

  // Disable power saving to force full performance, makes it not hang as long
  // CYW43_NO_POWERSAVE_MODE ensures the radio stays fully awake
  cyw43_wifi_pm(&cyw43_state, CYW43_NO_POWERSAVE_MODE);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("connected!");

  Serial.println("Syncing the clock");
  syncClock();  // have to have a clock to check a certificate

  // 1. Configure the TLS layer
  sslClient.setTrustAnchors(&cert);

  // 2. Configure WebSocket layer to run over our secure client
  webSocket.beginSslWithCA(MQTT_SERVER, wss_port, MQTT_PATH, &cert, "mqtt");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(15000);

  // 3. Inject our custom WebSocket wrapper into PubSubClient
  // PubSubClient requires a custom transport client wrapper if bypassing standard TCP sockets. 
  // For most simple integrations with arduinoWebSockets, we bind the client interface:
  // mqttClient.setClient(sslClient); 
  mqttClient.setServer(MQTT_SERVER, wss_port); // Handled sequentially via the WebSocket handshake
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  static unsigned long lastPublishTime = 0;
  unsigned long curMillis = millis();
  // Keep the WebSocket engine running frame-by-frame
  webSocket.loop();

  // If the secure WebSocket connection is alive, manage the MQTT layer
  if (webSocket.isConnected()) {
    if (!mqttClient.connected()) {
      connectToMqtt();
    }
    mqttClient.loop();

    // 2. Send an MQTT packet over WSS every 10 seconds (Non-blocking)
    if (mqttClient.connected() && ((curMillis - lastPublishTime > STATUS_TIME) || lastPublishTime == 0)) {
      // Create your topic and payload data strings
      const char* topic = MQTT_TOPIC;
      JsonDocument doc;
      doc["ModName"] = MOD_NAME;
      doc["MAC"] = WiFi.macAddress();
      doc["millis"] = curMillis;
      if(lastPublishTime == 0) {
        doc["boot"] = true;
      }

      Serial.printf("Publishing message to %s... ", topic);

      String myJSON;
      serializeJson(doc, myJSON);
      // Execute the publish command
      if (mqttClient.publish(topic, myJSON.c_str())) {
        Serial.println("Success!");
      } else {
        Serial.println("Failed to send frame!");
      }
      lastPublishTime = curMillis;
    }
  }
}
