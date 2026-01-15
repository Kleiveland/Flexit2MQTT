# Flexit2MQTT - Smartstyring for Flexit SL4R (CS 50)

Dette prosjektet gjør det mulig å integrere Flexit SL4R med Home Assistant via MQTT, med støtte for laststyring på L1-fasen.

## Filoversikt
- **src/main.cpp**: Hovedmotor (WiFi, MQTT og WebServer).
- **src/FlexitProtocol.h**: Protokoll-tolk basert på Vongraven/Broch (CI66).
- **data/index.html**: Det innebygde dashboardet (amsreader-stil).
- **platformio.ini**: Prosjektkonfigurasjon for ESP32 DevKitV1.

## Maskinvare
- ESP32 NodeMCU DevKitV1
- RS485 DIN-rail Expansion Board (DC 7-30V)
