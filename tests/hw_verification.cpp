#include <Arduino.h>

// --------------------------------------------------------------------------
// Hardware-konfigurasjon (ESP32)
// --------------------------------------------------------------------------
#define RE_DE_PIN 4
#define RX_PIN    16
#define TX_PIN    17

// --------------------------------------------------------------------------
// Globale buffere og variabler
// --------------------------------------------------------------------------
uint8_t rawData[25];
uint8_t cmdBuf[18] = {195,4,0,199,81,193,4,8,32,15,0,34,128,4,0,22,0,0};

int fanSpeed = 0;
bool preheatOn = false;
bool preheatActive = false;
int heatExchTemp = 0;
uint32_t opHours = 0;

// --------------------------------------------------------------------------
// 1. FLETCHER CHECKSUM
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
// 2. DEKODING AV LINJE 15 (25 bytes)
// --------------------------------------------------------------------------
void decodeLine15(uint8_t* buf) {
    fanSpeed = buf[6] / 17;
    preheatOn = (buf[7] == 128);
    heatExchTemp = buf[10];

    // Preheat active logikk
    if (buf[11] > 10 && preheatOn) preheatActive = true;
    if (buf[12] < 100 && preheatOn) preheatActive = false;

    // Driftstimer (ikke dokumentert, men vi tar med)
    opHours = (uint32_t)((buf[21] << 16) | (buf[22] << 8) | buf[23]);
}

// --------------------------------------------------------------------------
// 3. LES LINJE 15 FRA BUSSEN
// --------------------------------------------------------------------------
bool readLine15() {
    uint8_t buf[1000];
    int idx = 0;
    unsigned long start = millis();

    while (millis() - start < 2000) {
        if (Serial2.available()) {
            buf[idx] = Serial2.read();

            // Match signatur: 195 ... 193 ... 22
            if (idx >= 8 &&
                buf[idx] == 22 &&
                buf[idx - 2] == 193 &&
                buf[idx - 8] == 195) {

                memcpy(rawData, &buf[idx - 24], 25);
                decodeLine15(rawData);
                return true;
            }

            idx++;
            if (idx >= 1000) idx = 0;
        }
    }
    return false;
}

// --------------------------------------------------------------------------
// 4. LOGG STATUS
// --------------------------------------------------------------------------
void printStatus() {
    Serial.println("\n--- SL4R STATUS ---");
    Serial.printf("Fan level:        %d\n", fanSpeed);
    Serial.printf("Preheat on/off:   %s\n", preheatOn ? "ON" : "OFF");
    Serial.printf("Preheat active:   %s\n", preheatActive ? "YES" : "NO");
    Serial.printf("Heat exch temp:   %d C\n", heatExchTemp);
    Serial.printf("Operational hrs:  %lu\n", (unsigned long)opHours);

    Serial.println("Raw bytes:");
    for (int i = 0; i < 25; i++) {
        Serial.printf("%02X ", rawData[i]);
    }
    Serial.println("\n--------------------");
}

// --------------------------------------------------------------------------
// 5. SEND KOMMANDO (timing-basert injeksjon)
// --------------------------------------------------------------------------
void sendCommand(uint8_t fan, uint8_t heat, uint8_t temp, const char* desc) {
    cmdBuf[11] = fan * 17;
    cmdBuf[12] = heat ? 128 : 0;
    cmdBuf[15] = temp;

    updateChecksum();

    Serial.printf("\n[TX] %s (fan=%d, heat=%d, temp=%d)\n",
                  desc, fan, heat, temp);

    unsigned long start = millis();
    while (millis() - start < 3000) {
        if (Serial2.available() && Serial2.read() == 195) {
            while (!Serial2.available());
            if (Serial2.read() == 1) {

                // Hopp over resten av linjen
                for (int i = 0; i < 8; i++) {
                    while (!Serial2.available());
                    Serial2.read();
                }
                while (!Serial2.available());
                uint8_t len = Serial2.read() + 2;
                for (int i = 0; i < len; i++) {
                    while (!Serial2.available());
                    Serial2.read();
                }

                // Send i pausen
                digitalWrite(RE_DE_PIN, HIGH);
                delay(5);
                Serial2.write(cmdBuf, 18);
                Serial2.flush();
                digitalWrite(RE_DE_PIN, LOW);

                Serial.println("SENDT!");
                return;
            }
        }
    }
    Serial.println("FEIL: Fant ikke sendeluke");
}

// --------------------------------------------------------------------------
// 6. FULL TESTSEKVENS
// --------------------------------------------------------------------------
void runFullTest() {
    Serial.println("\n=== FULL TESTSEKVENS STARTER ===");

    // 1. Les status
    if (readLine15()) printStatus();

    // 2. Test vifte
    sendCommand(1, preheatOn, heatExchTemp, "Vifte → 1");
    delay(2000); readLine15(); printStatus();

    sendCommand(2, preheatOn, heatExchTemp, "Vifte → 2");
    delay(2000); readLine15(); printStatus();

    sendCommand(3, preheatOn, heatExchTemp, "Vifte → 3");
    delay(2000); readLine15(); printStatus();

    // 3. Test preheat
    sendCommand(fanSpeed, 1, heatExchTemp, "Preheat → ON");
    delay(3000); readLine15(); printStatus();

    sendCommand(fanSpeed, 0, heatExchTemp, "Preheat → OFF");
    delay(3000); readLine15(); printStatus();

    // 4. Test temperatur 15–25
    for (int t = 15; t <= 25; t++) {
        char msg[32];
        sprintf(msg, "Temp → %d", t);
        sendCommand(fanSpeed, preheatOn, t, msg);
        delay(1500);
        readLine15();
        printStatus();
    }

    Serial.println("=== FULL TESTSEKVENS FERDIG ===");
}

// --------------------------------------------------------------------------
// SETUP & LOOP
// --------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);

    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW);

    Serial.println("\nFLEXIT SL4R / CS50 – FULLSTENDIG TESTPROGRAM");
    delay(2000);
}

void loop() {
    runFullTest();
    Serial.println("\nVenter 60 sekunder før neste runde...");
    delay(60000);
}
