#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <vector>
#include <queue>

// Thrown by error() under NATIVE_TEST so fatal firmware paths unwind instead of hanging.
// On real hardware error() loops forever; in tests, catch TestAbort to assert the error code.
struct TestAbort {
    uint8_t errorCode;
};

// One entry in the audit log when firmware calls rtc.setAlarm1().
// Tests inspect whenUnix and mode to verify burst sleep, delay start, and 1 Hz scheduling.
struct AlarmCall {
    uint32_t whenUnix;   // DateTime.unixtime() of the alarm target
    uint8_t  mode;       // DS3231_A1_* or DS3231_A2_* enum value
};

// One entry when firmware calls attachInterrupt() on the RTC GPIO pin.
// Tests verify which ISR handler was registered and in what order across sleep/wake transitions.
struct AttachISRCall {
    int        pin;
    void     (*isr)();
    int        mode;
};

// Shared hub for all native-test mocks. Global instance: mockReg.
//
//   input  - values tests program before calling firmware (queues, flags, HAL readings)
//   state  - simulated device memory updated as firmware runs
//   audit  - call log and counters for post-run assertions
struct MockRegistry {

    // Test-programmed stub returns: push values here before calling firmware.
    struct Input {
        // RTC - rtcNowQueue drives rtc.now() in call order for temporal/state-machine tests
        std::queue<uint32_t> rtcNowQueue;
        bool     rtcBeginResult = true;
        bool     rtcLostPower   = false;

        // MS5803 - pressure/temperature queues supply getSensorReadings() outputs
        std::queue<int32_t> pressureQueue;
        std::queue<int32_t> temperatureQueue;

        // SD card - queues simulate success/failure sequences for open, sync, exists, write errors
        bool             sdBeginResult = true;
        std::queue<bool> existsResults;
        std::queue<bool> fileOpenResults;
        std::queue<bool> syncResults;
        std::queue<bool> writeErrorResults;
        uint32_t         fileSizeOverride = 0;
        bool             useFileSizeOverride = false;

        // Arduino HAL - millisValue and analogReadValue stand in for the MCU clock and ADC
        unsigned long millisValue   = 0;
        int           analogReadValue = 0;

        // EEPROM - serial number string embedded in CSV filenames (default "08")
        char serialNumber[16];
    } input;

    // In-memory model updated by mock I/O as firmware runs.
    struct State {
        std::string fileData;   // accumulated CSV bytes from File32 write/print
        bool        fileIsOpen = false;
    } state;

    // Call log and counters: inspect after firmware runs to assert behavior.
    struct Audit {
        // RTC
        std::vector<AlarmCall> setAlarm1Calls;
        std::vector<int>       clearAlarmCalls;
        std::vector<int>       disableAlarmCalls;
        uint32_t               adjustedToUnix = 0;
        bool                   adjustCalled   = false;

        // MS5803
        int sensorReadCount = 0;

        // SD card / file
        int closeCount      = 0;
        int syncCount       = 0;
        int recoverAttempts = 0;

        // error() seam
        uint8_t lastError   = 0;
        bool    errorCalled = false;

        // LowPower
        int idleCalled      = 0;
        int powerDownCalled = 0;

        // Timer1
        bool  timer1Initialized = false;
        int   timer1RestartCount = 0;
        int   timer1StopCount    = 0;
        void (*timer1ISR)()      = nullptr;
        int   timer1AttachCount  = 0;

        // Wire
        int wireBeginCount = 0;
        int wireEndCount   = 0;

        // GPIO interrupts
        std::vector<AttachISRCall> attachISRCalls;
        int  detachISRCount = 0;

        // Arduino HAL
        std::vector<std::pair<int,int>> digitalWriteCalls;
    } audit;

    // Pop next sd.exists() result; defaults to false (file not present).
    // makeFileName() walks _IT-XX indices until exists() returns false.
    bool popExists() {
        if (input.existsResults.empty()) return false;
        bool v = input.existsResults.front();
        input.existsResults.pop();
        return v;
    }

    // Pop next outputFile.open() result; defaults to true so happy-path tests need no setup.
    // Queue false entries to simulate open failures during setup() or recoverSdCard().
    bool popFileOpen() {
        if (input.fileOpenResults.empty()) return true;
        bool v = input.fileOpenResults.front();
        input.fileOpenResults.pop();
        return v;
    }

    // Pop next outputFile.sync() result; defaults to true.
    // Queue false to test flush paths that leave secondsSinceFlush unchanged on failure.
    bool popSync() {
        if (input.syncResults.empty()) return true;
        bool v = input.syncResults.front();
        input.syncResults.pop();
        return v;
    }

    // Pop next getWriteError() result; defaults to false (no error).
    // Queue true to trigger recoverSdCard() after a simulated SD write fault.
    bool popWriteError() {
        if (input.writeErrorResults.empty()) return false;
        bool v = input.writeErrorResults.front();
        input.writeErrorResults.pop();
        return v;
    }

    // Pop next rtc.now() unix timestamp; falls back to millisValue/1000 if queue is empty.
    // Temporal and state-machine tests push times in the order firmware will call now().
    uint32_t popNow() {
        if (input.rtcNowQueue.empty()) return input.millisValue / 1000;
        uint32_t v = input.rtcNowQueue.front();
        input.rtcNowQueue.pop();
        return v;
    }

    // Pop next pressure reading (0.1 mbar units); defaults to ~sea level if queue is empty.
    // Fault tests push extreme values to verify formatDataRow() buffer limits.
    int32_t popPressure() {
        if (input.pressureQueue.empty()) return 101325;
        int32_t v = input.pressureQueue.front();
        input.pressureQueue.pop();
        return v;
    }

    // Pop next temperature reading (0.01 C units); defaults to 20.00 C if queue is empty.
    // Paired with popPressure() so getSensorReadings() returns deterministic CSV rows.
    int32_t popTemp() {
        if (input.temperatureQueue.empty()) return 2000;
        int32_t v = input.temperatureQueue.front();
        input.temperatureQueue.pop();
        return v;
    }

    // Clear all input queues, state, and audit fields back to defaults.
    // Called via mockResetAll() from setUp() so each test starts with an isolated mock state.
    void reset() {
        while (!input.rtcNowQueue.empty())      input.rtcNowQueue.pop();
        while (!input.pressureQueue.empty())    input.pressureQueue.pop();
        while (!input.temperatureQueue.empty()) input.temperatureQueue.pop();
        while (!input.existsResults.empty())    input.existsResults.pop();
        while (!input.fileOpenResults.empty())  input.fileOpenResults.pop();
        while (!input.syncResults.empty())      input.syncResults.pop();
        while (!input.writeErrorResults.empty()) input.writeErrorResults.pop();

        audit.setAlarm1Calls.clear();
        audit.clearAlarmCalls.clear();
        audit.disableAlarmCalls.clear();
        audit.attachISRCalls.clear();
        audit.digitalWriteCalls.clear();
        state.fileData.clear();

        input.rtcBeginResult      = true;
        input.rtcLostPower        = false;
        input.sdBeginResult       = true;
        input.fileSizeOverride    = 0;
        input.useFileSizeOverride = false;
        input.millisValue         = 0;
        input.analogReadValue     = 0;
        strcpy(input.serialNumber, "08");

        state.fileIsOpen = false;

        audit.adjustCalled        = false;
        audit.adjustedToUnix      = 0;
        audit.sensorReadCount     = 0;
        audit.closeCount          = 0;
        audit.syncCount           = 0;
        audit.recoverAttempts     = 0;
        audit.lastError           = 0;
        audit.errorCalled         = false;
        audit.idleCalled          = 0;
        audit.powerDownCalled     = 0;
        audit.timer1Initialized   = false;
        audit.timer1RestartCount  = 0;
        audit.timer1StopCount     = 0;
        audit.timer1ISR           = nullptr;
        audit.timer1AttachCount   = 0;
        audit.wireBeginCount      = 0;
        audit.wireEndCount        = 0;
        audit.detachISRCount      = 0;
    }
};

inline MockRegistry mockReg;

// Called from Unity setUp() / sm_setUp() at the start of every test.
// Thin wrapper so tests do not need to know about the MockRegistry type directly.
inline void mockResetAll() { mockReg.reset(); }
