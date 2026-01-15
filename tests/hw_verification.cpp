#include <Arduino.h>
#include "FlexitProtocol.h"

// Hardware pins for ditt DIN-rail expansion board
#define RE_DE_PIN 4 
#define RX_PIN 16
#define TX_PIN 17

FlexitReadings currentData;

void logSeparator() {
    Serial.println("--------------------------------------------------");
}

// Funksjon for å sende rå-kommandoer med checksum-beregning og logg
void sendTestCommand(const char* beskrivelse, uint8_t type, uint8_t reg, uint8_t verdi) {
    uint8_t cmd[] = {0xC3, type, reg, verdi, 0x00};
    
    // Beregn checksum (sum av de 4 første bytes)
    for(int i=0; i<4; i++) cmd[4] += cmd[i];

    Serial.printf("[TX] %s: %02X %02X %02X %02X | CS: %02X\n", 
                  beskrivelse, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);

    digitalWrite(RE_DE_PIN, HIGH);
    delay(15); // Pre-transmission delay for RS485
    Serial2.write(cmd, sizeof(cmd));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);
    
    delay(1500); // Vent på at CS 50 prosesserer kommandoen
}

void setup() {
    Serial.begin(115200);
    // UART2 konfigurasjon (standard for ESP32 RS485)
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW);

    delay(2000);
    logSeparator();
    Serial.println("FLEXIT CS 50 MASKINVARE-VERIFIKASJON v2.0");
    Serial.println("Basert på Vongraven/Broch dekoding");
    logSeparator();
}

void loop() {
    // 1. LESESTATUS (POLLING)
    Serial.println("\n[STEG 1] Polling av sensor-data...");
    digitalWrite(RE_DE_PIN, HIGH);
    uint8_t poll[] = {0xC3, 0x02, 0x01, 0x03, 0xC9}; // Standard forespørsel
    Serial2.write(poll, sizeof(poll));
    Serial2.flush();
    digitalWrite(RE_DE_PIN, LOW);

    uint8_t buf[64];
    int len = Serial2.readBytes(buf, 64);

    if (len > 0) {
        Serial.printf("[RX] Mottok %d bytes fra aggregat.\n", len);
        currentData = FlexitProtocol::decode(buf, len);
        
        if (currentData.isValid) {
            Serial.printf(" >> Tilluft (T1):   %0.1f C\n", currentData.t1);
            Serial.printf(" >> Uteluft (T3):   %0.1f C\n", currentData.t3);
            Serial.printf(" >> Vifte-trinn:    %d\n", currentData.fanSpeed);
            Serial.printf(" >> Ettervarme:     %s\n", currentData.heaterActive ? "AKTIV" : "AV");
            Serial.printf(" >> Driftstimer:    %d t\n", currentData.operationalHours);
        } else {
            Serial.println(" [!] Dekoding feilet (ugyldig startbyte eller lengde)");
        }
    } else {
        Serial.println(" [!] TIMEOUT: Ingen svar fra RS485. Sjekk kabling (A/B) og 24V strøm.");
    }

    // 2. KONTROLL-TEST (LASTSTYRING/HEATER LOCK)
    logSeparator();
    Serial.println("[STEG 2] Tester sperring av ettervarme (Laststyring)");
    sendTestCommand("Sperrer Varme", 0x06, 0x1A, 0x00);
    
    // 3. KONTROLL-TEST (VIFTE)
    Serial.println("[STEG 3] Tester vifte-hastighet");
    sendTestCommand("Setter Vifte Høy (Trinn 3)", 0x06, 0x05, 0x03);

    // 4. NULLSTILLING
    Serial.println("[STEG 4] Returnerer til normal drift");
    sendTestCommand("Opphever sperre", 0x06, 0x1A, 0x01);
    sendTestCommand("Setter Vifte Normal (Trinn 2)", 0x06, 0x05, 0x02);

    Serial.println("\nTEST-SYKLUS FULLFØRT. Venter 60 sekunder...");
    logSeparator();
    delay(60000);
}
