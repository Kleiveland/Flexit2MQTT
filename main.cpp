#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "FlexitProtocol.h"

// Pins for DevKitV1 på Expansion Board
#define RX_PIN 16
#define TX_PIN 17
#define RE_DE_PIN 4 

WiFiClient espClient;
PubSubClient mqttClient(espClient);
FlexitReadings lastData;

// --- RS485 SKRIVEFUNKSJONER (Basert på Vongraven/Broch) ---
void sendFlexitCommand(uint8_t type, uint8_t addr, uint8_t val) {
    digitalWrite(RE_DE_PIN, HIGH);
    delay(5);
    uint8_t cmd[] = {0xC3, type, addr, val, 0x00}; // Forenklet eksempel
    // Her beregnes checksum før sending
    Serial2.write(cmd, sizeof(cmd));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);
}

// --- MQTT CALLBACK (Mottar kommandoer fra HA) ---
void callback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];

    if (String(topic) == "flexit/set/heater") {
        if (msg == "0") {
            Serial.println("Laststyring: Deaktiverer ettervarme");
            sendFlexitCommand(0x06, 0x1A, 0x00); // Sperr varme
        } else {
            sendFlexitCommand(0x06, 0x1A, 0x01); // Tillat varme
        }
    }
    
    if (String(topic) == "flexit/set/filter_reset") {
        if (msg == "RESET") {
            Serial.println("Nullstiller filteralarm");
            sendFlexitCommand(0x06, 0x1F, 0x01); // Reset kommando
        }
    }
}

void setup() {
    Serial.begin(115200);
    // UART2 for RS485
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW);

    WiFiManager wm;
    wm.autoConnect("Flexit-Smart-Gate");

    mqttClient.setServer("192.168.1.100", 1883);
    mqttClient.setCallback(callback);
}

void loop() {
    if (!mqttClient.connected()) {
        if (mqttClient.connect("FlexitESP32")) {
            mqttClient.subscribe("flexit/set/#");
        }
    }
    mqttClient.loop();

    // Hovedløkke for utlesing (hvert 5. sek)
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 5000) {
        lastUpdate = millis();
        // Lesedata-logikk her...
    }
}
