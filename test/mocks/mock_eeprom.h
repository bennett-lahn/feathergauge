#pragma once

#include <stdint.h>
#include <string.h>
#include "mock_registry.h"

// Stand-in for AVR EEPROM: supplies the gauge serial number for CSV filenames and headers.
// Production reads two ASCII digits from fixed EEPROM addresses; tests override via mockReg.input.serialNumber.
struct MockEEPROM {
    // Copies mockReg.input.serialNumber into val; tests set this string before setup()/makeFileName().
    // Filenames use WG-XX_... where XX comes from EEPROM bytes at the serial-number address.
    template<typename T>
    T& get(int addr, T& val) {
        (void)addr;
        char* dst = reinterpret_cast<char*>(&val);
        size_t sz = sizeof(T);
        strncpy(dst, mockReg.input.serialNumber, sz);
        dst[sz - 1] = '\0';
        return val;
    }
};

inline MockEEPROM EEPROM;
