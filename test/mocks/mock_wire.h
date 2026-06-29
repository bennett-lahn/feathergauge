#pragma once

#include "mock_registry.h"

// Minimal I2C stub: RTC and MS5803 share the Wire bus but need no real transactions on native builds.
// Sensor and clock data come from mockReg queues; Wire only tracks begin()/end() for power-management tests.
struct TwoWire {
    // Incremented when firmware starts I2C (including after delay-start wake restarts).
    // setup() and post-sleep paths call begin(); count verifies bus was reinitialized.
    void begin()                          { mockReg.audit.wireBeginCount++; }
    // Incremented when firmware shuts down I2C before deep sleep.
    // enterBurstDeepSleep() ends the bus to save power; wireEndCount tracks that transition.
    void end()                            { mockReg.audit.wireEndCount++;   }
    // Remaining Wire API stubs: no transactions modeled; RTC and MS5803 reads come from mockReg queues.
    void setClock(unsigned long)          {}
    void beginTransmission(uint8_t)       {}
    uint8_t endTransmission(bool = true)  { return 0; }
    size_t write(uint8_t)                 { return 1; }
    int    available()                    { return 0; }
    int    read()                         { return -1; }
    int    requestFrom(uint8_t, uint8_t)  { return 0; }
};

inline TwoWire Wire;
inline TwoWire Wire1;
