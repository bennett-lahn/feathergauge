#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for delay start + burst multi-sample mode.
// Combines pre-deployment waiting with burst record/sleep cycles after start.

extern DateTime           currentDateTime;
extern volatile bool      samplingFlag;
extern volatile bool      resetTimerFlag;
extern uint8_t            secondsSinceFlush;
extern int16_t            currentVoltage;
extern bool               ledWarmupManualPulsePending;
extern uint8_t            ledWarmupToggleTarget;
extern char               fileName[];

#if BURST_SAMPLING
  extern TimeSpan  elapsed;
  extern DateTime  timeAtBurstSwitch;
#endif

static void sm_setUp() {
    // Reset mocks and put firmware globals in a known idle state.
    // Each test starts from the same baseline so flag/counter assertions are independent.
    mockResetAll();
    secondsSinceFlush = 0;
    samplingFlag      = false;
    resetTimerFlag    = false;
    currentVoltage    = 330;
    currentDateTime   = DateTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.fileOpenResults.push(true);
    outputFile.open("dummy", O_WRITE);
#if BURST_SAMPLING
    elapsed           = TimeSpan(0);
    timeAtBurstSwitch = currentDateTime;
#endif
}

// ===========================
// Config 5: DELAY_START=1, BURST_SAMPLING=1, BURST_SAMPLING_ONE_SAMPLE=0
// ===========================

#if DELAY_START && BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE

// Delay loop logs samples while waiting for start time.
// Pre-start behavior matches delay-only mode: at least one row is written before the start threshold.
static void test_delay_then_burst_multi_writes_sample_during_wait() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 2); // initial check: not yet
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 1); // after first sleep: not yet, write sample
    mockReg.input.rtcNowQueue.push(startDt.unixtime());      // after second sleep: exit
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.analogReadValue = 512;

    delayStartDeepSleepLoop();
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
}

// After delay, expired burst window triggers powerDown sleep.
// Once sampling has started, an elapsed window past WRITE_SECONDS should end the awake period with deep sleep.
static void test_loop_burst_multi_samples_then_sleeps() {
    sm_setUp();
    elapsed = TimeSpan((int32_t)WRITE_SECONDS + 1);
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.analogReadValue = 512;

    loop();
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.powerDownCalled);
}

// Burst multi LED warmup fits within the first sampling window.
// Same warmup logic as non-delay burst multi: enough sample slots for the default flash count.
static void test_delay_burst_multi_configure_led_not_manual() {
    sm_setUp();
    configureLedWarmupIndicator();
    // With WRITE_SECONDS=1, SAMPLE_FREQ=16: 16 readings ≥ 6*2=12 → no manual pulse
    TEST_ASSERT_FALSE(ledWarmupManualPulsePending);
}

// Timer1 restarts after waking from burst deep sleep.
// Multi-sample burst requires Timer1 to resume high-rate sampling after the inter-burst sleep.
static void test_delay_burst_multi_timer1_restarted_after_burst_sleep() {
    sm_setUp();
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.analogReadValue = 512;

    enterBurstDeepSleep(endTime);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.timer1RestartCount);
}

// elapsed tracks seconds since timeAtBurstSwitch across repeated resetTimer calls.
// Verifies the burst window timer accumulates correctly over several 1 Hz ticks.
static void test_elapsed_updated_correctly_over_multiple_ticks() {
    sm_setUp();
    timeAtBurstSwitch = DateTime(2026, 5, 4, 10, 0, 0);
    for (int i = 1; i <= 5; i++) {
        mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, i).unixtime());
        mockReg.input.analogReadValue = 512;
        resetTimer(true);
    }
    TEST_ASSERT_EQUAL_INT32(5, elapsed.totalseconds());
}

// burstSleepInterrupt is attached during sleep; resetTimerInterrupt after wake.
// enterBurstDeepSleep() must swap RTC ISR handlers when transitioning between sleep and active burst recording.
static void test_delay_burst_multi_isr_for_burst_sleep_is_burst_isr() {
    sm_setUp();
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.analogReadValue = 512;

    enterBurstDeepSleep(endTime);
    // First attachInterrupt during sleep was burstSleepInterrupt, second (after wake) was resetTimerInterrupt
    TEST_ASSERT_TRUE(mockReg.audit.attachISRCalls.size() >= 2);
    TEST_ASSERT_EQUAL_PTR((void*)burstSleepInterrupt, (void*)mockReg.audit.attachISRCalls[0].isr);
}

#endif // DELAY_START && BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE

void register_tests_delay_burst_multi() {
#if DELAY_START && BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE
    RUN_TEST(test_delay_then_burst_multi_writes_sample_during_wait);
    RUN_TEST(test_loop_burst_multi_samples_then_sleeps);
    RUN_TEST(test_delay_burst_multi_configure_led_not_manual);
    RUN_TEST(test_delay_burst_multi_timer1_restarted_after_burst_sleep);
    RUN_TEST(test_elapsed_updated_correctly_over_multiple_ticks);
    RUN_TEST(test_delay_burst_multi_isr_for_burst_sleep_is_burst_isr);
#endif
}
