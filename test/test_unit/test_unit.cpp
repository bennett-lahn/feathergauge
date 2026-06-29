#include <unity.h>
#include "feathergauge_code.h"

// Pure-function unit tests: no firmware globals, minimal mocking.
// Covers helpers extracted from the .ino for host-side verification without hardware.

void setUp()    { mockResetAll(); }
void tearDown() {}

// ===========================
// calculateBatteryVoltage - sweep all 1024 ADC codes
// ===========================

// ADC code 0 maps to 0 V.
// Baseline check for the fixed-point battery scaling formula at minimum ADC input.
void test_battery_voltage_zero_adc() {
    TEST_ASSERT_EQUAL_INT16(0, calculateBatteryVoltage(0));
}

void test_battery_voltage_full_scale() {
    // At 1023, result should be close to REFERENCE_VOLTAGE * BATTERY_VOLTAGE_MULTIPLIER * 100
    // (3.3 * 2 * 100 = 660, but 1023/1023 of that)
    int16_t v = calculateBatteryVoltage(1023);
    // Allow ±1 for fixed-point rounding
    TEST_ASSERT_INT16_WITHIN(1, 659, v);
}

void test_battery_voltage_midscale() {
    int16_t v = calculateBatteryVoltage(512);
    // Expected ≈ 660 * 512/1023 ≈ 330
    TEST_ASSERT_INT16_WITHIN(2, 330, v);
}

void test_battery_voltage_rounding_boundary() {
    // Verify the +32768 rounding does not overflow int32_t for worst-case product
    // worst case: 1023 * BATT_FAST_MULT.  BATT_FAST_MULT ≈ 21261. 1023*21261 = ~21750000, well within int32_t
    // No assertion needed other than "does not crash" - but verify result is non-negative
    int16_t v = calculateBatteryVoltage(1023);
    TEST_ASSERT_TRUE(v > 0);
}

void test_battery_voltage_all_adc_codes() {
    // Sweep all 1024 codes and verify result matches the reference float formula
    for (int32_t adc = 0; adc <= 1023; adc++) {
        float expected_f = ((float)adc * 3.3f * 2.0f * 100.0f) / 1023.0f;
        int16_t expected = (int16_t)(expected_f + 0.5f);
        int16_t actual   = calculateBatteryVoltage(adc);
        TEST_ASSERT_INT16_WITHIN(1, expected, actual);
    }
}

// ===========================
// appendPadded2 - exhaustive 0..99
// ===========================

void test_appendPadded2_exhaustive() {
    for (int val = 0; val <= 99; val++) {
        char buf[4] = {0};
        char* ptr = buf;
        appendPadded2(ptr, val);
        TEST_ASSERT_EQUAL_INT(2, (int)(ptr - buf));
        int tens = (val / 10) % 10;
        int ones = val % 10;
        TEST_ASSERT_EQUAL_CHAR('0' + tens, buf[0]);
        TEST_ASSERT_EQUAL_CHAR('0' + ones, buf[1]);
    }
}

// ===========================
// appendPadded34 - exhaustive 0..1999
// ===========================

void test_appendPadded34_three_digit_range() {
    for (uint16_t val = 0; val <= 999; val++) {
        char buf[8] = {0};
        char* ptr = buf;
        appendPadded34(ptr, val);
        TEST_ASSERT_EQUAL_INT(3, (int)(ptr - buf));
        TEST_ASSERT_EQUAL_CHAR('0' + (val / 100) % 10, buf[0]);
        TEST_ASSERT_EQUAL_CHAR('0' + (val / 10)  % 10, buf[1]);
        TEST_ASSERT_EQUAL_CHAR('0' + (val % 10),        buf[2]);
    }
}

void test_appendPadded34_four_digit_range() {
    for (uint16_t val = 1000; val <= 1999; val++) {
        char buf[8] = {0};
        char* ptr = buf;
        appendPadded34(ptr, val);
        TEST_ASSERT_EQUAL_INT(4, (int)(ptr - buf));
        TEST_ASSERT_EQUAL_CHAR('0' + (val / 1000) % 10, buf[0]);
        TEST_ASSERT_EQUAL_CHAR('0' + (val / 100)  % 10, buf[1]);
        TEST_ASSERT_EQUAL_CHAR('0' + (val / 10)   % 10, buf[2]);
        TEST_ASSERT_EQUAL_CHAR('0' + (val % 10),         buf[3]);
    }
}

void test_appendPadded34_boundary_999_1000() {
    char buf999[8] = {0};  char* p999 = buf999;  appendPadded34(p999, 999);
    char buf1000[8] = {0}; char* p1000 = buf1000; appendPadded34(p1000, 1000);
    TEST_ASSERT_EQUAL_INT(3, (int)(p999  - buf999));
    TEST_ASSERT_EQUAL_INT(4, (int)(p1000 - buf1000));
}

// ===========================
// appendInt / append32Int
// ===========================

void test_appendInt_zero() {
    char buf[16] = {0}; char* p = buf;
    appendInt(p, 0);
    TEST_ASSERT_EQUAL_STRING("0", buf);
}

void test_appendInt_single_digit() {
    char buf[16] = {0}; char* p = buf;
    appendInt(p, 9);
    TEST_ASSERT_EQUAL_STRING("9", buf);
}

void test_appendInt_large_positive() {
    char buf[16] = {0}; char* p = buf;
    appendInt(p, 2026);
    TEST_ASSERT_EQUAL_STRING("2026", buf);
}

void test_append32Int_negative() {
    char buf[16] = {0}; char* p = buf;
    append32Int(p, -1234);
    TEST_ASSERT_EQUAL_STRING("-1234", buf);
}

void test_append32Int_int32_min() {
    char buf[16] = {0}; char* p = buf;
    append32Int(p, (int32_t)-2147483647L - 1);
    TEST_ASSERT_EQUAL_STRING("-2147483648", buf);
}

void test_append32Int_large_pressure() {
    char buf[16] = {0}; char* p = buf;
    append32Int(p, 101325L);
    TEST_ASSERT_EQUAL_STRING("101325", buf);
}

// ===========================
// normalizeMillisec
// ===========================

void test_normalize_below_1000() {
    TEST_ASSERT_EQUAL_UINT16(0,   normalizeMillisec(0));
    TEST_ASSERT_EQUAL_UINT16(500, normalizeMillisec(500));
    TEST_ASSERT_EQUAL_UINT16(999, normalizeMillisec(999));
}

void test_normalize_exactly_1000_becomes_999() {
    TEST_ASSERT_EQUAL_UINT16(999, normalizeMillisec(1000));
}

void test_normalize_above_1000_untouched() {
    TEST_ASSERT_EQUAL_UINT16(1001, normalizeMillisec(1001));
    TEST_ASSERT_EQUAL_UINT16(1500, normalizeMillisec(1500));
    TEST_ASSERT_EQUAL_UINT16(65535, normalizeMillisec(65535));
}

// ===========================
// computeWarmupFlashCount
// ===========================

void test_warmup_flash_count_enough_readings() {
    // 16 Hz * 1 s = 16 readings, default 6 flashes * 2 = 12 toggles <= 16 → no halving
    bool useManual = false;
    uint8_t count = computeWarmupFlashCount(16, useManual);
    TEST_ASSERT_EQUAL_UINT8(6, count);
    TEST_ASSERT_FALSE(useManual);
}

void test_warmup_flash_count_halving_once() {
    // 10 readings, 6*2=12 > 10, halve to 3; 3*2=6 <= 10 → stop
    bool useManual = false;
    uint8_t count = computeWarmupFlashCount(10, useManual);
    TEST_ASSERT_EQUAL_UINT8(3, count);
    TEST_ASSERT_FALSE(useManual);
}

void test_warmup_flash_count_halving_to_manual() {
    // 1 reading available: even 1*2=2 > 1, so useManualPulse=true
    bool useManual = false;
    computeWarmupFlashCount(1, useManual);
    TEST_ASSERT_TRUE(useManual);
}

void test_warmup_flash_count_zero_readings() {
    bool useManual = false;
    computeWarmupFlashCount(0, useManual);
    TEST_ASSERT_TRUE(useManual);
}

// ===========================
// formatDataRow
// ===========================

static void assertRowContains(const char* row, size_t len, const char* expected) {
    // Simple substring search
    bool found = false;
    size_t elen = strlen(expected);
    for (size_t i = 0; i + elen <= len; i++) {
        if (memcmp(row + i, expected, elen) == 0) { found = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(found, expected);
}

void test_format_row_ends_with_crlf() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 500, 10132, 2000, 330);
    TEST_ASSERT_TRUE(len >= 2);
    TEST_ASSERT_EQUAL_CHAR('\r', buf[len - 2]);
    TEST_ASSERT_EQUAL_CHAR('\n', buf[len - 1]);
}

void test_format_row_within_buffer_size() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 12, 31, 23, 59, 59);
    size_t len = formatDataRow(buf, dt, 999, 999999, -9999, 660);
    TEST_ASSERT_TRUE(len <= ROW_BUFFER_SIZE);
}

void test_format_row_positive_temperature() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 0, 10010, 2050, 330);
    assertRowContains(buf, len, "20.50");
}

void test_format_row_negative_temperature() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 1, 1, 0, 0, 0);
    size_t len = formatDataRow(buf, dt, 0, 10010, -150, 300);
    assertRowContains(buf, len, "-1.50");
}

void test_format_row_pressure_decimal() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    // pressure = 10137 units of 0.1 mbar → "1013.7"
    size_t len = formatDataRow(buf, dt, 0, 10137, 2000, 330);
    assertRowContains(buf, len, "1013.7");
}

void test_format_row_millisec_padding_zero() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 0, 10010, 2000, 330);
    assertRowContains(buf, len, ".000,");
}

void test_format_row_millisec_padding_nine() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 9, 10010, 2000, 330);
    assertRowContains(buf, len, ".009,");
}

void test_format_row_millisec_padding_99() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 99, 10010, 2000, 330);
    assertRowContains(buf, len, ".099,");
}

void test_format_row_millisec_padding_999() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 999, 10010, 2000, 330);
    assertRowContains(buf, len, ".999,");
}

void test_format_row_millisec_overrun_1001() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    size_t len = formatDataRow(buf, dt, 1001, 10010, 2000, 330);
    // overrun value passed through unchanged (1001 → 4 digits)
    assertRowContains(buf, len, ".1001,");
}

void test_format_row_date_format() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 3, 7, 8, 5, 9);
    size_t len = formatDataRow(buf, dt, 0, 10010, 2000, 330);
    // Date: YYYY/M/D (no leading zeros on month/day in appendInt)
    assertRowContains(buf, len, "2026/3/7");
}

void test_format_row_time_format() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 3, 7, 8, 5, 9);
    size_t len = formatDataRow(buf, dt, 0, 10010, 2000, 330);
    // Time: H:MM:SS.mmm (minute and second zero-padded)
    assertRowContains(buf, len, "8:05:09.000");
}

void test_format_row_voltage() {
    char buf[ROW_BUFFER_SIZE];
    DateTime dt(2026, 5, 4, 10, 30, 0);
    // batteryVoltage = 370 units of 0.01V → "3.70"
    size_t len = formatDataRow(buf, dt, 0, 10010, 2000, 370);
    assertRowContains(buf, len, "3.70");
}

// ===========================
// DateTime arithmetic (exercise ported math)
// ===========================

void test_datetime_unix_roundtrip() {
    DateTime dt(2026, 5, 4, 10, 30, 15);
    uint32_t u = dt.unixtime();
    DateTime dt2(u);
    TEST_ASSERT_EQUAL_UINT16(2026, dt2.year());
    TEST_ASSERT_EQUAL_UINT8(5,    dt2.month());
    TEST_ASSERT_EQUAL_UINT8(4,    dt2.day());
    TEST_ASSERT_EQUAL_UINT8(10,   dt2.hour());
    TEST_ASSERT_EQUAL_UINT8(30,   dt2.minute());
    TEST_ASSERT_EQUAL_UINT8(15,   dt2.second());
}

void test_datetime_add_timespan_crosses_midnight() {
    DateTime dt(2026, 5, 4, 23, 59, 50);
    DateTime dt2 = dt + TimeSpan(20);
    TEST_ASSERT_EQUAL_UINT8(5, dt2.day());
    TEST_ASSERT_EQUAL_UINT8(0, dt2.hour());
    TEST_ASSERT_EQUAL_UINT8(0, dt2.minute());
    TEST_ASSERT_EQUAL_UINT8(10, dt2.second());
}

void test_datetime_leap_year_feb28_to_mar1() {
    DateTime dt(2028, 2, 28, 23, 59, 59);
    DateTime dt2 = dt + TimeSpan(1);
    TEST_ASSERT_EQUAL_UINT8(2, dt2.month());
    TEST_ASSERT_EQUAL_UINT8(29, dt2.day());
}

void test_datetime_subtract_gives_timespan() {
    DateTime a(2026, 5, 4, 10, 0, 0);
    DateTime b(2026, 5, 4, 10, 0, 10);
    TimeSpan ts = b - a;
    TEST_ASSERT_EQUAL_INT32(10, ts.totalseconds());
}

// ===========================
// main
// ===========================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_battery_voltage_zero_adc);
    RUN_TEST(test_battery_voltage_full_scale);
    RUN_TEST(test_battery_voltage_midscale);
    RUN_TEST(test_battery_voltage_rounding_boundary);
    RUN_TEST(test_battery_voltage_all_adc_codes);

    RUN_TEST(test_appendPadded2_exhaustive);
    RUN_TEST(test_appendPadded34_three_digit_range);
    RUN_TEST(test_appendPadded34_four_digit_range);
    RUN_TEST(test_appendPadded34_boundary_999_1000);

    RUN_TEST(test_appendInt_zero);
    RUN_TEST(test_appendInt_single_digit);
    RUN_TEST(test_appendInt_large_positive);
    RUN_TEST(test_append32Int_negative);
    RUN_TEST(test_append32Int_int32_min);
    RUN_TEST(test_append32Int_large_pressure);

    RUN_TEST(test_normalize_below_1000);
    RUN_TEST(test_normalize_exactly_1000_becomes_999);
    RUN_TEST(test_normalize_above_1000_untouched);

    RUN_TEST(test_warmup_flash_count_enough_readings);
    RUN_TEST(test_warmup_flash_count_halving_once);
    RUN_TEST(test_warmup_flash_count_halving_to_manual);
    RUN_TEST(test_warmup_flash_count_zero_readings);

    RUN_TEST(test_format_row_ends_with_crlf);
    RUN_TEST(test_format_row_within_buffer_size);
    RUN_TEST(test_format_row_positive_temperature);
    RUN_TEST(test_format_row_negative_temperature);
    RUN_TEST(test_format_row_pressure_decimal);
    RUN_TEST(test_format_row_millisec_padding_zero);
    RUN_TEST(test_format_row_millisec_padding_nine);
    RUN_TEST(test_format_row_millisec_padding_99);
    RUN_TEST(test_format_row_millisec_padding_999);
    RUN_TEST(test_format_row_millisec_overrun_1001);
    RUN_TEST(test_format_row_date_format);
    RUN_TEST(test_format_row_time_format);
    RUN_TEST(test_format_row_voltage);

    RUN_TEST(test_datetime_unix_roundtrip);
    RUN_TEST(test_datetime_add_timespan_crosses_midnight);
    RUN_TEST(test_datetime_leap_year_feb28_to_mar1);
    RUN_TEST(test_datetime_subtract_gives_timespan);

    return UNITY_END();
}
