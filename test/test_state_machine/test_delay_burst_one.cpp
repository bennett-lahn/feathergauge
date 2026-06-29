#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for delay start + burst one-sample mode.
// Waits for deployment start, then alternates one sample per wake with long sleep.

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
// Config 6: DELAY_START=1, BURST_SAMPLING=1, BURST_SAMPLING_ONE_SAMPLE=1
// ===========================

#if DELAY_START && BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE

// One-sample mode uses manual LED warmup even with delay start.
// Delay start does not change the one-sample warmup rule: only a single manual pulse is scheduled.
static void test_delay_burst_one_configure_led_is_manual() {
    sm_setUp();
    configureLedWarmupIndicator();
    TEST_ASSERT_TRUE(ledWarmupManualPulsePending);
}

// Delay loop writes samples and sleeps twice before start threshold.
// Two powerDown iterations occur before RTC reaches START_*; each iteration can log a waiting-period sample.
static void test_delay_then_burst_one_sample_writes_during_wait() {
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
    TEST_ASSERT_EQUAL_INT(2, mockReg.audit.powerDownCalled); // two iterations before threshold
}

// Burst one-sample guard fires on every loop() iteration.
// After delay, each loop() still takes exactly one reading because BURST_SAMPLING_ONE_SAMPLE is always true.
static void test_delay_burst_one_loop_guard_always_true() {
    sm_setUp();
    // Burst one-sample: guard evaluates to true immediately every loop() call
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    uint32_t nowResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.analogReadValue = 512;
    loop();
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.sensorReadCount);
}

// Timer1 stays off after burst resume in one-sample mode.
// Same as non-delay one-sample: no Timer1 restart after enterBurstDeepSleep().
static void test_delay_burst_one_timer1_not_restarted() {
    sm_setUp();
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    uint32_t nowResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.analogReadValue = 512;
    enterBurstDeepSleep(endTime);
    TEST_ASSERT_EQUAL_INT(0, mockReg.audit.timer1RestartCount);
}

// resetTimer() does not clear alarm 1 when BURST_SAMPLING_ONE_SAMPLE is set.
// Confirms the compile-time guard that skips clearAlarm(1) in one-sample configurations.
static void test_delay_burst_one_reset_timer_skips_clear_alarm() {
    sm_setUp();
    int clearsBefore = (int)mockReg.audit.clearAlarmCalls.size();
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    // Should NOT have added a clearAlarm(1) because of !BURST_SAMPLING_ONE_SAMPLE guard
    TEST_ASSERT_EQUAL_INT(clearsBefore, (int)mockReg.audit.clearAlarmCalls.size());
}

// One CSV row per loop() cycle in delay + burst one-sample config.
// Counts CRLF-terminated rows in mockReg.state.fileData to ensure no duplicate writes per cycle.
static void test_delay_burst_one_writes_one_row_per_loop() {
    sm_setUp();
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    uint32_t nowResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.analogReadValue = 512;
    loop();
    // Exactly one CSV row
    int crlf_count = 0;
    const std::string& d = mockReg.state.fileData;
    for (size_t i = 0; i + 1 < d.size(); i++)
        if (d[i] == '\r' && d[i+1] == '\n') crlf_count++;
    TEST_ASSERT_EQUAL_INT(1, crlf_count);
}

#endif // DELAY_START && BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE

void register_tests_delay_burst_one() {
#if DELAY_START && BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE
    RUN_TEST(test_delay_burst_one_configure_led_is_manual);
    RUN_TEST(test_delay_then_burst_one_sample_writes_during_wait);
    RUN_TEST(test_delay_burst_one_loop_guard_always_true);
    RUN_TEST(test_delay_burst_one_timer1_not_restarted);
    RUN_TEST(test_delay_burst_one_reset_timer_skips_clear_alarm);
    RUN_TEST(test_delay_burst_one_writes_one_row_per_loop);
#endif
}
