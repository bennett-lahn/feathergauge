#pragma once

#include <stdint.h>
#include "mock_registry.h"

// Enums from SparkFun_MS5803_I2C.h so firmware compiles unchanged without the real library.

enum precision {
    ADC_256  = 0x00,
    ADC_512  = 0x02,
    ADC_1024 = 0x04,
    ADC_2048 = 0x06,
    ADC_4096 = 0x08
};

enum ms5803_addr {
    ADDRESS_HIGH = 0x76,
    ADDRESS_LOW  = 0x77
};

enum reading_mode { BOTH, TEMP_ONLY, PRESSURE_ONLY };

// Stand-in for the MS5803 pressure sensor; CustomMS5803 in feathergauge_code.h inherits from this.
// getSensorReadings() is the only method exercised by performSensorReading() in native tests.
class MS5803 {
public:
    MS5803(ms5803_addr = ADDRESS_HIGH) {}
    // No-op: reset sequence is not exercised by current tests.
    void reset() {}
    // Always succeeds (returns 0); begin failure is not modeled separately from getSensorReadings().
    int  begin() { return 0; }

    // Pops mockReg.input pressure/temperature queues; increments mockReg.audit.sensorReadCount.
    // performSensorReading() calls this once per sample; count verifies sampling frequency in state-machine tests.
    void getSensorReadings(precision, precision,
                           int32_t* pressure, int32_t* temperature,
                           reading_mode = BOTH) {
        mockReg.audit.sensorReadCount++;
        if (pressure)    *pressure    = mockReg.popPressure();
        if (temperature) *temperature = mockReg.popTemp();
    }

protected:
    // Overridden by CustomMS5803 on hardware for conversion delay; no wait needed on native builds.
    virtual void sensorWait(uint8_t) {}
};
