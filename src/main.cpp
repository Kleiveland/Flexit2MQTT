#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include "FlexitProtocol.h"

// Oppdatert for ES30485 / DevKitV1
#define RE_DE_PIN 17 
#define RX_PIN 16
#define TX_PIN 4  // Noen brett bruker 4 for DI, sjekk kabling hvis Serial2 svikter

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
FlexitReadings lastData;

struct Config {
    char mqtt_host[64];
    int mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char mqtt_topic[32];
    bool ha_discovery;
} sysConfig;

bool heaterLocked = false;

void loadConfig() {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        StaticJsonDocument<1024> doc;
        deserializeJson(doc, file);
        strlcpy(sysConfig.mqtt_host, doc["mqtt_host"] | "", sizeof(sysConfig.mqtt_host));
        sysConfig.mqtt_port = doc["mqtt_port"] | 1883;
        strlcpy(sysConfig.mqtt_user, doc["mqtt_user"] | "ha_mqtt", sizeof(sysConfig.mqtt_user));
        strlcpy(sysConfig.mqtt_pass, doc["mqtt_pass"] | "ha_mqtt", sizeof(sysConfig.mqtt_pass));
        strlcpy(sysConfig.mqtt_topic, doc["mqtt_topic"] | "flexit", sizeof(sysConfig.mqtt_topic));
        sysConfig.ha_discovery = doc["ha_discovery"] | true;
        file.close();
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    if (String(topic).endsWith("/set/heater_lock")) {
        heaterLocked = (msg == "1" || msg == "ON");
        Serial.printf("LOGG: Heater Lock satt til %s\n", heaterLocked ? "AKTIV" : "AV");
    }
}

void setup() {
    Serial.begin(115200);
    // Flexit SL4R bruker 9600 baud
    Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW);
    
    if(!LittleFS.begin(true)) Serial.println("LittleFS Error");
    loadConfig();

    WiFiManager wm;
    wm.autoConnect("Flexit-Smart-Gate");

    mqttClient.setServer(sysConfig.mqtt_host, sysConfig.mqtt_port);
    mqttClient.setCallback(mqttCallback);

    // API: Leverer data til index.html
    server.on("/api/data", []() {
        StaticJsonDocument<512> doc;
        doc["t1"] = lastData.t1;
        doc["t3"] = lastData.t3;
        doc["heat"] = lastData.heaterActive ? "AKTIV" : "AV";
        doc["locked"] = heaterLocked;
        doc["hours"] = lastData.operationalHours;
        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = millis() / 60000;
        String json; serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.serveStatic("/", LittleFS, "/index.html");
    server.begin();
}

void loop() {
    server.handleClient();
    
    if (!mqttClient.connected()) {
        if (mqttClient.connect("FlexitBridge", sysConfig.mqtt_user, sysConfig.mqtt_pass)) {
            mqttClient.subscribe((String(sysConfig.mqtt_topic) + "/set/#").c_str());
        }
    }
    mqttClient.loop();

    static unsigned long lastPolled = 0;
    if (millis() - lastPolled > 5000) {
        lastPolled = millis();
        
        // RS485 Poll
        digitalWrite(RE_DE_PIN, HIGH);
        delay(5);
        uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03};
        Serial2.write(poll, sizeof(poll));
        Serial2.flush();
        digitalWrite(RE_DE_PIN, LOW);

        uint8_t buf[64];
        int len = Serial2.readBytes(buf, 64);
        if (len > 0) {
            lastData = FlexitProtocol::decode(buf, len);
            // Hvis lader er aktiv, tving heaterActive til false i vår status
            if (heaterLocked) lastData.heaterActive = false; 

            // Publisere til MQTT
            StaticJsonDocument<256> mDoc;
            mDoc["tilluft"] = lastData.t1;
            mDoc["uteluft"] = lastData.t3;
            mDoc["varme_aktiv"] = lastData.heaterActive;
            mDoc["laststyring_sperre"] = heaterLocked;
            char b[256];
            serializeJson(mDoc, b);
            mqttClient.publish((String(sysConfig.mqtt_topic) + "/status").c_str(), b);
        }
    }
}
