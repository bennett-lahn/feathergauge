#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for burst one-sample mode.
// Each wake cycle takes exactly one reading, then sleeps until the next burst window.

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
// Config 3: BURST_SAMPLING=1, BURST_SAMPLING_ONE_SAMPLE=1, DELAY_START=0
// ===========================

#if BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE && !DELAY_START

// One-sample guard is always true; loop() takes one reading then sleeps.
// BURST_SAMPLING_ONE_SAMPLE makes the burst-end condition true every iteration, so each loop() is one sample plus sleep.
static void test_loop_one_sample_mode_immediate_burst_guard_true() {
    sm_setUp();
    // BURST_SAMPLING_ONE_SAMPLE is true → guard `elapsed > WRITE_SECONDS || BURST_SAMPLING_ONE_SAMPLE`
    // is always true. loop() enters the burst block every iteration.
    // Queue one sensor reading for performSensorReading() inside burst block
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    // rtc.now() for endTime
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    // enterBurstDeepSleep - resume branch (now >= endTime + SLEEP_SECONDS - 1)
    uint32_t nowForResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowForResume); // inside enterBurstDeepSleep
    mockReg.input.rtcNowQueue.push(nowForResume); // resetTimer(false)
    mockReg.input.analogReadValue = 512;
    loop();
    // One row written by performSensorReading() inside loop()
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.sensorReadCount);
}

// Timer1 sampling path is disabled; sample comes from the burst block only.
// In one-sample mode performSensorReading() runs inside the burst block, not on samplingFlag.
static void test_sampling_flag_not_used_in_one_sample_mode() {
    sm_setUp();
    samplingFlag = true;
    // Queue to let loop() get through burst block
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    uint32_t nowResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.rtcNowQueue.push(nowResume);
    mockReg.input.analogReadValue = 512;
    loop();
    // samplingFlag cleared but the sample from performSensorReading() inside burst block is what ran
    TEST_ASSERT_FALSE(samplingFlag);
}

// One-sample mode always uses a single manual LED pulse.
// There is no time for a multi-flash warmup sequence, so configureLedWarmupIndicator() sets manual pulse mode.
static void test_configure_led_warmup_one_sample_is_manual() {
    sm_setUp();
    configureLedWarmupIndicator();
    TEST_ASSERT_TRUE(ledWarmupManualPulsePending);
    TEST_ASSERT_EQUAL_UINT8(0, ledWarmupToggleTarget);
}

// resetTimer() skips clearAlarm(1) when BURST_SAMPLING_ONE_SAMPLE is set.
// One-sample burst does not use the per-second RTC alarm during recording, so resetTimer() must not clear it.
static void test_reset_timer_does_not_clear_alarm_1_in_one_sample() {
    // In burst one-sample, clearAlarm(1) inside resetTimer() is skipped because
    // the #if !(BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE) guard excludes it
    sm_setUp();
    int clearsBefore = (int)mockReg.audit.clearAlarmCalls.size();
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    int clearsAfter = (int)mockReg.audit.clearAlarmCalls.size();
    // No additional clearAlarm(1) calls from resetTimer()
    TEST_ASSERT_EQUAL_INT(clearsBefore, clearsAfter);
}

// Timer1 is not restarted after burst resume in one-sample mode.
// High-rate Timer1 sampling is unused when only one sample is taken per wake cycle.
static void test_burst_resume_in_one_sample_does_not_restart_timer1() {
    sm_setUp();
    DateTime endTime(2026, 5, 4, 10, 0, 0);
    uint32_t nowResume = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowResume); // now inside enterBurstDeepSleep
    mockReg.input.rtcNowQueue.push(nowResume); // resetTimer(false)
    mockReg.input.analogReadValue = 512;
    enterBurstDeepSleep(endTime);
    // Timer1 should NOT be restarted in one-sample mode
    TEST_ASSERT_EQUAL_INT(0, mockReg.audit.timer1RestartCount);
    TEST_ASSERT_EQUAL_INT(0, mockReg.audit.timer1AttachCount);
}

// Exactly one sensor read per loop() cycle in one-sample mode.
// Confirms the burst block does not double-read or leave extra rows in the mock SD buffer.
static void test_burst_one_sample_writes_exactly_one_row_per_cycle() {
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
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.sensorReadCount);
}

#endif // BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE && !DELAY_START

void register_tests_burst_one() {
#if BURST_SAMPLING && BURST_SAMPLING_ONE_SAMPLE && !DELAY_START
    RUN_TEST(test_loop_one_sample_mode_immediate_burst_guard_true);
    RUN_TEST(test_sampling_flag_not_used_in_one_sample_mode);
    RUN_TEST(test_configure_led_warmup_one_sample_is_manual);
    RUN_TEST(test_reset_timer_does_not_clear_alarm_1_in_one_sample);
    RUN_TEST(test_burst_resume_in_one_sample_does_not_restart_timer1);
    RUN_TEST(test_burst_one_sample_writes_exactly_one_row_per_cycle);
#endif
}
