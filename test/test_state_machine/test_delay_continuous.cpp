#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for delay start + continuous sampling.
// setup() may sleep until START_* before normal continuous sampling begins.

extern DateTime           currentDateTime;
extern volatile bool      samplingFlag;
extern volatile bool      resetTimerFlag;
extern uint8_t            secondsSinceFlush;
extern int16_t            currentVoltage;
extern bool               ledWarmupManualPulsePending;
extern uint8_t            ledWarmupToggleTarget;
extern char               fileName[];

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
}

// ===========================
// Config 4: DELAY_START=1, BURST_SAMPLING=0
// ===========================

#if DELAY_START && !BURST_SAMPLING

// Delay loop writes one sample per iteration before start time is reached.
// While waiting for the configured start date, the firmware still logs periodic readings to the SD card.
static void test_delay_start_loop_samples_while_waiting() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    // Three queue entries: initial check not yet, first loop not yet (write sample), second loop at threshold (exit)
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 2); // initial check: not yet
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 1); // after first sleep: not yet, write sample
    mockReg.input.rtcNowQueue.push(startDt.unixtime());      // after second sleep: at threshold, exit
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.analogReadValue = 512;

    delayStartDeepSleepLoop();

    // One sample row should have been written during the wait
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.detachISRCount);
}

// Start time already passed: exit immediately with no samples.
// If the RTC is already past START_*, delayStartDeepSleepLoop() returns without sleeping or writing.
static void test_delay_start_past_threshold_immediate_return() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime() + 100);

    size_t dataBefore = mockReg.state.fileData.size();
    delayStartDeepSleepLoop();
    TEST_ASSERT_EQUAL((int)dataBefore, (int)mockReg.state.fileData.size());
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.detachISRCount);
}

// Each wait iteration calls LowPower.powerDown once.
// Counts deep-sleep entries so we know the loop ran the expected number of times before start.
static void test_delay_start_power_down_called_each_iteration() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 2); // not yet
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 1); // after sleep: still not yet, write sample
    mockReg.input.rtcNowQueue.push(startDt.unixtime());      // exit
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.pressureQueue.push(10133);
    mockReg.input.temperatureQueue.push(2001);
    mockReg.input.analogReadValue = 512;

    delayStartDeepSleepLoop();
    TEST_ASSERT_EQUAL_INT(2, mockReg.audit.powerDownCalled);
}

// Delay start arms alarm 1 in hourly mode until start time.
// The RTC hourly alarm wakes the MCU once per hour while waiting for the deployment start time.
static void test_delay_start_alarm_uses_hour_mode() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime()); // immediate exit
    delayStartDeepSleepLoop();
    TEST_ASSERT_EQUAL_INT(1, (int)mockReg.audit.setAlarm1Calls.size());
    TEST_ASSERT_EQUAL_UINT8(DS3231_A1_Hour, mockReg.audit.setAlarm1Calls[0].mode);
}

// After delay exit, loop() behaves like normal continuous sampling.
// Once delay start finishes, the main loop should accept samplingFlag like the non-delay configuration.
static void test_continuous_loop_after_delay_start_responds_to_sampling_flag() {
    sm_setUp();
    // After delay-start exits, loop() should behave like continuous mode
    samplingFlag = true;
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    loop();
    TEST_ASSERT_FALSE(samplingFlag);
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
}

// I2C is restarted (Wire.end/begin) after each deep-sleep wakeup.
// Reinitializing I2C after powerDown improves stability when waking to read the RTC and sensor.
static void test_delay_start_wire_end_begin_on_each_iteration() {
    sm_setUp();
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 1);
    mockReg.input.rtcNowQueue.push(startDt.unixtime());
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.analogReadValue = 512;

    delayStartDeepSleepLoop();
    // Wire.end() + Wire.begin() pair per iteration
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.wireEndCount);
    // wireBeginCount includes begin() at the start of delayStartDeepSleepLoop and after sleep
}

#endif // DELAY_START && !BURST_SAMPLING

void register_tests_delay_continuous() {
#if DELAY_START && !BURST_SAMPLING
    RUN_TEST(test_delay_start_loop_samples_while_waiting);
    RUN_TEST(test_delay_start_past_threshold_immediate_return);
    RUN_TEST(test_delay_start_power_down_called_each_iteration);
    RUN_TEST(test_delay_start_alarm_uses_hour_mode);
    RUN_TEST(test_continuous_loop_after_delay_start_responds_to_sampling_flag);
    RUN_TEST(test_delay_start_wire_end_begin_on_each_iteration);
#endif
}
