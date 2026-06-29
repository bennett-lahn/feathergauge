#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <utility>

#include "mock_registry.h"
#include "mock_avr_power.h"

// Minimal Arduino HAL for native builds: enough for firmware to compile and run logic tests on the host.
// Replaces avr-libc and Arduino core symbols that the .ino includes unconditionally.

typedef unsigned char  byte;
typedef bool           boolean;

#define HIGH            1
#define LOW             0
#define INPUT           0
#define OUTPUT          1
#define INPUT_PULLUP    2
#define FALLING         2
#define RISING          3
#define CHANGE          1

// Analog pin numbers for the ATmega32u4 / Feather layout (used by analogRead pin constants).
#define A0   18
#define A1   19
#define A2   20
#define A3   21
#define A4   24
#define A5   25
#define A6   26
#define A7   27
#define A8   28
#define A9   29
#define A10  30
#define A11  31

#ifndef INT_MAX
  #include <limits.h>
#endif

// On AVR, F() stores strings in flash; on native it is a pass-through so F("...") still compiles.
#define F(x) (x)

// mockReg.input.millisValue stands in for the MCU millisecond counter (also used by popNow() fallback).
// Tests set millisValue to control sub-second CSV timestamps and ISR-to-loop timing deltas.
inline unsigned long millis() { return mockReg.input.millisValue; }
inline unsigned long micros() { return mockReg.input.millisValue * 1000UL; }
// No-op: firmware calls delay() around sensor conversion waits; native tests skip real time.
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}

inline void pinMode(int, int) {}

// Each call logged in mockReg.audit.digitalWriteCalls (LED warmup and error-blink paths).
// setup() and error() drive the status LED; tests assert pin/value sequences without GPIO hardware.
inline void digitalWrite(int pin, int val) {
    mockReg.audit.digitalWriteCalls.push_back({pin, val});
}

inline int digitalRead(int) { return LOW; }

// mockReg.input.analogReadValue feeds getBatteryVoltage() / calculateBatteryVoltage() tests.
// performSensorReading() samples the battery pin each row; set analogReadValue before calling firmware.
inline int analogRead(int) { return mockReg.input.analogReadValue; }

inline void analogWrite(int, int) {}

// No-op on native: firmware still calls these around shared global reads/writes.
// Real firmware disables interrupts briefly around samplingFlag/resetTimerFlag; not modeled on host.
inline void noInterrupts() {}
inline void interrupts()   {}

inline int digitalPinToInterrupt(int pin) { return pin; }

// Does not run ISRs; records pin, handler, and edge mode for attach/detach verification tests.
// setup() attaches resetTimerInterrupt on the RTC alarm pin; audit.attachISRCalls captures the handler.
inline void attachInterrupt(int pin, void (*isr)(), int mode) {
    mockReg.audit.attachISRCalls.push_back({pin, isr, mode});
}

inline void detachInterrupt(int pin) {
    (void)pin;
    // Counted so tests verify alarm ISR is torn down before burst deep sleep.
    mockReg.audit.detachISRCount++;
}

// AVR provides these in avr/stdlib.h; glibc lacks them but firmware string formatting uses them.
static inline char* itoa(int val, char* buf, int /*base*/) {
    snprintf(buf, 14, "%d", val); return buf;
}
static inline char* ltoa(long val, char* buf, int /*base*/) {
    snprintf(buf, 24, "%ld", val); return buf;
}
static inline char* utoa(unsigned int val, char* buf, int /*base*/) {
    snprintf(buf, 14, "%u", val); return buf;
}
static inline char* ultoa(unsigned long val, char* buf, int /*base*/) {
    snprintf(buf, 24, "%lu", val); return buf;
}

// Serial is disabled in production firmware; stub satisfies setup()'s Serial.end() call.
// No output is captured; tests never depend on UART logging.
struct MockSerial {
    void begin(unsigned long)  {}
    void end()                 {}
    template<typename T> void print(T)   {}
    template<typename T> void println(T) {}
    void println()             {}
    void flush()               {}
    int  available()           { return 0; }
};

inline MockSerial Serial;
inline MockSerial Serial1;

// SdFat expects this constant; defined here to avoid pulling in the full SPI library on native.
#define SPI_HALF_SPEED 0
