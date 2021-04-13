#include <Arduino.h>
#include <ArduinoJson.h>  // get it from https://arduinojson.org/ or install via Arduino library manager
#include <ArduinoOTA.h>
#include <ESP_DoubleResetDetector.h>  //https://github.com/khoih-prog/ESP_DoubleResetDetector
#include <ESP_WiFiManager.h>  //https://github.com/khoih-prog/ESP_WiFiManager
#include <ESPmDNS.h>
#include <PubSubClient.h>

#define USE_LITTLEFS true
#define USE_SPIFFS false

#if USE_LITTLEFS
// Use LittleFS
#include "FS.h"

// The library will be depreciated after being merged to future major Arduino
// esp32 core release 2.x At that time, just remove this library inclusion
#include <LITTLEFS.h>  // https://github.com/lorol/LITTLEFS

FS* filesystem = &LITTLEFS;
#define FileFS LITTLEFS
#define FS_Name "LittleFS"
#endif

#define DRD_TIMEOUT 10
#define DRD_ADDRESS 0

#define SENSE_PIN 13  // Für Touch: 2, 4, 12, 13, 14, 15, 27
#define CONFIG_PIN 16

DoubleResetDetector* drd;
const int PIN_LED = 2;
bool initialConfig = false;

/* const char* mqtt_user = "LightSwitch2";
const char* mqtt_pwd = "LightSwitch2";
const char* mqtt_server = "10.0.0.200"; */

String id = "1";
String host = "WaterSensor_" + id;
String Topic = "/WaterSensor/" + id + "/";

const char* CONFIG_FILE = "/ConfigMQTT.json";

#define DeviceId "01"

/* #define MQTT_SERVER "mqtt"
#define MQTT_SERVERPORT "1883"  // 1883, or 8883 for SSL
#define MQTT_USERNAME "WaterSensor"
#define MQTT_KEY "WaterSensor" */

#define DeviceId_Label "DeviceId_Label"

#define MQTT_SERVER_Label "MQTT_SERVER_Label"
#define MQTT_SERVERPORT_Label "MQTT_SERVERPORT_Label"
#define MQTT_USERNAME_Label "MQTT_USERNAME_Label"
#define MQTT_KEY_Label "MQTT_KEY_Label"

#define custom_DeviceId_LEN 3

#define custom_MQTT_SERVER_LEN 20
#define custom_MQTT_PORT_LEN 5
#define custom_MQTT_USERNAME_LEN 20
#define custom_MQTT_KEY_LEN 40

char custom_DeviceId[custom_DeviceId_LEN];

char custom_MQTT_SERVER[custom_MQTT_SERVER_LEN];
char custom_MQTT_SERVERPORT[custom_MQTT_PORT_LEN];
char custom_MQTT_USERNAME[custom_MQTT_USERNAME_LEN];
char custom_MQTT_KEY[custom_MQTT_KEY_LEN];

#define FORMAT_FILESYSTEM false

void setId(char newId[]) {
  id = newId + '\0';
  host = "WaterSensor_" + id;
  Topic = "/WaterSensor/" + id + "/";
}

bool readConfigFile() {
  // this opens the config file in read-mode
  File f = FileFS.open(CONFIG_FILE, "r");

  if (!f) {
    Serial.println(F("Config File not found"));
    return false;
  } else {
    // we could open the file
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size + 1]);

    f.readBytes(buf.get(), size);
    f.close();
    // Using dynamic JSON buffer which is not the recommended memory model, but
    // anyway See https://github.com/bblanchon/ArduinoJson/wiki/Memory%20model

    DynamicJsonDocument json(1024);
    auto deserializeError = deserializeJson(json, buf.get());

    if (deserializeError) {
      Serial.println(F("JSON parseObject() failed"));
      return false;
    }

    serializeJson(json, Serial);
    if (json.containsKey(DeviceId_Label)) {
      strcpy(custom_DeviceId, json[DeviceId_Label]);
    }
    if (json.containsKey(MQTT_SERVER_Label)) {
      strcpy(custom_MQTT_SERVER, json[MQTT_SERVER_Label]);
    }
    if (json.containsKey(MQTT_SERVERPORT_Label)) {
      strcpy(custom_MQTT_SERVERPORT, json[MQTT_SERVERPORT_Label]);
    }
    if (json.containsKey(MQTT_USERNAME_Label)) {
      strcpy(custom_MQTT_USERNAME, json[MQTT_USERNAME_Label]);
    }
    if (json.containsKey(MQTT_KEY_Label)) {
      strcpy(custom_MQTT_KEY, json[MQTT_KEY_Label]);
    }
  }

  Serial.println(F("\nConfig File successfully parsed"));

  return true;
}

bool writeConfigFile() {
  Serial.println(F("Saving Config File"));

  DynamicJsonDocument json(1024);

  // JSONify local configuration parameters
  json[DeviceId_Label] = custom_DeviceId;
  json[MQTT_SERVER_Label] = custom_MQTT_SERVER;
  json[MQTT_SERVERPORT_Label] = custom_MQTT_SERVERPORT;
  json[MQTT_USERNAME_Label] = custom_MQTT_USERNAME;
  json[MQTT_KEY_Label] = custom_MQTT_KEY;

  File f = FileFS.open(CONFIG_FILE, "w");

  if (!f) {
    Serial.println(F("Failed to open Config File for writing"));
    return false;
  }
  serializeJsonPretty(json, Serial);
  serializeJson(json, f);
  f.close();

  Serial.println(F("\nConfig File successfully saved"));
  return true;
}

WiFiClient espClient;
PubSubClient client(espClient);

long mill, mqttConnectMillis, touchReadtMillis;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String msg = "";
  for (uint16_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    msg += (char)payload[i];
  }
  Serial.println();

  /* String topicStr = String(topic);

  if (topicStr.startsWith(LightSwitchTopic + "0") ||
      topicStr.startsWith(LightSwitchTopic + "1")) {
    Serial.println(topicStr.charAt(topicStr.length() - 1));

    SwitchRelay(topicStr.charAt(topicStr.length() - 1) - '0',
                (char)payload[0] == '1');
  } */
}

char* str2ch(String command) {
  if (command.length() != 0) {
    char* p = const_cast<char*>(command.c_str());
    return p;
  }
  return const_cast<char*>("");
}

void reconnect() {
  // Loop until we're reconnected
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = host;
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), custom_MQTT_USERNAME, custom_MQTT_KEY,
                       str2ch(Topic + "Status"), 0, true, "OFFLINE")) {
      Serial.println("connected");

      // client.subscribe(str2ch(LightSwitchTopic + "#"));

      Serial.println("Publishing IP: " + WiFi.localIP().toString());
      client.publish(str2ch(Topic + "IP"), str2ch(WiFi.localIP().toString()),
                     true);

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
    }
  }
}

// void IRAM_ATTR ISR() { wifiManager.startConfigPortal(); }

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);

  Serial.println("Start Setup");

  if (FORMAT_FILESYSTEM) {
    Serial.println(F("Forced Formatting."));
    FileFS.format();
  }

  if (!FileFS.begin(true)) {
    Serial.print(FS_Name);
    Serial.println(F(" failed! AutoFormatting."));
  }

  Serial.println("after SPIFFS.begin");

  ESP_WiFiManager wifiManager("WaterSensor");


  if (!readConfigFile()) {
    Serial.println(F("Can't read Config File, using default values"));
  }

  setId(custom_DeviceId);

  ESP_WMParameter DeviceId_FIELD(DeviceId_Label, "Device Id", custom_DeviceId,
                                 custom_DeviceId_LEN + 1);

  ESP_WMParameter MQTT_SERVER_FIELD(MQTT_SERVER_Label, "MQTT SERVER",
                                    custom_MQTT_SERVER,
                                    custom_MQTT_SERVER_LEN + 1);

  ESP_WMParameter MQTT_SERVERPORT_FIELD(
      MQTT_SERVERPORT_Label, "MQTT SERVER PORT", custom_MQTT_SERVERPORT,
      custom_MQTT_PORT_LEN + 1);

  ESP_WMParameter MQTT_USERNAME_FIELD(MQTT_USERNAME_Label, "MQTT USERNAME",
                                      custom_MQTT_USERNAME,
                                      custom_MQTT_USERNAME_LEN + 1);

  ESP_WMParameter MQTT_KEY_FIELD(MQTT_KEY_Label, "MQTT PASSWORD",
                                 custom_MQTT_KEY, custom_MQTT_KEY_LEN + 1);

  wifiManager.addParameter(&DeviceId_FIELD);
  wifiManager.addParameter(&MQTT_SERVER_FIELD);
  wifiManager.addParameter(&MQTT_SERVERPORT_FIELD);
  wifiManager.addParameter(&MQTT_USERNAME_FIELD);
  wifiManager.addParameter(&MQTT_KEY_FIELD);

  

  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  if (drd->detectDoubleReset()) {
    Serial.println(F("DRD"));
    initialConfig = true;
  }
  if (wifiManager.WiFi_SSID() == "") {
    Serial.println(F("No AP credentials"));
    initialConfig = true;
  }
  if (initialConfig) {
    Serial.println(F("Starting Config Portal"));
    digitalWrite(PIN_LED, HIGH);
    if (!wifiManager.startConfigPortal()) {
      Serial.println(F("Not connected to WiFi"));
    } else {
      Serial.println(F("connected"));
      strcpy(custom_DeviceId, DeviceId_FIELD.getValue());
      strcpy(custom_MQTT_SERVER, MQTT_SERVER_FIELD.getValue());
      strcpy(custom_MQTT_SERVERPORT, MQTT_SERVERPORT_FIELD.getValue());
      strcpy(custom_MQTT_USERNAME, MQTT_USERNAME_FIELD.getValue());
      strcpy(custom_MQTT_KEY, MQTT_KEY_FIELD.getValue());

      // Writing JSON config file to flash for next boot
      writeConfigFile();
    }
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin();
  }

  wifiManager.setDebugOutput(true);
  wifiManager.setConfigPortalChannel(0);

  wifiManager.autoConnect("WaterSensor");

  client.setServer(custom_MQTT_SERVER, atoi(custom_MQTT_SERVERPORT));
  client.setCallback(callback);

  Serial.println(client.state());

  MDNS.begin("WaterSensor");
  MDNS.addService("http", "tcp", 80);

  Serial.println("EndSetup");
}

uint8_t touch_value = 100;

void loop() {
  // put your main code here, to run repeatedly:
  drd->loop();
  ArduinoOTA.handle();
  client.loop();

  if ((millis() - touchReadtMillis) > 500) {
    touch_value = touchRead(SENSE_PIN);
    Serial.println(touch_value);    
    client.publish(str2ch(Topic + "Water"), (touch_value < 2)?"1":"0", true);
    touchReadtMillis = millis();
  }

  if ((millis() - mqttConnectMillis) > 5000) {
    reconnect();
    mqttConnectMillis = millis();
  }

  client.loop();

  if ((millis() - mill) > 30000) {
    client.publish(str2ch(Topic + "Status"), str2ch("ONLINE"), true);
    mill = millis();
  }
}