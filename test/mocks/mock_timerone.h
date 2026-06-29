#pragma once

#include "mock_registry.h"

// Stand-in for TimerOne: drives high-rate sampling via triggerSampling ISR in continuous/burst-multi mode.
// Audit fields record initialize/restart/stop/attach calls for burst window state-machine assertions.
struct MockTimerOne {
    // Records that Timer1 was configured; burst-multi setup calls initialize() before attachInterrupt().
    void initialize(long) { mockReg.audit.timer1Initialized = true; }

    // Stores ISR pointer and count so tests verify triggerSampling was attached after burst wake.
    // timer1ISR should point at triggerSampling in continuous and burst-multi configurations.
    void attachInterrupt(void (*isr)()) {
        mockReg.audit.timer1ISR = isr;
        mockReg.audit.timer1AttachCount++;
    }

    // Called when resuming burst multi-sample recording after deep sleep.
    // enterBurstDeepSleep() restarts Timer1 so high-rate samples continue within the burst window.
    void restart() {
        mockReg.audit.timer1RestartCount++;
    }

    // Called before deep sleep; stops high-rate sampling until the next burst wake.
    void stop() {
        mockReg.audit.timer1StopCount++;
    }

    // No-op: detach is not audited; firmware rarely calls this on the 32u4 path under test.
    void detachInterrupt() {}
};

inline MockTimerOne Timer1;
