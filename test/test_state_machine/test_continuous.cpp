#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for continuous sampling (no burst, no delay start).
// Exercises loop() and related paths when the device samples at a fixed rate around the clock.

extern DateTime           currentDateTime;
extern volatile bool      samplingFlag;
extern volatile bool      resetTimerFlag;
extern uint8_t            secondsSinceFlush;
extern volatile unsigned long millisAtInterrupt;
extern int16_t            currentVoltage;
extern uint8_t            ledWarmupToggleTarget;
extern bool               ledWarmupManualPulsePending;
extern char               fileName[];

static void sm_setUp() {
    // Reset mocks and put firmware globals in a known idle state.
    // Each test starts from the same baseline so flag/counter assertions are independent.
    mockResetAll();
    secondsSinceFlush = 0;
    samplingFlag      = false;
    resetTimerFlag    = false;
    millisAtInterrupt = 0;
    currentVoltage    = 330;
    currentDateTime   = DateTime(2026, 5, 4, 10, 0, 0);
    // Pre-open file so loop() can write
    mockReg.input.fileOpenResults.push(true);
    outputFile.open("dummy", O_WRITE);
}

#if !BURST_SAMPLING && !DELAY_START

// Idle loop with no flags set writes nothing and enters idle sleep.
// Confirms that loop() does not read the sensor or SD card until an interrupt sets a flag.
static void test_loop_no_flags_no_row_written() {
    sm_setUp();
    // No flags set - loop() sleeps and returns
    loop();
    TEST_ASSERT_EQUAL(0, (int)mockReg.state.fileData.size());
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.idleCalled);
}

// samplingFlag causes one sensor read and CSV row per loop iteration.
// Timer1 sets this flag from its ISR; the main loop clears it after performSensorReading().
static void test_loop_sampling_flag_writes_one_row() {
    sm_setUp();
    samplingFlag = true;
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    loop();
    TEST_ASSERT_FALSE(samplingFlag);
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
}

// resetTimerFlag path increments the flush counter via resetTimer(true).
// The RTC 1 Hz interrupt sets resetTimerFlag; loop() calls resetTimer(true) to sync time and count seconds toward SD flush.
static void test_loop_reset_timer_flag_increments_flush_counter() {
    sm_setUp();
    resetTimerFlag = true;
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    loop();
    TEST_ASSERT_FALSE(resetTimerFlag);
    TEST_ASSERT_EQUAL_UINT8(1, secondsSinceFlush);
}

// resetTimer runs before sampling so the row uses RTC-corrected time, not ISR guess.
// When both flags are set in one loop pass, time correction must happen before the sample is stamped.
static void test_loop_both_flags_reset_runs_before_sample() {
    sm_setUp();
    resetTimerFlag = true;
    samplingFlag   = true;
    DateTime corrected(2026, 5, 4, 10, 0, 5);
    mockReg.input.rtcNowQueue.push(corrected.unixtime());
    mockReg.input.analogReadValue  = 512;
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.millisValue = 100;
    loop();
    TEST_ASSERT_FALSE(resetTimerFlag);
    TEST_ASSERT_FALSE(samplingFlag);
    // Row should contain the corrected time, not the ISR-pre-incremented time
    TEST_ASSERT_NOT_NULL(strstr(mockReg.state.fileData.c_str(), "10:00:05"));
}

// resetTimer() overwrites the ISR's +1 second pre-increment with rtc.now().
// The ISR optimistically advances currentDateTime; resetTimer() replaces it with the authoritative RTC read.
static void test_isr_handoff_corrected_time_wins() {
    sm_setUp();
    // Simulate ISR pre-increment then resetTimer correction
    currentDateTime = DateTime(2026, 5, 4, 10, 0, 0);
    resetTimerInterrupt(); // now 10:00:01
    DateTime authoritative(2026, 5, 4, 10, 0, 2);
    mockReg.input.rtcNowQueue.push(authoritative.unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    TEST_ASSERT_EQUAL_UINT32(authoritative.unixtime(), currentDateTime.unixtime());
}

// At FLUSH_INTERVAL_SECONDS, loop() syncs and resets the counter.
// Periodic SD sync prevents data loss if power is lost between explicit flushes.
static void test_flush_threshold_syncs_on_exact_count() {
    sm_setUp();
    secondsSinceFlush = FLUSH_INTERVAL_SECONDS;
    loop();
    TEST_ASSERT_EQUAL_UINT8(0, secondsSinceFlush); // reset after successful sync
}

// Failed periodic sync leaves secondsSinceFlush unchanged.
// A failed flush should not reset the counter, so the firmware retries on the next loop pass.
static void test_flush_sync_fail_preserves_counter_and_recovers() {
    sm_setUp();
    secondsSinceFlush = FLUSH_INTERVAL_SECONDS;
    mockReg.input.syncResults.push(false);
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true);
    loop();
    TEST_ASSERT_EQUAL_UINT8(FLUSH_INTERVAL_SECONDS, secondsSinceFlush);
}

// Continuous mode uses the full default LED warmup flash count.
// Without burst timing constraints, the startup LED sequence can use all configured flashes.
static void test_configure_led_warmup_continuous_mode() {
    sm_setUp();
    configureLedWarmupIndicator();
    TEST_ASSERT_EQUAL_UINT8(LED_WARMUP_DEFAULT_FLASHES * 2, ledWarmupToggleTarget);
    TEST_ASSERT_FALSE(ledWarmupManualPulsePending);
}

// resetTimer() clears alarm 1 in continuous / burst-multi configs.
// After each 1 Hz tick, the DS3231 alarm must be cleared so the next second can trigger again.
static void test_reset_timer_does_clear_alarm_1() {
    sm_setUp();
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    // clearAlarm(1) should have been called
    bool found = false;
    for (int c : mockReg.audit.clearAlarmCalls) if (c == 1) { found = true; break; }
    TEST_ASSERT_TRUE(found);
}

#endif // !BURST_SAMPLING && !DELAY_START

void register_tests_continuous() {
#if !BURST_SAMPLING && !DELAY_START
    RUN_TEST(test_loop_no_flags_no_row_written);
    RUN_TEST(test_loop_sampling_flag_writes_one_row);
    RUN_TEST(test_loop_reset_timer_flag_increments_flush_counter);
    RUN_TEST(test_loop_both_flags_reset_runs_before_sample);
    RUN_TEST(test_isr_handoff_corrected_time_wins);
    RUN_TEST(test_flush_threshold_syncs_on_exact_count);
    RUN_TEST(test_flush_sync_fail_preserves_counter_and_recovers);
    RUN_TEST(test_configure_led_warmup_continuous_mode);
    RUN_TEST(test_reset_timer_does_clear_alarm_1);
#endif
}
