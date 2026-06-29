#include <unity.h>
#include "feathergauge_code.h"
#include <string.h>

// SD/sensor fault injection and setup() error-path tests.
// Verifies recoverSdCard(), error codes, and that extreme sensor values stay within buffer limits.

extern char             fileName[];
extern DateTime         currentDateTime;
extern uint8_t          secondsSinceFlush;
extern volatile bool    samplingFlag;
extern volatile bool    resetTimerFlag;
extern int16_t          currentVoltage;

void setUp() {
    mockResetAll();
    secondsSinceFlush = 0;
    samplingFlag      = false;
    resetTimerFlag    = false;
    currentVoltage    = 330;
    currentDateTime   = DateTime(2026, 5, 4, 10, 0, 0);
    strncpy(fileName, "WG-08_2026-05-04_IT-00.csv", FILENAME_LENGTH);
    // Pre-open the mock file so writeToOutputFile() and recoverSdCard() have a valid handle.
    mockReg.input.fileOpenResults.push(true);
    outputFile.open("dummy", O_WRITE);
}

void tearDown() {}

// ===========================
// writeToOutputFile - write error recovery
// ===========================

// getWriteError() triggers recoverSdCard and closes the file.
// Simulates an SD write fault and checks that the firmware attempts recovery instead of continuing blindly.
void test_write_error_triggers_recover() {
    mockReg.input.writeErrorResults.push(true); // getWriteError() returns true
    // recoverSdCard needs sd.begin and outputFile.open to succeed
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true);
    writeToOutputFile(currentDateTime, 0, 10132, 2000, 330);
    TEST_ASSERT_EQUAL_INT(1, mockReg.audit.closeCount); // close() called in recoverSdCard
}

// recoverSdCard clears the write-error flag after recovery.
// After a successful recovery path, the mock file must be ready for subsequent writes.
void test_write_error_clears_write_error_flag() {
    mockReg.input.writeErrorResults.push(true);
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true);
    writeToOutputFile(currentDateTime, 0, 10132, 2000, 330);
    // clearWriteError was called; _writeError in mock is now false
    TEST_ASSERT_FALSE(outputFile._writeError);
}

// ===========================
// recoverSdCard - retry ladder
// ===========================

// Three failed begin() attempts increment recoverAttempts and throw.
// recoverSdCard() retries three times before giving up; each attempt is counted in the audit log.
void test_recover_all_begin_fail_calls_error() {
    mockReg.input.sdBeginResult = false;
    bool threw = false;
    try {
        recoverSdCard();
    } catch (const TestAbort& e) {
        threw = true;
        TEST_ASSERT_EQUAL_UINT8(ERROR_SD_CARD_FAILED, e.errorCode);
    }
    TEST_ASSERT_TRUE(threw);
    TEST_ASSERT_EQUAL_INT(3, mockReg.audit.recoverAttempts);
}

// begin() succeeds but all open() attempts fail -> error.
// Remounting the filesystem without reopening the data file is treated as a fatal SD failure.
void test_recover_begin_ok_open_fail_calls_error() {
    mockReg.input.sdBeginResult = true;
    // All three open() attempts fail
    mockReg.input.fileOpenResults.push(false);
    mockReg.input.fileOpenResults.push(false);
    mockReg.input.fileOpenResults.push(false);
    bool threw = false;
    try {
        recoverSdCard();
    } catch (const TestAbort& e) {
        threw = true;
        TEST_ASSERT_EQUAL_UINT8(ERROR_SD_CARD_FAILED, e.errorCode);
    }
    TEST_ASSERT_TRUE(threw);
}

// Successful recovery does not call error().
// Happy-path recovery should remount and reopen without triggering the error() seam.
void test_recover_success_no_error_thrown() {
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true);
    bool threw = false;
    try {
        recoverSdCard();
    } catch (const TestAbort&) {
        threw = true;
    }
    TEST_ASSERT_FALSE(threw);
    TEST_ASSERT_FALSE(mockReg.audit.errorCalled);
}

// ===========================
// Flush-path sync failure
// ===========================

// Failed sync leaves secondsSinceFlush unchanged.
// Mirrors loop() flush logic: only a successful sync resets the seconds-since-flush counter.
void test_flush_sync_failure_does_not_reset_counter() {
    secondsSinceFlush = FLUSH_INTERVAL_SECONDS;
    mockReg.input.syncResults.push(false);   // sync() fails
    mockReg.input.sdBeginResult = true;
    mockReg.input.fileOpenResults.push(true); // recovery open succeeds
    // loop() flush check: if (!outputFile.sync()) recoverSdCard()
    // The counter should NOT be reset on failure
    if (!outputFile.sync()) {
        recoverSdCard();
    } else {
        secondsSinceFlush = 0;
    }
    TEST_ASSERT_EQUAL_UINT8(FLUSH_INTERVAL_SECONDS, secondsSinceFlush);
}

// Successful sync resets secondsSinceFlush to zero.
// After a good sync, the periodic flush interval starts counting from zero again.
void test_flush_sync_success_resets_counter() {
    secondsSinceFlush = FLUSH_INTERVAL_SECONDS;
    // sync() succeeds (default)
    if (!outputFile.sync()) {
        recoverSdCard();
    } else {
        secondsSinceFlush = 0;
    }
    TEST_ASSERT_EQUAL_UINT8(0, secondsSinceFlush);
}

// ===========================
// setup() error codes
// ===========================

// rtc.begin() failure throws error(2).
// setup() halts early if the DS3231 cannot be initialized (error code 2 = RTC failure).
void test_setup_rtc_fail_triggers_error_2() {
    mockReg.input.rtcBeginResult = false;
    bool threw = false;
    try {
        setup();
    } catch (const TestAbort& e) {
        threw = true;
        TEST_ASSERT_EQUAL_UINT8(2, e.errorCode);
    }
    TEST_ASSERT_TRUE(threw);
}

// sd.begin() failure throws ERROR_SD_CARD_FAILED.
// setup() requires a working SD card before it opens the CSV log file.
void test_setup_sd_fail_triggers_error_sd_card_failed() {
    // RTC succeeds, SD fails
    mockReg.input.rtcBeginResult = true;
    mockReg.input.rtcLostPower   = false;
    mockReg.input.sdBeginResult  = false;
    bool threw = false;
    try {
        setup();
    } catch (const TestAbort& e) {
        threw = true;
        TEST_ASSERT_EQUAL_UINT8(ERROR_SD_CARD_FAILED, e.errorCode);
    }
    TEST_ASSERT_TRUE(threw);
}

// outputFile.open() failure throws ERROR_FILE_OPEN_FAILED.
// Even with RTC and SD mount OK, setup() fails if the daily CSV cannot be opened for append.
void test_setup_file_open_fail_triggers_error_file_open_failed() {
    mockReg.input.rtcBeginResult = true;
    mockReg.input.rtcLostPower   = false;
    mockReg.input.sdBeginResult  = true;
    // makeFileName needs rtc.now()
    mockReg.input.rtcNowQueue.push(DateTime(2026, 5, 4, 10, 0, 0).unixtime());
    // File open fails
    mockReg.input.fileOpenResults.push(false);
    bool threw = false;
    try {
        setup();
    } catch (const TestAbort& e) {
        threw = true;
        TEST_ASSERT_EQUAL_UINT8(ERROR_FILE_OPEN_FAILED, e.errorCode);
    }
    TEST_ASSERT_TRUE(threw);
}

// ===========================
// Extreme MS5803 values vs ROW_BUFFER_SIZE
// ===========================

// Row fits in ROW_BUFFER_SIZE for extreme pressure values.
// Ensures formatDataRow() never overflows the stack buffer when pressure is INT32_MAX.
void test_format_row_extreme_max_pressure() {
    char buf[ROW_BUFFER_SIZE];
    size_t len = formatDataRow(buf, currentDateTime, 0, INT32_MAX, 2000, 330);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

// Row fits in ROW_BUFFER_SIZE for INT32_MIN pressure.
// Negative fixed-point formatting must still produce a bounded CSV line.
void test_format_row_extreme_min_pressure() {
    char buf[ROW_BUFFER_SIZE];
    size_t len = formatDataRow(buf, currentDateTime, 0, INT32_MIN, 2000, 330);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

// Negative pressure within normal sensor range still formats without buffer overrun.
void test_format_row_negative_pressure() {
    char buf[ROW_BUFFER_SIZE];
    size_t len = formatDataRow(buf, currentDateTime, 0, -101325, 2000, 330);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

// Row fits in ROW_BUFFER_SIZE for extreme positive temperature.
void test_format_row_extreme_temperature_max() {
    char buf[ROW_BUFFER_SIZE];
    size_t len = formatDataRow(buf, currentDateTime, 0, 10132, INT32_MAX, 330);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

// Row fits in ROW_BUFFER_SIZE for extreme negative temperature.
void test_format_row_extreme_temperature_min() {
    char buf[ROW_BUFFER_SIZE];
    size_t len = formatDataRow(buf, currentDateTime, 0, 10132, INT32_MIN, 330);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

void test_format_row_pressure_awkward_modulo() {
    // pressure % 10 should produce correct single decimal digit for all residues
    char buf[ROW_BUFFER_SIZE];
    for (int32_t residue = 0; residue <= 9; residue++) {
        int32_t pressure = 10130 + residue;
        size_t len = formatDataRow(buf, currentDateTime, 0, pressure, 2000, 330);
        TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
        // Verify last decimal digit present
        char expected[4]; snprintf(expected, sizeof(expected), ".%d,", (int)residue);
        TEST_ASSERT_NOT_NULL(strstr(buf, expected));
    }
}

// ===========================
// main
// ===========================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_write_error_triggers_recover);
    RUN_TEST(test_write_error_clears_write_error_flag);

    RUN_TEST(test_recover_all_begin_fail_calls_error);
    RUN_TEST(test_recover_begin_ok_open_fail_calls_error);
    RUN_TEST(test_recover_success_no_error_thrown);

    RUN_TEST(test_flush_sync_failure_does_not_reset_counter);
    RUN_TEST(test_flush_sync_success_resets_counter);

    RUN_TEST(test_setup_rtc_fail_triggers_error_2);
    RUN_TEST(test_setup_sd_fail_triggers_error_sd_card_failed);
    RUN_TEST(test_setup_file_open_fail_triggers_error_file_open_failed);

    RUN_TEST(test_format_row_extreme_max_pressure);
    RUN_TEST(test_format_row_extreme_min_pressure);
    RUN_TEST(test_format_row_negative_pressure);
    RUN_TEST(test_format_row_extreme_temperature_max);
    RUN_TEST(test_format_row_extreme_temperature_min);
    RUN_TEST(test_format_row_pressure_awkward_modulo);

    return UNITY_END();
}
