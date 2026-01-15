#include <Arduino.h>

// --------------------------------------------------------------------------
// Hardware-konfigurasjon (Dine pinner)
// --------------------------------------------------------------------------
#define RE_DE_PIN 4
#define RX_PIN    16
#define TX_PIN    17

// --------------------------------------------------------------------------
// Globale variabler og buffere
// --------------------------------------------------------------------------
uint8_t rawData[25];  // Buffer for mottatt linje 15 fra CS50
uint8_t cmdBuf[18] = {195, 4, 0, 199, 81, 193, 4, 8, 32, 15, 0, 34, 128, 4, 0, 22, 0, 0};

// Variabler for dekodede verdier
float t1 = 0;
int fanSpeed = 0;
bool heaterActive = false;
uint32_t opHours = 0;

// --------------------------------------------------------------------------
// 1. FLETCHER CHECKSUM (Beregner A/B kontrollsum for CS50)
// --------------------------------------------------------------------------
void updateFletcherChecksum(uint8_t* buf) {
    int s1 = 0, s2 = 0;
    for (int i = 5; i < 16; i++) {
        s1 += buf[i];
        s2 += s1;
    }
    buf[16] = s1 % 256;
    buf[17] = s2 % 256;
}

// --------------------------------------------------------------------------
// 2. RÅ-DEKODING AV LINJE 15
// --------------------------------------------------------------------------
bool decodeLine15(uint8_t* buf) {
    // buf peker på starten av linje 15 (25 bytes)
    // buf[6] = Vifte (17/34/51)
    // buf[7] = Varme (128=PÅ, 0=AV)
    // buf[10] = Temp (preset varmeveksler-temp, 15–25)

    fanSpeed = buf[6] / 17;
    heaterActive = (buf[7] == 128);
    t1 = (float)buf[10];

    // opHours er egentlig ikke dokumentert for SL4R, men vi lar den stå som "best guess"
    opHours = (uint32_t)((buf[21] << 16) | (buf[22] << 8) | buf[23]);

    return (fanSpeed >= 0 && fanSpeed <= 3);
}

// --------------------------------------------------------------------------
// 3. SEND KOMMANDO MED INJEKSJON (Lytt -> Finn luke -> Send)
// --------------------------------------------------------------------------
void sendInjectedCommand(const char* beskrivelse, uint8_t fan, uint8_t heat, uint8_t temp) {
    cmdBuf[11] = fan * 17;         // 17, 34, 51
    cmdBuf[12] = heat ? 128 : 0;   // 128=PÅ, 0=AV
    cmdBuf[15] = temp;             // Settpunkt
    updateFletcherChecksum(cmdBuf);

    Serial.printf("\n[TX] %s (Vifte:%d, Varme:%s, Temp:%d)... ", 
                  beskrivelse, fan, heat ? "PÅ" : "AV", temp);
    
    unsigned long start = millis();
    while (millis() - start < 3000) { // 3 sekunder timeout
        if (Serial2.available()) {
            uint8_t b = Serial2.read();
            if (b == 195) {          // Startbyte
                while (!Serial2.available());
                uint8_t b2 = Serial2.read();
                if (b2 == 1) {       // Hode
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

                    // Nå er vi i pausen – send kommando
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
    }
    Serial.println("TIMEOUT (Fant ikke luke på bussen)");
}

// --------------------------------------------------------------------------
// 4. STATUSAVLESING (Lytter på bussen og dekoder linje 15)
// --------------------------------------------------------------------------
void readAndPrintStatus() {
    Serial.println("[INFO] Lytter etter status fra aggregat...");
    uint8_t buf[1000];
    int idx = 0;
    unsigned long start = millis();

    while (millis() - start < 2000) { // Let i 2 sekunder
        if (Serial2.available()) {
            buf[idx] = Serial2.read();

            // Matcher signaturen for linje 15:
            // ...195 .... 193 .... 22
            if (idx >= 8 && buf[idx] == 22 && buf[idx-2] == 193 && buf[idx-8] == 195) {
                // Kopier ut de siste 25 bytene som linje 15
                memcpy(rawData, &buf[idx-24], 25);

                if (decodeLine15(rawData)) {
                    Serial.println("------------------------------------");
                    Serial.printf(" >> STATUS BEKREFTET:\n");
                    Serial.printf(" >> Vifte-trinn: %d\n", fanSpeed);
                    Serial.printf(" >> Ettervarme:  %s\n", heaterActive ? "AKTIV" : "AV-SLÅTT");
                    Serial.printf(" >> Temp (T1):   %.1f C\n", t1);
                    Serial.printf(" >> Driftstimer: %lu t\n", (unsigned long)opHours);
                    Serial.println("------------------------------------");
                    return;
                }
            }

            idx++;
            if (idx >= 1000) idx = 0;
        }
    }
    Serial.println(" [!] Kunne ikke dekode status. Er A/B og strøm OK?");
}

// --------------------------------------------------------------------------
// SETUP & LOOP
// --------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    Serial2.begin(19200, SERIAL_8N1, RX_PIN, TX_PIN);
    Serial2.setTimeout(100);
    
    pinMode(RE_DE_PIN, OUTPUT);
    digitalWrite(RE_DE_PIN, LOW); // Start i lyttemodus

    Serial.println("\n===============================================");
    Serial.println("   FLEXIT SL4R / CS50 STANDALONE HW-TEST");
    Serial.println("   Ingen eksterne filer - Alt i én kilde");
    Serial.println("===============================================");
    delay(2000);
}

void loop() {
    // TEST 1: LASTSTYRING (SKRU AV VARME)
    sendInjectedCommand("LASTSTYRING: SKRUR AV VARME", 2, 0, 22);
    delay(1000);
    readAndPrintStatus();
    
    delay(10000); // Vent 10 sek

    // TEST 2: NORMAL DRIFT (SKRU PÅ VARME)
    sendInjectedCommand("RETUR: SKRUR PÅ VARME", 2, 1, 22);
    delay(1000);
    readAndPrintStatus();

    Serial.println("\nTestsirklus ferdig. Venter 30 sekunder...");
    delay(30000);
}
