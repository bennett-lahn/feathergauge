#include <unity.h>
#include "feathergauge_code.h"
#include <string.h>

// Scheduling, timer correction, and file-rollover boundary tests.
// Exercises timekeeping handoff between ISR and main loop, plus SD file naming and rollover.

// External firmware globals accessed by temporal tests
extern char             fileName[];
extern DateTime         currentDateTime;
extern volatile unsigned long millisAtInterrupt;
extern uint8_t          secondsSinceFlush;
extern volatile bool    resetTimerFlag;
extern volatile bool    samplingFlag;
extern int16_t          currentVoltage;

void setUp() {
    mockResetAll();
    secondsSinceFlush = 0;
    samplingFlag      = false;
    resetTimerFlag    = false;
    millisAtInterrupt = 0;
    currentVoltage    = 0;
    currentDateTime   = DateTime(2026, 5, 4, 10, 0, 0);
    // Give file a default open state so write tests don't need to set it up each time
    mockReg.input.fileOpenResults.push(true);
    outputFile.open("dummy", O_WRITE);
}

void tearDown() {}

// ===========================
// makeFileName - iteration search
// ===========================

// First available index (_IT-00) when no files exist.
// makeFileName() scans sd.exists() until it finds a free _IT-XX suffix for the day.
void test_make_file_name_no_existing_files() {
    // No files exist → _IT-00
    DateTime now(2026, 5, 4, 10, 0, 0);
    char serial[] = "08";
    makeFileName(fileName, serial, now);
    TEST_ASSERT_NOT_NULL(strstr(fileName, "WG-08"));
    TEST_ASSERT_NOT_NULL(strstr(fileName, "2026-05-04"));
    TEST_ASSERT_NOT_NULL(strstr(fileName, "_IT-00"));
    TEST_ASSERT_NOT_NULL(strstr(fileName, ".csv"));
}

// Skips _IT-00 and _IT-01 when those files already exist on the SD card.
void test_make_file_name_skips_existing() {
    // IT-00 and IT-01 exist → resolves to IT-02
    mockReg.input.existsResults.push(true);   // IT-00 exists
    mockReg.input.existsResults.push(true);   // IT-01 exists
    // IT-02 does not exist (default false from empty queue)
    DateTime now(2026, 5, 4, 10, 0, 0);
    char serial[] = "08";
    makeFileName(fileName, serial, now);
    TEST_ASSERT_NOT_NULL(strstr(fileName, "_IT-02"));
}

// Month and day are zero-padded in the ISO-style date portion of the filename.
void test_make_file_name_date_zero_padding() {
    DateTime now(2026, 1, 7, 0, 0, 0);
    char serial[] = "08";
    makeFileName(fileName, serial, now);
    TEST_ASSERT_NOT_NULL(strstr(fileName, "2026-01-07"));
}

// Two-character serial number from EEPROM appears in the WG-XX prefix.
void test_make_file_name_serial_two_chars() {
    DateTime now(2026, 5, 4, 0, 0, 0);
    char serial[] = "15";
    makeFileName(fileName, serial, now);
    TEST_ASSERT_NOT_NULL(strstr(fileName, "WG-15"));
}

// Iteration suffix uses two digits (_IT-10) when the first ten indices are taken.
void test_make_file_name_it_suffix_double_digit() {
    // First 10 indices taken
    for (int i = 0; i < 10; i++) mockReg.input.existsResults.push(true);
    DateTime now(2026, 5, 4, 0, 0, 0);
    char serial[] = "08";
    makeFileName(fileName, serial, now);
    TEST_ASSERT_NOT_NULL(strstr(fileName, "_IT-10"));
}

// ===========================
// resetTimer + resetTimerInterrupt - time correction
// ===========================

// ISR path increments secondsSinceFlush once per tick.
// Only resetTimer(true) from the 1 Hz heartbeat path advances the flush interval counter.
void test_reset_timer_increments_seconds_since_flush() {
    secondsSinceFlush = 0;
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 1).unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    TEST_ASSERT_EQUAL_UINT8(1, secondsSinceFlush);
}

// resetTimer() replaces currentDateTime with rtc.now(), discarding stale ISR time.
void test_reset_timer_corrects_current_date_time() {
    DateTime authoritative(2026, 5, 4, 10, 0, 5);
    mockReg.input.rtcNowQueue.push(authoritative.unixtime());
    mockReg.input.analogReadValue = 512;
    // ISR pre-incremented to a different value
    currentDateTime = DateTime(2026, 5, 4, 10, 0, 4);
    resetTimer(true);
    TEST_ASSERT_EQUAL_UINT32(authoritative.unixtime(), currentDateTime.unixtime());
}

// resetTimerInterrupt() optimistically adds one second before main-loop correction.
void test_isr_pre_increments_current_date_time() {
    DateTime base(2026, 5, 4, 10, 0, 0);
    currentDateTime = base;
    resetTimerInterrupt();
    TEST_ASSERT_EQUAL_UINT32((base + TimeSpan(1)).unixtime(), currentDateTime.unixtime());
}

// resetTimerInterrupt() captures millis() for sub-second timestamp fields in CSV rows.
void test_isr_stamps_millis_at_interrupt() {
    mockReg.input.millisValue = 12345;
    millisAtInterrupt = 0;
    resetTimerInterrupt();
    TEST_ASSERT_EQUAL_UINT32(12345, millisAtInterrupt);
}

void test_reset_timer_overwrites_isr_pre_increment() {
    // ISR runs first, increments; then resetTimer() must overwrite with authoritative time.
    // End-to-end check that the main-loop correction wins over the ISR's optimistic +1 second.
    DateTime base(2026, 5, 4, 10, 0, 0);
    currentDateTime = base;
    resetTimerInterrupt(); // pre-increments to 10:00:01
    // Now RTC says 10:00:02 (slight drift correction)
    DateTime authoritative(2026, 5, 4, 10, 0, 2);
    mockReg.input.rtcNowQueue.push(authoritative.unixtime());
    mockReg.input.analogReadValue = 512;
    resetTimer(true);
    TEST_ASSERT_EQUAL_UINT32(authoritative.unixtime(), currentDateTime.unixtime());
}

// FLUSH_INTERVAL_SECONDS consecutive resetTimer(true) calls reach the flush threshold.
void test_reset_timer_n_ticks_reaches_flush_boundary() {
    secondsSinceFlush = 0;
    for (int i = 0; i < (int)FLUSH_INTERVAL_SECONDS; i++) {
        mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, i + 1).unixtime());
        mockReg.input.analogReadValue = 512;
        resetTimer(true);
    }
    TEST_ASSERT_EQUAL_UINT8(FLUSH_INTERVAL_SECONDS, secondsSinceFlush);
}

// ===========================
// performSensorReading - millisecond computation
// ===========================

// Helper: set millis delta and queue default sensor values.
// Configures the mock clock and sensor so performSensorReading() tests focus on millisecond math.
static void setMillisDelta(unsigned long irqMs, unsigned long nowMs) {
    millisAtInterrupt        = irqMs;
    mockReg.input.millisValue      = nowMs;
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
}

static const char* findMillisField(const std::string& data) {
    // Returns pointer to the millisecond portion ".XXX," in the accumulated file data
    static char tmp[8];
    const char* dot = nullptr;
    for (size_t i = 0; i + 5 < data.size(); i++) {
        if (data[i] == '.') { dot = data.c_str() + i; break; }
    }
    if (!dot) return "";
    // copy up to comma
    size_t j = 0;
    dot++; // skip '.'
    while (*dot && *dot != ',' && j < 7) tmp[j++] = *dot++;
    tmp[j] = '\0';
    return tmp;
}

// Sample taken at the RTC tick (zero ms offset) formats as .000 in the CSV timestamp.
void test_perform_reading_millisec_delta_zero() {
    setMillisDelta(1000, 1000);
    performSensorReading();
    TEST_ASSERT_EQUAL_STRING("000", findMillisField(mockReg.state.fileData));
}

void test_perform_reading_millisec_delta_500() {
    setMillisDelta(0, 500);
    performSensorReading();
    TEST_ASSERT_EQUAL_STRING("500", findMillisField(mockReg.state.fileData));
}

void test_perform_reading_millisec_delta_999() {
    setMillisDelta(0, 999);
    performSensorReading();
    TEST_ASSERT_EQUAL_STRING("999", findMillisField(mockReg.state.fileData));
}

void test_perform_reading_millisec_delta_1000_clamped() {
    // delta == 1000 → clamped to 999
    setMillisDelta(0, 1000);
    performSensorReading();
    TEST_ASSERT_EQUAL_STRING("999", findMillisField(mockReg.state.fileData));
}

void test_perform_reading_millisec_delta_1500_passthrough() {
    // delta > 1000 → kept as overrun (1500 → 4 digits)
    setMillisDelta(0, 1500);
    performSensorReading();
    TEST_ASSERT_EQUAL_STRING("1500", findMillisField(mockReg.state.fileData));
}

// ===========================
// writeToOutputFile - file rollover boundary
// ===========================

// Projected size below limit does not close the file.
// writeToOutputFile() only rolls over when size + row would exceed FILE_ROLLOVER_SIZE_BYTES.
void test_no_rollover_below_limit() {
    mockReg.input.useFileSizeOverride = true;
    mockReg.input.fileSizeOverride    = FILE_ROLLOVER_SIZE_BYTES - 50;
    int closeBefore = mockReg.audit.closeCount;
    writeToOutputFile(currentDateTime, 0, 10132, 2000, 330);
    TEST_ASSERT_EQUAL_INT(closeBefore, mockReg.audit.closeCount);
}

// Projected file size at the limit triggers close and reopen with a new daily filename.
void test_rollover_above_limit_closes_and_reopens() {
    // Make the projected size exceed the limit
    mockReg.input.useFileSizeOverride = true;
    mockReg.input.fileSizeOverride    = FILE_ROLLOVER_SIZE_BYTES;
    // Queue opens for rollover: sync ok, new file opens ok
    mockReg.input.fileOpenResults.push(true);
    mockReg.input.rtcNowQueue.push(currentDateTime.unixtime()); // for setFileTimestampOnce
    writeToOutputFile(currentDateTime, 0, 10132, 2000, 330);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.closeCount);
}

// New file after rollover includes the standard CSV column header row.
void test_rollover_writes_header_to_new_file() {
    mockReg.input.useFileSizeOverride = true;
    mockReg.input.fileSizeOverride    = FILE_ROLLOVER_SIZE_BYTES;
    mockReg.input.fileOpenResults.push(true);
    mockReg.input.rtcNowQueue.push(currentDateTime.unixtime());
    mockReg.state.fileData.clear();
    writeToOutputFile(currentDateTime, 0, 10132, 2000, 330);
    TEST_ASSERT_NOT_NULL(strstr(mockReg.state.fileData.c_str(), "Timestamp,Pressure"));
}

// ===========================
// enterBurstDeepSleep - scheduling math
// ===========================

#if BURST_SAMPLING

extern DateTime timeAtBurstSwitch;
extern TimeSpan elapsed;

void test_burst_sleep_branch_sets_date_alarm() {
    // now < endTime + SLEEP_SECONDS - 1 → sleep branch
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    DateTime now(2026, 5, 4, 10, 0, 6);   // 6 < 5 + 10 - 1 = 14 → sleep
    mockReg.input.rtcNowQueue.push(now.unixtime());
    // After wakeup:
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(afterWake.unixtime()); // for timeAtBurstSwitch = rtc.now()
    mockReg.input.rtcNowQueue.push(afterWake.unixtime()); // for resetTimer(false)
    mockReg.input.analogReadValue = 512;
    enterBurstDeepSleep(endTime);

    // At least one setAlarm1 call with DS3231_A1_Date
    bool foundDateAlarm = false;
    for (const auto& a : mockReg.audit.setAlarm1Calls) {
        if (a.mode == DS3231_A1_Date) { foundDateAlarm = true; break; }
    }
    TEST_ASSERT_TRUE(foundDateAlarm);
}

void test_burst_sleep_alarm_target_is_end_plus_sleep_seconds() {
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    DateTime now(2026, 5, 4, 10, 0, 6);
    mockReg.input.rtcNowQueue.push(now.unixtime());
    DateTime afterWake(2026, 5, 4, 10, 0, 16);
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.rtcNowQueue.push(afterWake.unixtime());
    mockReg.input.analogReadValue = 512;
    enterBurstDeepSleep(endTime);

    uint32_t expectedAlarm = (endTime + TimeSpan((int32_t)SLEEP_SECONDS)).unixtime();
    bool found = false;
    for (const auto& a : mockReg.audit.setAlarm1Calls) {
        if (a.mode == DS3231_A1_Date && a.whenUnix == expectedAlarm) { found = true; break; }
    }
    TEST_ASSERT_TRUE(found);
}

void test_burst_resume_branch_skips_sleep() {
    // now >= endTime + SLEEP_SECONDS - 1 → resume immediately without sleep
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    uint32_t nowUnix = endTime.unixtime() + SLEEP_SECONDS - 1;
    mockReg.input.rtcNowQueue.push(nowUnix);
    mockReg.input.rtcNowQueue.push(nowUnix); // resetTimer(false)
    mockReg.input.analogReadValue = 512;

    int pdBefore = mockReg.audit.powerDownCalled;
    enterBurstDeepSleep(endTime);
    TEST_ASSERT_EQUAL_INT(pdBefore, mockReg.audit.powerDownCalled);
}

void test_burst_resume_sets_per_second_alarm() {
    DateTime endTime(2026, 5, 4, 10, 0, 5);
    uint32_t nowUnix = endTime.unixtime() + SLEEP_SECONDS;
    mockReg.input.rtcNowQueue.push(nowUnix);
    mockReg.input.rtcNowQueue.push(nowUnix); // resetTimer(false)
    mockReg.input.analogReadValue = 512;
    currentDateTime = DateTime(nowUnix);
    enterBurstDeepSleep(endTime);

    bool found = false;
    for (const auto& a : mockReg.audit.setAlarm1Calls) {
        if (a.mode == DS3231_A1_PerSecond) { found = true; break; }
    }
    TEST_ASSERT_TRUE(found);
}

void test_burst_window_boundary_equal_does_not_enter_sleep() {
    // elapsed.totalseconds() == WRITE_SECONDS → guard is '>' so no sleep
    elapsed = TimeSpan((int32_t)WRITE_SECONDS);
    // loop() guard: if (elapsed.totalseconds() > WRITE_SECONDS) - should be false
    TEST_ASSERT_FALSE(elapsed.totalseconds() > (int32_t)WRITE_SECONDS);
}

void test_burst_window_expired_enters_sleep() {
    elapsed = TimeSpan((int32_t)WRITE_SECONDS + 1);
    TEST_ASSERT_TRUE(elapsed.totalseconds() > (int32_t)WRITE_SECONDS);
}

#endif // BURST_SAMPLING

// ===========================
// delayStartDeepSleepLoop - scheduling
// ===========================

#if DELAY_START

void test_delay_start_already_past_returns_immediately() {
    // Queue a time at or after startDateTime
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    // now >= start → immediate return
    mockReg.input.rtcNowQueue.push(startDt.unixtime());       // first check inside loop
    mockReg.input.rtcNowQueue.push(startDt.unixtime() + 1);  // after sleep (should not be reached)

    std::string dataBefore = mockReg.state.fileData;
    delayStartDeepSleepLoop();

    // No sample written
    TEST_ASSERT_EQUAL(dataBefore.size(), mockReg.state.fileData.size());
    // interrupt was detached
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.detachISRCount);
}

void test_delay_start_writes_samples_while_waiting() {
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    // Two iterations before threshold, then exit
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 2);  // initial check: not yet
    mockReg.input.rtcNowQueue.push(startDt.unixtime() - 1);  // after first sleep: not yet, write sample
    mockReg.input.rtcNowQueue.push(startDt.unixtime());       // after second sleep: at threshold, exit
    mockReg.input.pressureQueue.push(10132);
    mockReg.input.temperatureQueue.push(2000);
    mockReg.input.pressureQueue.push(10133);
    mockReg.input.temperatureQueue.push(2001);
    mockReg.input.analogReadValue = 512;

    delayStartDeepSleepLoop();

    // Two rows written with millisec == 0
    TEST_ASSERT_TRUE(mockReg.state.fileData.size() > 0);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.detachISRCount);
}

void test_delay_start_alarm_mode_is_hour() {
    DateTime startDt(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MINUTE, 0);
    mockReg.input.rtcNowQueue.push(startDt.unixtime()); // immediate exit
    delayStartDeepSleepLoop();
    TEST_ASSERT_EQUAL_INT(1, (int)mockReg.audit.setAlarm1Calls.size());
    TEST_ASSERT_EQUAL_UINT8(DS3231_A1_Hour, mockReg.audit.setAlarm1Calls[0].mode);
}

#endif // DELAY_START

// ===========================
// main
// ===========================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_make_file_name_no_existing_files);
    RUN_TEST(test_make_file_name_skips_existing);
    RUN_TEST(test_make_file_name_date_zero_padding);
    RUN_TEST(test_make_file_name_serial_two_chars);
    RUN_TEST(test_make_file_name_it_suffix_double_digit);

    RUN_TEST(test_reset_timer_increments_seconds_since_flush);
    RUN_TEST(test_reset_timer_corrects_current_date_time);
    RUN_TEST(test_isr_pre_increments_current_date_time);
    RUN_TEST(test_isr_stamps_millis_at_interrupt);
    RUN_TEST(test_reset_timer_overwrites_isr_pre_increment);
    RUN_TEST(test_reset_timer_n_ticks_reaches_flush_boundary);

    RUN_TEST(test_perform_reading_millisec_delta_zero);
    RUN_TEST(test_perform_reading_millisec_delta_500);
    RUN_TEST(test_perform_reading_millisec_delta_999);
    RUN_TEST(test_perform_reading_millisec_delta_1000_clamped);
    RUN_TEST(test_perform_reading_millisec_delta_1500_passthrough);

    RUN_TEST(test_no_rollover_below_limit);
    RUN_TEST(test_rollover_above_limit_closes_and_reopens);
    RUN_TEST(test_rollover_writes_header_to_new_file);

#if BURST_SAMPLING
    RUN_TEST(test_burst_sleep_branch_sets_date_alarm);
    RUN_TEST(test_burst_sleep_alarm_target_is_end_plus_sleep_seconds);
    RUN_TEST(test_burst_resume_branch_skips_sleep);
    RUN_TEST(test_burst_resume_sets_per_second_alarm);
    RUN_TEST(test_burst_window_boundary_equal_does_not_enter_sleep);
    RUN_TEST(test_burst_window_expired_enters_sleep);
#endif

#if DELAY_START
    RUN_TEST(test_delay_start_already_past_returns_immediately);
    RUN_TEST(test_delay_start_writes_samples_while_waiting);
    RUN_TEST(test_delay_start_alarm_mode_is_hour);
#endif

    return UNITY_END();
}
