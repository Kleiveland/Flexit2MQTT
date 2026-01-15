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

// Innstillinger som lagres i LittleFS
struct Config {
    char mqtt_host[64];
    int mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
} config;

// Laster innstillinger fra config.json
void loadConfig() {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        StaticJsonDocument<512> doc;
        deserializeJson(doc, file);
        strlcpy(config.mqtt_host, doc["mqtt_host"] | "", sizeof(config.mqtt_host));
        config.mqtt_port = doc["mqtt_port"] | 1883;
        file.close();
    }
}

// Lagrer innstillinger til config.json
void saveConfig() {
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<512> doc;
    doc["mqtt_host"] = config.mqtt_host;
    doc["mqtt_port"] = config.mqtt_port;
    serializeJson(doc, file);
    file.close();
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, 16, 17);
    pinMode(RE_DE_PIN, OUTPUT);
    
    if(!LittleFS.begin(true)) Serial.println("LittleFS Mount Failed");
    
    loadConfig();

    WiFiManager wm;
    wm.autoConnect("Flexit2MQTT_Setup");

    if (strlen(config.mqtt_host) > 0) {
        mqttClient.setServer(config.mqtt_host, config.mqtt_port);
    }

    // API for å hente nåværende config til HTML
    server.on("/api/get_config", []() {
        StaticJsonDocument<256> doc;
        doc["mqtt_host"] = config.mqtt_host;
        doc["mqtt_port"] = config.mqtt_port;
        String json; serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    // API for å lagre ny config
    server.on("/api/save_config", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            StaticJsonDocument<256> doc;
            deserializeJson(doc, server.arg("plain"));
            strlcpy(config.mqtt_host, doc["mqtt_host"], sizeof(config.mqtt_host));
            config.mqtt_port = doc["mqtt_port"];
            saveConfig();
            server.send(200, "text/plain", "OK");
            delay(1000); ESP.restart(); // Restarter for å koble til ny broker
        }
    });

    server.on("/api/data", handleApiData); // (Samme som før)
    server.serveStatic("/", LittleFS, "/index.html");
    server.begin();
}

void loop() {
    server.handleClient();
    if (strlen(config.mqtt_host) > 0) {
        if (!mqttClient.connected()) {
            mqttClient.connect("FlexitBridge");
        }
        mqttClient.loop();
    }
    // RS485 polling (Samme som før)
}
