#ifndef FLEXIT_PROTOCOL_H
#define FLEXIT_PROTOCOL_H

#include <Arduino.h>

struct FlexitReadings {
    float t1, t2, t3, t4;
    int fanSpeed;
    bool heaterActive, rotorActive, filterAlarm;
    uint32_t operationalHours;
    int setpoint;
    bool isValid;
};

class FlexitProtocol {
public:
    static FlexitReadings decode(uint8_t* buf, int len) {
        FlexitReadings r = {0};
        if (len >= 26 && buf[0] == 0xC3) {
            r.t1 = (int16_t)((buf[9] << 8) | buf[10]) / 10.0;
            r.t2 = (int16_t)((buf[11] << 8) | buf[12]) / 10.0;
            r.t3 = (int16_t)((buf[13] << 8) | buf[14]) / 10.0;
            r.t4 = (int16_t)((buf[17] << 8) | buf[18]) / 10.0;
            r.fanSpeed = buf[5];
            r.setpoint = buf[7];
            r.filterAlarm = (buf[15] & 0x01);
            r.heaterActive = (buf[15] & 0x08);
            r.rotorActive = (buf[15] & 0x10);
            r.operationalHours = (uint32_t)((buf[22] << 24) | (buf[23] << 16) | (buf[24] << 8) | buf[25]);
            r.isValid = true;
        }
        return r;
    }
};
#endif
