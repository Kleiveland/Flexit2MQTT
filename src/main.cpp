#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "FlexitProtocol.h"

#define RE_DE_PIN 4  // Vanlig på RS485 expansion boards

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
FlexitReadings lastData;

void sendCommand(uint8_t reg, uint8_t val) {
    digitalWrite(RE_DE_PIN, HIGH);
    uint8_t cmd[] = {0xC3, 0x06, reg, val, 0x00}; // Forenklet skriving
    Serial2.write(cmd, sizeof(cmd));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];

    if (String(topic) == "flexit/set/heater") {
        sendCommand(0x1A, (msg == "0") ? 0 : 1); // Sperr eller aktiver
    } else if (String(topic) == "flexit/set/filter_reset") {
        if (msg == "RESET") sendCommand(0x1F, 0x01);
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, 16, 17); // RX=16, TX=17 for expansion board
    pinMode(RE_DE_PIN, OUTPUT);
    
    LittleFS.begin();
    WiFiManager wm;
    wm.autoConnect("Flexit2MQTT_Setup");

    mqttClient.setServer("192.168.1.100", 1883); // BYTT TIL DIN HA IP
    mqttClient.setCallback(mqttCallback);

    server.on("/api/data", []() {
        StaticJsonDocument<512> doc;
        doc["t1"] = lastData.t1; doc["t2"] = lastData.t2;
        doc["t3"] = lastData.t3; doc["t4"] = lastData.t4;
        doc["heat"] = lastData.heaterActive ? "AKTIV" : "AV";
        doc["filter"] = lastData.filterAlarm ? "ALARM" : "OK";
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
    if (!mqttClient.connected()) mqttClient.connect("FlexitBridge");
    mqttClient.loop();

    static unsigned long lastRead = 0;
    if (millis() - lastRead > 5000) {
        lastRead = millis();
        digitalWrite(RE_DE_PIN, HIGH);
        uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03};
        Serial2.write(poll, sizeof(poll));
        Serial2.flush();
        digitalWrite(RE_DE_PIN, LOW);

        uint8_t buf[64];
        int len = Serial2.readBytes(buf, 64);
        if (len > 0) lastData = FlexitProtocol::decode(buf, len);
    }
}
