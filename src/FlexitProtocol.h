#ifndef FLEXIT_PROTOCOL_H
#define FLEXIT_PROTOCOL_H

#include <Arduino.h>

struct FlexitReadings {
    float t1, t2, t3, t4;
    int fanSpeed;
    bool heaterActive, rotorActive, filterAlarm;
    uint32_t operationalHours;
    bool isValid;
};

class FlexitProtocol {
public:
    static FlexitReadings decode(uint8_t* buf, int len) {
        FlexitReadings r;
        // Design-prinsipp: Sikker initialisering
        r.t1 = r.t2 = r.t3 = r.t4 = float(0);
        r.fanSpeed = int(0);
        r.heaterActive = r.rotorActive = r.filterAlarm = false;
        r.operationalHours = (uint32_t)0;
        r.isValid = false;

        if (len < 26 || buf[0] != 0xC3) return r;

        // Temperaturer (Big Endian)
        r.t1 = (int16_t)((buf[9] << 8) | buf[10]) / 10.0f;
        r.t2 = (int16_t)((buf[11] << 8) | buf[12]) / 10.0f;
        r.t3 = (int16_t)((buf[13] << 8) | buf[14]) / 10.0f;
        r.t4 = (int16_t)((buf[17] << 8) | buf[18]) / 10.0f;

        // Statuser
        r.fanSpeed = (int)buf[5];
        r.filterAlarm  = (buf[15] & 0x01);
        r.heaterActive = (buf[15] & 0x08) >> 3;
        r.rotorActive  = (buf[15] & 0x10) >> 4;

        r.operationalHours = (uint32_t)((buf[22] << 24) | (buf[23] << 16) | (buf[24] << 8) | buf[25]);
        
        r.isValid = true;
        return r;
    }
};
#endif
