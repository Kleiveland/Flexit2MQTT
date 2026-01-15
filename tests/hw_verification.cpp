#include <Arduino.h>
#include "FlexitProtocol.h"

// Hardware-konfigurasjon
#define RE_DE_PIN 4
#define RX_PIN    16
#define TX_PIN    17

// Globale variabler for testen
uint8_t rawData[25];
uint8_t cmdBuf[18] = {195, 4, 0, 199, 81, 193, 4, 8, 32, 15, 0, 34, 128, 4, 0, 22, 0, 0};
bool lastHeaterStatus = false;

// --------------------------------------------------------------------------
// 1. FLETCHER CHECKSUM (A/B) - Beregner kontrollsum for CS50
// --------------------------------------------------------------------------
void updateChecksum() {
    int s1 = 0, s2 = 0;
    for (int i = 5; i < 16; i++) {
        s1 += cmdBuf[i];
        s2 += s1;
    }
    cmdBuf[16] = s1 % 256;
    cmdBuf[17] = s2 % 256;
}

// --------------------------------------------------------------------------
// 2. INJEKSJONS-LOGIKK - Venter på luke i trafikken for å sende
// --------------------------------------------------------------------------
bool injectCommand(const char* beskrivelse, uint8_t fan, uint8_t heat) {
    cmdBuf[11] = fan * 17;        // 17, 34, 51
    cmdBuf[12] = heat ? 128 : 0;  // 128 = PÅ, 0 = AV
    updateChecksum();

    Serial.printf("\n[TX] Forsøker: %s (Vifte:%d, Varme:%s)... ", 
                  beskrivelse, fan, heat ? "PÅ" : "AV");
    
    unsigned long start = millis();
    while (millis() - start < 3000) { // 3 sekunder timeout
        if (Serial2.available()) {
            uint8_t b = Serial2.read();
            // Vi ser etter synkroniserings-bytene fra CS50 (195 1)
            if (b == 195) {
                delay(2);
                if (Serial2.peek() == 1) { 
                    // Aggregatet er midt i en sending, vi venter til den er ferdig
                    delay(50); 
                    
                    digitalWrite(RE_DE_PIN, HIGH);
                    delay(5);
                    Serial2.write(cmdBuf, 18);
                    Serial2.flush();
                    digitalWrite(RE_DE_PIN, LOW);
                    
                    Serial.println("SENDT OK!");
                    return true;
                }
            }
        }
    }
    Serial.println("FEILET (Ingen synk/bus busy)");
    return false;
}

// --------------------------------------------------------------------------
// 3. STATUS-AVLESING - Verifiserer resultatet
// --------------------------------------------------------------------------
void verifyStatus() {
    Serial.print("[INFO] Venter på statusoppdatering fra aggregat...");
    delay(2000); // Vent på at aggregatet prosesserer og sender ny status
    
    uint8_t buf[100];
    int len = Serial2.readBytes(buf, 100);
    
    if (len >= 25) {
        // Finn linje 15-signaturen i strømmen
        for (int i = 0; i < len - 20; i++) {
            if (buf[i+15] == 22 && buf[i+13] == 193) {
                 FlexitReadings r = FlexitProtocol::decode(&buf[i+7], 25);
                 if (r.isValid) {
                     Serial.printf("\n >> BEKREFTET: Varme er nå %s, Vifte er trinn %d\n", 
                                   r.heaterActive ? "PÅ" : "AV", r.fanSpeed);
                     lastHeaterStatus = r.heaterActive;
                     return;
                 }
            }
        }
    }
    Serial.println(" Kunne ikke lese status.");
}

// --------------------------------------------------------------------------
// SETUP & LOOP
// --------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial2.setTimeout(500);
    
    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW);

    Serial.println("\n===============================================");
    Serial.println("   FLEXIT SL4R / CS50 FULL MASKINVARETEST");
    Serial.println("   Tester Fletcher Checksum & Bus-Injection");
    Serial.println("===============================================");
    delay(2000);
}

void loop() {
    // TEST 1: LASTSTYRING - SKRU AV VARME
    if (injectCommand("LASTSTYRING (L1-BESKYTTELSE)", 2, 0)) {
        verifyStatus();
    }
    
    delay(10000); // Vent 10 sek før neste steg

    // TEST 2: NORMAL DRIFT - SKRU PÅ VARME
    if (injectCommand("RETUR TIL NORMAL DRIFT", 2, 1)) {
        verifyStatus();
    }

    Serial.println("\n[Sjekkpunkt] Er 'BEKREFTET' lik 'Forsøker' over?");
    Serial.println("Venter 30 sekunder før ny testsyklus...");
    delay(30000);
}
