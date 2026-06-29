#include <unity.h>
#include "feathergauge_code.h"

// State-machine tests for burst multi-sample mode.
// Burst mode records for WRITE_SECONDS, then sleeps for SLEEP_SECONDS before the next window.

extern DateTime           currentDateTime;
extern volatile bool      samplingFlag;
extern volatile bool      resetTimerFlag;
extern uint8_t            secondsSinceFlush;
extern volatile unsigned long millisAtInterrupt;
extern int16_t            currentVoltage;
extern uint8_t            ledWarmupToggleTarget;
extern bool               ledWarmupManualPulsePending;
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
    millisAtInterrupt = 0;
    currentVoltage    = 330;
    currentDateTime   = DateTime(2026, 5, 4, 10, 0, 0);
    mockReg.input.fileOpenResults.push(true);
    outputFile.open("dummy", O_WRITE);
#if BURST_SAMPLING
    elapsed           = TimeSpan(0);
    timeAtBurstSwitch = currentDateTime;
#endif
}

#if BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE && !DELAY_START

// Within the write window, sampling proceeds without entering burst sleep.
// While elapsed <= WRITE_SECONDS the device stays awake and Timer1-driven sampling continues.
static void test_burst_write_window_active_allows_sampling() {
    sm_setUp();
    elapsed = TimeSpan(0); // within window
    samplingFlag = true;
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    // elapsed <= WRITE_SECONDS → loop() does NOT enter burst sleep block
    // Just runs samplingFlag path
    loop();
    TEST_ASSERT_FALSE(samplingFlag);
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
    TEST_ASSERT_EQUAL_INT(0, mockReg.audit.powerDownCalled);
}

// elapsed == WRITE_SECONDS does not trigger sleep (guard is strictly '>').
// The burst window is open through the full WRITE_SECONDS duration; sleep starts only after it expires.
static void test_burst_window_boundary_equal_no_sleep() {
    sm_setUp();
    elapsed = TimeSpan((int32_t)WRITE_SECONDS); // exactly equal, guard is '>'
    // Queue enough for loop to complete without crash
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    loop();
    TEST_ASSERT_EQUAL_INT(0, mockReg.audit.powerDownCalled);
}

// Window expiry flushes SD, enters powerDown, and leaves flush counter at 0.
// When the write window ends, loop() syncs the file and enterBurstDeepSleep() puts the MCU to sleep until the next cycle.
static void test_burst_window_expired_triggers_flush_and_sleep() {
    sm_setUp();
    elapsed = TimeSpan((int32_t)WRITE_SECONDS + 1);
    // loop() will call outputFile.sync() then enterBurstDeepSleep(rtc.now())
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());             // endTime = rtc.now() inside loop()
    // enterBurstDeepSleep - sleep branch (now < endTime + SLEEP_SECONDS - 1)
    mockReg.input.rtcNowQueue.push(endTime.unixtime());             // second rtc.now() inside enterBurstDeepSleep
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());           // timeAtBurstSwitch = rtc.now()
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());           // resetTimer(false)
    mockReg.input.analogReadValue = 512;
    loop();
    // Burst sync succeeded (counter reset to 0); resetTimer(false) was called manually so no increment
    TEST_ASSERT_EQUAL_UINT8(0, secondsSinceFlush);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.powerDownCalled);
}

// After burst sleep wakeup, Timer1 and resetTimerInterrupt are re-armed.
// Multi-sample burst mode needs Timer1 sampling and the 1 Hz RTC ISR restored after deep sleep.
static void test_burst_sleep_re_arms_timer1_and_isr() {
    sm_setUp();
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());   // now inside enterBurstDeepSleep
    mockReg.input.rtcNowQueue.push(afterWake.unixtime()); // timeAtBurstSwitch
    mockReg.input.rtcNowQueue.push(afterWake.unixtime()); // resetTimer
    mockReg.input.analogReadValue = 512;
    enterBurstDeepSleep(endTime);

    // Timer1.restart() and Timer1.attachInterrupt() should have been called
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.timer1RestartCount);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.timer1AttachCount);
    // resetTimerInterrupt attached
    bool foundISR = false;
    for (const auto& a : mockReg.audit.attachISRCalls)
        if (a.isr == resetTimerInterrupt) { foundISR = true; break; }
    TEST_ASSERT_TRUE(foundISR);
}

// Failed burst flush preserves secondsSinceFlush; manual resetTimer(false) does not increment.
// A failed end-of-window sync must not falsely reset the flush timer, and post-sleep resetTimer(false) is not a heartbeat tick.
static void test_burst_flush_failure_does_not_reset_counter() {
    sm_setUp();
    elapsed = TimeSpan((int32_t)WRITE_SECONDS + 1);
    secondsSinceFlush = 3;
    mockReg.input.syncResults.push(false);               // burst flush fails
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true);            // recovery
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    mockReg.input.rtcNowQueue.push(endTime.unixtime());
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.analogReadValue = 512;
    loop();
    // Sync failed so counter was NOT zeroed; resetTimer(false) called manually so no increment
    TEST_ASSERT_EQUAL_UINT8(3, secondsSinceFlush);
}

// resetTimer updates elapsed from timeAtBurstSwitch to rtc.now().
// elapsed drives the burst window guard; it must reflect wall time since the last sleep/wake transition.
static void test_reset_timer_computes_elapsed() {
    sm_setUp();
    timeAtBurstSwitch = DateTime(2026, 5, 4, 10, 0, 0);
    DateTime now(2026, 5, 4, 10, 0, 7);
    mockReg.input.rtcNowQueue.push(now.unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    TEST_ASSERT_EQUAL_INT32(7, elapsed.totalseconds());
}

// Default flash count fits within the first burst sampling window.
// LED warmup toggles must complete before the first burst window closes, or the firmware falls back to a single pulse.
static void test_configure_led_warmup_burst_multi_fits() {
    sm_setUp();
    // With WRITE_SECONDS=1, SAMPLE_FREQ=16: availableReadings=16
    // LED_WARMUP_DEFAULT_FLASHES=6; 6*2=12 <= 16 → no halving needed
    configureLedWarmupIndicator();
    TEST_ASSERT_FALSE(ledWarmupManualPulsePending);
    TEST_ASSERT_TRUE(ledWarmupToggleTarget > 0);
}

// Burst multi config clears alarm 1 inside resetTimer().
// Unlike one-sample burst, multi-sample mode re-arms the per-second RTC alarm on each resetTimer() call.
static void test_reset_timer_does_not_clear_alarm_in_burst_one_config() {
    // In burst multi (not one-sample), clearAlarm(1) IS called
    sm_setUp();
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    bool found = false;
    for (int c : mockReg.audit.clearAlarmCalls) if (c == 1) { found = true; break; }
    TEST_ASSERT_TRUE(found);
}

#endif // BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE && !DELAY_START

void register_tests_burst_multi() {
#if BURST_SAMPLING && !BURST_SAMPLING_ONE_SAMPLE && !DELAY_START
    RUN_TEST(test_burst_write_window_active_allows_sampling);
    RUN_TEST(test_burst_window_boundary_equal_no_sleep);
    RUN_TEST(test_burst_window_expired_triggers_flush_and_sleep);
    RUN_TEST(test_burst_sleep_re_arms_timer1_and_isr);
    RUN_TEST(test_burst_flush_failure_does_not_reset_counter);
    RUN_TEST(test_reset_timer_computes_elapsed);
    RUN_TEST(test_configure_led_warmup_burst_multi_fits);
    RUN_TEST(test_reset_timer_does_not_clear_alarm_in_burst_one_config);
#endif
}
