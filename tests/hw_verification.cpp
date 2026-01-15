#include <Arduino.h>
#include "FlexitProtocol.h"

#define RE_DE_PIN 4 
#define RX_PIN 16
#define TX_PIN 17

FlexitReadings currentData;

// Hjelpefunksjon for å sende kommandoer med logging
void sendTestCommand(const char* beskrivelse, uint8_t reg, uint8_t verdi) {
    Serial.printf("\n[TEST] %s -> Setter register 0x%02X til %d\n", beskrivelse, reg, verdi);
    digitalWrite(RE_DE_PIN, HIGH);
    delay(15);
    uint8_t cmd[] = {0xC3, 0x06, reg, verdi, 0x00}; // 0x06 = Skrive-kommando
    // Enkel checksum
    for(int i=0; i<4; i++) cmd[4] += cmd[i];
    
    Serial2.write(cmd, sizeof(cmd));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);
    delay(2000); // Vent på at CS 50 prosesserer
}

// Hjelpefunksjon for å lese status
bool readFlexitStatus() {
    digitalWrite(RE_DE_PIN, HIGH);
    uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03, 0xC9};
    Serial2.write(poll, sizeof(poll));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);

    uint8_t buf[64];
    int len = Serial2.readBytes(buf, 64);
    if (len > 0) {
        currentData = FlexitProtocol::decode(buf, len);
        return currentData.isValid;
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    pinMode(RE_DE_PIN, OUTPUT);
    Serial.println("\n--- OPPSTARTER AUTOMATISK TEST-SEKVENS (Flexit CS 50) ---");
}

void loop() {
    // STEG 1: Baseline sjekk
    Serial.println("\n>> STEG 1: Leser nåværende status...");
    if (readFlexitStatus()) {
        Serial.printf("LOGG: T1=%0.1f, Vifte=%d, Varme=%s, Driftstimer=%d\n", 
                      currentData.t1, currentData.fanSpeed, 
                      currentData.heaterActive ? "PÅ" : "AV", 
                      currentData.operationalHours);
    } else {
        Serial.println("FEIL: Ingen respons fra aggregat. Sjekk kabling!");
        delay(5000); return;
    }

    // STEG 2: Test av Viftestyring (Borte -> Høy)
    sendTestCommand("Vifter trinn 3 (Forsering)", 0x05, 0x03);
    readFlexitStatus();
    Serial.printf("VERIFISERING: Vifte er nå trinn %d. (Forventet 3)\n", currentData.fanSpeed);

    // STEG 3: Test av Laststyring (Viktig for L1 og elbillader)
    Serial.println("\n>> STEG 3: Verifiserer Laststyring (Heater Lock)...");
    sendTestCommand("Deaktiverer ettervarme (Sperre)", 0x1A, 0x00);
    readFlexitStatus();
    Serial.printf("VERIFISERING: Ettervarme-bit er nå: %s\n", currentData.heaterActive ? "AKTIV (FEIL)" : "DEAKTIVERT (OK)");

    // STEG 4: Nullstilling
    Serial.println("\n>> STEG 4: Setter systemet tilbake til normal drift...");
    sendTestCommand("Aktiverer ettervarme igjen", 0x1A, 0x01);
    sendTestCommand("Vifter trinn 2 (Normal)", 0x05, 0x02);
    
    Serial.println("\n--- TEST-SEKVENS FERDIG. Venter 30 sekunder før ny runde ---");
    delay(30000);
}
