#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include "FlexitProtocol.h"

// Hardware pins for DevKitV1 + Expansion Board
#define RE_DE_PIN 4 
#define RX_PIN 16
#define TX_PIN 17

WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
FlexitReadings lastData;

// Global config struktur
struct Config {
    char mqtt_host[64];
    int mqtt_port;
    char mqtt_topic[32];
    bool ha_discovery;
    int temp_setpoint;
} sysConfig;

// Laststyring variabler
bool heaterLocked = false;
bool pendingCommand = false;

void loadConfig() {
    if (LittleFS.exists("/config.json")) {
        File file = LittleFS.open("/config.json", "r");
        StaticJsonDocument<1024> doc;
        deserializeJson(doc, file);
        strlcpy(sysConfig.mqtt_host, doc["mqtt_host"] | "", sizeof(sysConfig.mqtt_host));
        sysConfig.mqtt_port = doc["mqtt_port"] | 1883;
        strlcpy(sysConfig.mqtt_topic, doc["mqtt_topic"] | "flexit", sizeof(sysConfig.mqtt_topic));
        sysConfig.ha_discovery = doc["ha_discovery"] | true;
        
        // Oppsett av norsk tid
        configTime(0, 0, doc["ntp_server"] | "no.pool.ntp.org");
        setenv("TZ", doc["timezone"] | "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        file.close();
    }
}

void saveConfig() {
    File file = LittleFS.open("/config.json", "w");
    StaticJsonDocument<1024> doc;
    doc["mqtt_host"] = sysConfig.mqtt_host;
    doc["mqtt_port"] = sysConfig.mqtt_port;
    doc["mqtt_topic"] = sysConfig.mqtt_topic;
    serializeJson(doc, file);
    file.close();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    
    // Logikk for laststyring (Heater Lock)
    if (String(topic).endsWith("/set/heater_lock")) {
        heaterLocked = (msg == "1" || msg == "ON");
        pendingCommand = true;
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    pinMode(RE_DE_PIN, OUTPUT);
    
    if(!LittleFS.begin(true)) Serial.println("LittleFS Error");
    loadConfig();

    WiFiManager wm;
    wm.autoConnect("Flexit-Smart-Gate");

    if (strlen(sysConfig.mqtt_host) > 0) {
        mqttClient.setServer(sysConfig.mqtt_host, sysConfig.mqtt_port);
        mqttClient.setCallback(mqttCallback);
    }

    // --- API ENDEPUNKTER ---
    server.on("/api/data", []() {
        StaticJsonDocument<512> doc;
        doc["t1"] = lastData.t1; doc["t3"] = lastData.t3;
        doc["heat"] = lastData.heaterActive ? "AKTIV" : "AV";
        doc["locked"] = heaterLocked;
        doc["hours"] = lastData.operationalHours;
        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = millis() / 60000;
        
        struct tm timeinfo;
        char timeStr[20];
        if(getLocalTime(&timeinfo)) strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
        doc["time"] = timeStr;

        String json; serializeJson(doc, json);
        server.send(200, "application/json", json);
    });

    server.on("/api/get_config", []() {
        File file = LittleFS.open("/config.json", "r");
        server.streamFile(file, "application/json");
        file.close();
    });

    server.on("/api/save_config", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            File file = LittleFS.open("/config.json", "w");
            file.print(server.arg("plain"));
            file.close();
            server.send(200, "text/plain", "Lagret");
            delay(500); ESP.restart();
        }
    });

    server.serveStatic("/", LittleFS, "/index.html");
    server.begin();
}

void loop() {
    server.handleClient();
    
    if (strlen(sysConfig.mqtt_host) > 0) {
        if (!mqttClient.connected()) {
            if (mqttClient.connect("FlexitBridge")) {
                String t = String(sysConfig.mqtt_topic) + "/set/#";
                mqttClient.subscribe(t.c_str());
            }
        }
        mqttClient.loop();
    }

    static unsigned long lastPolled = 0;
    if (millis() - lastPolled > 5000) {
        lastPolled = millis();
        
        // RS485 Kommunikasjon
        digitalWrite(RE_DE_PIN, HIGH);
        uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03};
        Serial2.write(poll, sizeof(poll));
        Serial2.flush();
        digitalWrite(RE_DE_PIN, LOW);

        uint8_t buf[64];
        int len = Serial2.readBytes(buf, 64);
        if (len > 0) lastData = FlexitProtocol::decode(buf, len);
        
        // Send til MQTT hvis tilkoblet
        if(mqttClient.connected()) {
            StaticJsonDocument<256> mDoc;
            mDoc["t1"] = lastData.t1;
            mDoc["heater"] = lastData.heaterActive;
            mDoc["locked"] = heaterLocked;
            char b[256];
            serializeJson(mDoc, b);
            String t = String(sysConfig.mqtt_topic) + "/status";
            mqttClient.publish(t.c_str(), b);
        }
    }
}
