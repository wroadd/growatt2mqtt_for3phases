// Growatt Solar Inverter to MQTT
// Repo: https://github.com/nygma2004/growatt2mqtt
// author: Csongor Varga, csongor.varga@gmail.com
// 1 Phase, 2 string inverter version such as MIN 3000 TL-XE, MIC 1500 TL-X

// Libraries:
// - FastLED by Daniel Garcia
// - ModbusMaster by Doc Walker
// - ArduinoOTA
// - SoftwareSerial
// Hardware:
// - Wemos D1 mini
// - RS485 to TTL converter: https://www.aliexpress.com/item/1005001621798947.html
// - To power from mains: Hi-Link 5V power supply (https://www.aliexpress.com/item/1005001484531375.html), fuseholder and 1A fuse, and varistor

#include <ESP8266WiFi.h>          // Wifi connection
#include <ESP8266WebServer.h>     // Web server for general HTTP response
#include <PubSubClient.h>         // MQTT support
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include "globals.h"
#include "settings.h"
#include "growattInterface.h"

os_timer_t myTimer;
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(mqtt_server, 1883, 0, espClient);

CRGB leds[NUM_LEDS];
growattIF growattInterface(MAX485_RE_NEG, MAX485_DE, MAX485_RX, MAX485_TX);

struct HASensor {
  const char* key;
  const char* name;
  const char* sourceTopic;
  const char* unit;
  const char* deviceClass;
  const char* stateClass;
  const char* icon;
};

const HASensor haSensors[] = {
  {"status", "Status", "data", "", "", "", "mdi:solar-power"},
  {"solarpower", "Solar power", "data", "W", "power", "measurement", ""},
  {"pv1voltage", "PV1 voltage", "data", "V", "voltage", "measurement", ""},
  {"pv1current", "PV1 current", "data", "A", "current", "measurement", ""},
  {"pv1power", "PV1 power", "data", "W", "power", "measurement", ""},
  {"pv2voltage", "PV2 voltage", "data", "V", "voltage", "measurement", ""},
  {"pv2current", "PV2 current", "data", "A", "current", "measurement", ""},
  {"pv2power", "PV2 power", "data", "W", "power", "measurement", ""},
  {"outputpower", "Output power", "data", "W", "power", "measurement", ""},
  {"gridfrequency", "Grid frequency", "data", "Hz", "frequency", "measurement", ""},
  {"ph1voltage", "Phase 1 voltage", "data", "V", "voltage", "measurement", ""},
  {"ph1current", "Phase 1 current", "data", "A", "current", "measurement", ""},
  {"ph1power", "Phase 1 power", "data", "W", "power", "measurement", ""},
  {"ph2voltage", "Phase 2 voltage", "data", "V", "voltage", "measurement", ""},
  {"ph2current", "Phase 2 current", "data", "A", "current", "measurement", ""},
  {"ph2power", "Phase 2 power", "data", "W", "power", "measurement", ""},
  {"ph3voltage", "Phase 3 voltage", "data", "V", "voltage", "measurement", ""},
  {"ph3current", "Phase 3 current", "data", "A", "current", "measurement", ""},
  {"ph3power", "Phase 3 power", "data", "W", "power", "measurement", ""},
  {"lineph1voltage", "Line 1 voltage", "data", "V", "voltage", "measurement", ""},
  {"lineph2voltage", "Line 2 voltage", "data", "V", "voltage", "measurement", ""},
  {"lineph3voltage", "Line 3 voltage", "data", "V", "voltage", "measurement", ""},
  {"energytoday", "Energy today", "data", "kWh", "energy", "total", ""},
  {"energytotal", "Energy total", "data", "kWh", "energy", "total_increasing", ""},
  {"totalworktime", "Total work time", "data", "h", "duration", "total_increasing", ""},
  {"pv1energytoday", "PV1 energy today", "data", "kWh", "energy", "total", ""},
  {"pv1energytotal", "PV1 energy total", "data", "kWh", "energy", "total_increasing", ""},
  {"pv2energytoday", "PV2 energy today", "data", "kWh", "energy", "total", ""},
  {"pv2energytotal", "PV2 energy total", "data", "kWh", "energy", "total_increasing", ""},
  {"opfullpower", "Full output power", "data", "W", "power", "measurement", ""},
  {"tempinverter", "Inverter temperature", "data", "\\u00b0C", "temperature", "measurement", ""},
  {"tempipm", "IPM temperature", "data", "\\u00b0C", "temperature", "measurement", ""},
  {"tempboost", "Boost temperature", "data", "\\u00b0C", "temperature", "measurement", ""},
  {"ipf", "Input power factor", "data", "", "power_factor", "measurement", ""},
  {"realoppercent", "Real output percent", "data", "%", "", "measurement", "mdi:gauge"},
  {"deratingmode", "Derating mode", "data", "", "", "", "mdi:alert-decagram"},
  {"faultcode", "Fault code", "data", "", "", "", "mdi:alert-circle"},
  {"faultbitcode", "Fault bitcode", "data", "", "", "", "mdi:alert-circle"},
  {"warningbitcode", "Warning bitcode", "data", "", "", "", "mdi:alert"},
  {"rssi", "WiFi RSSI", "status", "dBm", "signal_strength", "measurement", ""},
  {"uptime", "Gateway uptime", "status", "min", "duration", "total_increasing", ""}
};

bool mqttConfigured() {
  return mqtt_server != NULL && mqtt_server[0] != '\0';
}

void appendJsonStringField(char* payload, size_t payloadSize, const char* key, const char* value) {
  if (value == NULL || value[0] == '\0') {
    return;
  }
  size_t used = strlen(payload);
  snprintf(payload + used, payloadSize - used, ",\"%s\":\"%s\"", key, value);
}

void appendDeviceInfo(char* payload, size_t payloadSize) {
  size_t used = strlen(payload);
  snprintf(payload + used, payloadSize - used,
           ",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"Growatt\",\"model\":\"3-phase inverter MQTT gateway\",\"sw_version\":\"%s\"}",
           newclientid, haDeviceName, buildversion);
}

void publishHASensorDiscovery(const HASensor& sensor) {
  char topic[160];
  char payload[900];

  snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config", haDiscoveryPrefix, newclientid, sensor.key);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s/%s\",\"value_template\":\"{{ value_json.%s }}\",\"availability_topic\":\"%s/connection\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
           sensor.name, newclientid, sensor.key, topicRoot, sensor.sourceTopic, sensor.key, topicRoot);
  appendJsonStringField(payload, sizeof(payload), "unit_of_measurement", sensor.unit);
  appendJsonStringField(payload, sizeof(payload), "device_class", sensor.deviceClass);
  appendJsonStringField(payload, sizeof(payload), "state_class", sensor.stateClass);
  appendJsonStringField(payload, sizeof(payload), "icon", sensor.icon);
  appendDeviceInfo(payload, sizeof(payload));
  strncat(payload, "}", sizeof(payload) - strlen(payload) - 1);

  mqtt.publish(topic, payload, true);
}

void publishHASwitchDiscovery() {
  char topic[160];
  char payload[900];

  snprintf(topic, sizeof(topic), "%s/switch/%s/enable/config", haDiscoveryPrefix, newclientid);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"Enable inverter\",\"unique_id\":\"%s_enable\",\"command_topic\":\"%s/write/setEnable\",\"state_topic\":\"%s/settings\",\"value_template\":\"{{ 'ON' if value_json.enable == 1 else 'OFF' }}\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"availability_topic\":\"%s/connection\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
           newclientid, topicRoot, topicRoot, topicRoot);
  appendDeviceInfo(payload, sizeof(payload));
  strncat(payload, "}", sizeof(payload) - strlen(payload) - 1);

  mqtt.publish(topic, payload, true);
}

void publishHANumberDiscovery(const char* key, const char* name, const char* command, const char* valueKey, const char* unit, float minValue, float maxValue, float stepValue) {
  char topic[160];
  char payload[900];

  snprintf(topic, sizeof(topic), "%s/number/%s/%s/config", haDiscoveryPrefix, newclientid, key);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"command_topic\":\"%s/write/%s\",\"state_topic\":\"%s/settings\",\"value_template\":\"{{ value_json.%s }}\",\"unit_of_measurement\":\"%s\",\"min\":%.1f,\"max\":%.1f,\"step\":%.1f,\"mode\":\"box\",\"availability_topic\":\"%s/connection\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
           name, newclientid, key, topicRoot, command, topicRoot, valueKey, unit, minValue, maxValue, stepValue, topicRoot);
  appendDeviceInfo(payload, sizeof(payload));
  strncat(payload, "}", sizeof(payload) - strlen(payload) - 1);

  mqtt.publish(topic, payload, true);
}

void publishHomeAssistantDiscovery() {
  if (!haDiscoveryEnabled || !mqttConfigured()) {
    return;
  }

  for (unsigned int i = 0; i < sizeof(haSensors) / sizeof(haSensors[0]); i++) {
    publishHASensorDiscovery(haSensors[i]);
    delay(10);
  }
  publishHASwitchDiscovery();
  delay(10);
  publishHANumberDiscovery("maxoutputactivepp", "Max output", "setMaxOutput", "maxoutputactivepp", "%", 0, 100, 1);
  delay(10);
  publishHANumberDiscovery("startvoltage", "Start voltage", "setStartVoltage", "startvoltage", "V", 0, 1000, 0.1);

  Serial.println(F("Home Assistant MQTT discovery sent"));
}


void ReadInputRegisters() {
  char json[1024];
  char topic[80];

  leds[0] = CRGB::Yellow;
  FastLED.show();
  uint8_t result;

  digitalWrite(STATUS_LED, 0);

  result = growattInterface.ReadInputRegisters(json);
  if (result == growattInterface.Success) {
    leds[0] = CRGB::Green;
    FastLED.show();
    lastRGB = millis();
    ledoff = true;

#ifdef DEBUG_SERIAL
    Serial.println(result);
#endif
    sprintf(topic, "%s/data", topicRoot);
    mqtt.publish(topic, json);
    Serial.println("Data MQTT sent");

  } else if (result != growattInterface.Continue) {
    leds[0] = CRGB::Red;
    FastLED.show();
    lastRGB = millis();
    ledoff = true;

    Serial.print(F("Error: "));
    String message = growattInterface.sendModbusError(result);
    Serial.println(message);
    char topic[80];
    sprintf(topic, "%s/error", topicRoot);
    mqtt.publish(topic, message.c_str());
    delay(5);
  }
  digitalWrite(STATUS_LED, 1);
}

void ReadHoldingRegisters() {
  char json[1024];
  char topic[80];

  leds[0] = CRGB::Yellow;
  FastLED.show();
  uint8_t result;

  digitalWrite(STATUS_LED, 0);
  result = growattInterface.ReadHoldingRegisters(json);
  if (result == growattInterface.Success)   {
    leds[0] = CRGB::Green;
    FastLED.show();
    lastRGB = millis();
    ledoff = true;

#ifdef DEBUG_SERIAL
    Serial.println(json);
#endif
    sprintf(topic, "%s/settings", topicRoot);
    mqtt.publish(topic, json);
    Serial.println("Setting MQTT sent");
    // Set the flag to true not to read the holding registers again
    holdingregisters = true;

  } else if (result != growattInterface.Continue) {
    leds[0] = CRGB::Red;
    FastLED.show();
    lastRGB = millis();
    ledoff = true;

    Serial.print(F("Error: "));
    String message = growattInterface.sendModbusError(result);
    Serial.println(message);
    char topic[80];
    sprintf(topic, "%s/error", topicRoot);
    mqtt.publish(topic, message.c_str());
    delay(5);
  }
  digitalWrite(STATUS_LED, 1);
}

// This is the 1 second timer callback function
void timerCallback(void *pArg) {
  seconds++;

  // Query the modbus device
  if (seconds % UPDATE_MODBUS == 0) {
    //ReadInputRegisters();
    if (!holdingregisters) {
      // Read the holding registers
      ReadHoldingRegisters();
    } else {
      // Read the input registers
      ReadInputRegisters();
    }
  }

  // Send RSSI and uptime status
  if (seconds % UPDATE_STATUS == 0) {
    // Send MQTT update
    if (mqttConfigured()) {
      char topic[80];
      char value[300];
      sprintf(value, "{\"rssi\": %d, \"uptime\": %d, \"ssid\": \"%s\", \"ip\": \"%d.%d.%d.%d\", \"clientid\":\"%s\", \"version\":\"%s\"}", WiFi.RSSI(), uptime, WiFi.SSID().c_str(), WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3], newclientid, buildversion);
      sprintf(topic, "%s/%s", topicRoot, "status");
      mqtt.publish(topic, value);
      Serial.println(F("MQTT status sent"));
    }
  }
}

// MQTT reconnect logic
void reconnect() {
  //String mytopic;
  // Loop until we're reconnected
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    byte mac[6];                     // the MAC address of your Wifi shield
    WiFi.macAddress(mac);
    sprintf(newclientid, "%s-%02x%02x%02x", clientID, mac[2], mac[1], mac[0]);
    Serial.print(F("Client ID: "));
    Serial.println(newclientid);
    // Attempt to connect
    char topic[80];
    sprintf(topic, "%s/%s", topicRoot, "connection");
    if (mqtt.connect(newclientid, mqtt_user, mqtt_password, topic, 1, true, "offline")) { //last will
      Serial.println(F("connected"));
      // ... and resubscribe
      mqtt.publish(topic, "online", true);
      publishHomeAssistantDiscovery();
      sprintf(topic, "%s/write/#", topicRoot);
      mqtt.subscribe(topic);
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(mqtt.state());
      Serial.println(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  FastLED.addLeds<LED_TYPE, RGBLED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalSMD5050 );
  FastLED.setBrightness( BRIGHTNESS );
  leds[0] = CRGB::Pink;
  FastLED.show();

  Serial.begin(SERIAL_RATE);
  Serial.println(F("\nGrowatt Solar Inverter to MQTT Gateway"));
  // Init outputs, RS485 in receive mode
  pinMode(STATUS_LED, OUTPUT);

  // Initialize some variables
  uptime = 0;
  seconds = 0;
  leds[0] = CRGB::Pink;
  FastLED.show();

  // Connect to Wifi
  Serial.print(F("Connecting to Wifi"));
  WiFi.mode(WIFI_STA);

#ifdef FIXEDIP
  // Configures static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
#endif

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    seconds++;
    if (seconds > 180) {
      // reboot the ESP if cannot connect to wifi
      ESP.restart();
    }
  }
  seconds = 0;
  Serial.println("");
  Serial.println(F("Connected to wifi network"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("Signal [RSSI]: "));
  Serial.println(WiFi.RSSI());

  // Set up the Modbus line
  growattInterface.initGrowatt();
  Serial.println("Modbus connection is set up");

  // Create the 1 second timer interrupt
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, 1000, true);

  server.on("/", []() {                       // Dummy page
    server.send(200, "text/plain", "Growatt Solar Inverter to MQTT Gateway");
  });
  server.begin();
  Serial.println(F("HTTP server started"));

  // Set up the MQTT server connection
  if (mqttConfigured()) {
    mqtt.setServer(mqtt_server, 1883);
    mqtt.setBufferSize(2048);
    mqtt.setCallback(callback);
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  byte mac[6];                     // the MAC address of your Wifi shield
  WiFi.macAddress(mac);
  char value[80];
  sprintf(value, "%s-%02x%02x%02x", clientID, mac[2], mac[1], mac[0]);
  ArduinoOTA.setHostname(value);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    os_timer_disarm(&myTimer);
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    os_timer_arm(&myTimer, 1000, true);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  leds[0] = CRGB::Black;
  FastLED.show();
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Convert the incoming byte array to a string

  uint8_t result;
  char messageBuffer[80];
  unsigned int copyLength = length < (sizeof(messageBuffer) - 1) ? length : (sizeof(messageBuffer) - 1);

  for (unsigned int i = 0; i < copyLength; i++) {
    messageBuffer[i] = toupper((unsigned char)payload[i]);
  }
  messageBuffer[copyLength] = '\0';
  String message = messageBuffer;

  char expectedTopic[96];

  sprintf(expectedTopic, "%s/write/getSettings", topicRoot);
  if (strcmp(expectedTopic, topic) == 0) {
    if (strcmp(messageBuffer, "ON") == 0) {
      holdingregisters = false;
    }
  }

  sprintf(expectedTopic, "%s/write/setEnable", topicRoot);
  if (strcmp(expectedTopic, topic) == 0) {
    char json[50];
    char topic[80];

    if (strcmp(messageBuffer, "ON") == 0) {
      growattInterface.writeRegister(growattInterface.regOnOff, 1);
      delay(5);
      sprintf(json, "{ \"enable\":%d}", growattInterface.readRegister(growattInterface.regOnOff));
      sprintf(topic, "%s/settings", topicRoot);
      mqtt.publish(topic, json);
    } else if (strcmp(messageBuffer, "OFF") == 0) {
      growattInterface.writeRegister(growattInterface.regOnOff, 0);
      delay(5);
      sprintf(json, "{ \"enable\":%d}", growattInterface.readRegister(growattInterface.regOnOff));
      sprintf(topic, "%s/settings", topicRoot);
      mqtt.publish(topic, json);
    }
  }

  sprintf(expectedTopic, "%s/write/setMaxOutput", topicRoot);
  if (strcmp(expectedTopic, topic) == 0) {
    char json[50];
    char topic[80];

    result = growattInterface.writeRegister(growattInterface.regMaxOutputActive, message.toInt());
    if (result == growattInterface.Success) {
      holdingregisters = false;
    } else {
      sprintf(json, "last transmission failed with: %s", growattInterface.sendModbusError(result).c_str());
      sprintf(topic, "%s/error", topicRoot);
      mqtt.publish(topic, json);
    }
  }

  sprintf(expectedTopic, "%s/write/setStartVoltage", topicRoot);
  if (strcmp(expectedTopic, topic) == 0) {
    char json[50];
    char topic[80];

    result = growattInterface.writeRegister(growattInterface.regStartVoltage, (uint16_t)(message.toFloat() * 10));  // *10 transmits one digit after decimal place
    if (result == growattInterface.Success) {
      holdingregisters = false;
    } else {
      sprintf(json, "last transmission failed with: %s", growattInterface.sendModbusError(result).c_str());
      sprintf(topic, "%s/error", topicRoot);
      mqtt.publish(topic, json);
    }
  }

#ifdef DEBUG_SERIAL
  Serial.print(F("Message arrived on topic: ["));
  Serial.print(topic);
  Serial.print(F("], "));
  Serial.println(message);
#endif
}

void loop() {
  // Handle HTTP server requests
  server.handleClient();
  ArduinoOTA.handle();

  // Handle MQTT connection/reconnection
  if (mqttConfigured()) {
    if (!mqtt.connected()) {
      reconnect();
    }
    mqtt.loop();
  }

  // Uptime calculation
  if (millis() - lastTick >= 60000) {
    lastTick = millis();
    uptime++;
  }

  if (millis() - lastWifiCheck >= WIFICHECK) {
    // reconnect to the wifi network if connection is lost
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnecting to wifi...");
      WiFi.reconnect();
    }
    lastWifiCheck = millis();
  }

  if (ledoff && (millis() - lastRGB >= RGBSTATUSDELAY)) {
    ledoff = false;
    leds[0] = CRGB::Black;
    FastLED.show();
  }
}
