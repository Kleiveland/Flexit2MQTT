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
        FlexitReadings r = {0};
        r.isValid = false;

        // Validering: CS 50 pakker er alltid 26-30 bytes
        if (len < 26 || buf[0] != 0xC3) return r;

        // Checksum-validering (Inspirert av Vongraven)
        uint8_t checksum = 0;
        for (int i = 0; i < len - 1; i++) checksum += buf[i];
        // if (checksum != buf[len-1]) return r; // Aktiveres når checksum-type er bekreftet

        // Temperaturer (Big Endian)
        r.t1 = (int16_t)((buf[9] << 8) | buf[10]) / 10.0;
        r.t2 = (int16_t)((buf[11] << 8) | buf[12]) / 10.0;
        r.t3 = (int16_t)((buf[13] << 8) | buf[14]) / 10.0;
        r.t4 = (int16_t)((buf[17] << 8) | buf[18]) / 10.0;

        // Statuser (Bitmasking fra Sindre Broch)
        r.fanSpeed = buf[5];
        r.filterAlarm  = (buf[15] & 0x01);       // Bit 0
        r.heaterActive = (buf[15] & 0x08) >> 3;  // Bit 3
        r.rotorActive  = (buf[15] & 0x10) >> 4;  // Bit 4

        // Driftstimer (Akkumulert 32-bit)
        r.operationalHours = (uint32_t)((buf[22] << 24) | (buf[23] << 16) | (buf[24] << 8) | buf[25]);
        
        r.isValid = true;
        return r;
    }
};
#endif
