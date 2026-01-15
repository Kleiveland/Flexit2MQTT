#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "FlexitProtocol.h"

#define RE_DE_PIN 4 

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
FlexitReadings lastData;

// Kø-håndtering for kommandoer (Laststyring)
bool pendingHeaterCommand = false;
bool targetHeaterState = true;

void sendFlexitCommand(uint8_t reg, uint8_t val) {
    digitalWrite(RE_DE_PIN, HIGH);
    delay(10);
    uint8_t cmd[] = {0xC3, 0x06, reg, val, 0x00}; 
    // Legg til checksum her hvis nødvendig
    Serial2.write(cmd, sizeof(cmd));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];

    if (String(topic) == "flexit/set/heater_lock") {
        targetHeaterState = (msg == "0") ? false : true;
        pendingHeaterCommand = true; 
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, 16, 17); // DevKitV1 Pins
    pinMode(RE_DE_PIN, OUTPUT);
    
    LittleFS.begin();
    WiFiManager wm;
    wm.autoConnect("Flexit2MQTT_Gate");

    mqttClient.setServer("YOUR_HA_IP", 1883); // Sett din IP her
    mqttClient.setCallback(mqttCallback);

    server.on("/api/data", []() {
        StaticJsonDocument<512> doc;
        doc["t1"] = lastData.t1; doc["t2"] = lastData.t2;
        doc["t3"] = lastData.t3; doc["t4"] = lastData.t4;
        doc["heat"] = lastData.heaterActive ? "PÅ" : "AV";
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
        if (mqttClient.connect("FlexitBridge")) {
            mqttClient.subscribe("flexit/set/#");
        }
    }
    mqttClient.loop();

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 5000) {
        lastUpdate = millis();
        
        // Hvis vi har en ventende kommando fra HA (Laststyring), gjør den først
        if (pendingHeaterCommand) {
            sendFlexitCommand(0x1A, targetHeaterState ? 0x01 : 0x00);
            pendingHeaterCommand = false;
            delay(100);
        }

        // Polling av data
        digitalWrite(RE_DE_PIN, HIGH);
        uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03};
        Serial2.write(poll, sizeof(poll));
        Serial2.flush();
        digitalWrite(RE_DE_PIN, LOW);

        uint8_t buf[64];
        int len = Serial2.readBytes(buf, 64);
        if (len > 0) lastData = FlexitProtocol::decode(buf, len);
        
        // Publiser til MQTT
        StaticJsonDocument<256> mqttDoc;
        mqttDoc["supply_temp"] = lastData.t1;
        mqttDoc["heater_active"] = lastData.heaterActive;
        char buffer[256];
        serializeJson(mqttDoc, buffer);
        mqttClient.publish("flexit/status", buffer);
    }
}
