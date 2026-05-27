// SD Write Performance Test
// Three back-to-back tests, each performing 100 timed flush events:
//
// Test 1 - Raw byte writes: calls file.write() with varying payload sizes
//          around 512 bytes (256/384/512/640/768 B cycling), then flushes.
//
// Test 2 - feathergauge imitation: replicates the exact outputFile.print()
//          sequence used by writeToOutputFile() in feathergauge_code.ino,
//          batching BUFFER_WRITE_COUNT (32) rows per flush, exactly as the
//          main loop does.
//
// Test 3 - Optimized feathergauge: same row content and flush cadence as
//          Test 2, but builds each row in a local char buffer with dtostrf()
//          + snprintf(), then issues a single file.write() call per row.
//
// Uses the same SD library and chip-select pin as feathergauge_code.ino.

// Coding challenge for next time:
// 1. Instead of relying on snprintf, use pointer arithmetic and itoa from avr-libc to manipulate the 
//    row buffer directly (snprintf too heavy for feathergauge)
// 2. Cache date/time string and only update it once every time a write occurs
// 3. Use fixed point arithmetic instead of floating point to simplify conversions
//    e.g. instead of storing 1013.25 as a float store it as 101325 and just divide by 100. 
//    In real feathergauge code, this will require modifying the MS5803 library to return int instead of float.

// Handle FAT32 4gb file size limit

// Architecture rework:
// Replace circular buffer with direct write to SdFat (more optimized SD), relying on the libraries internal buffer
// This would still have occassional 20-50ms latency spikes unless we pre-allocate a huge file and shrink it down or allocate rolling files
// rolling file allocation would still have 20-50ms spikes, just every 100mb.

// Also consider using input capture for timer 1 '

// "You connect the DS3231's 1Hz SQW pin to the Input Capture Pin (ICP1, which is Digital Pin 4 on the Feather 32u4)."
// Hmmm

// "The Magic: When the SQW pin triggers, the AVR hardware instantly copies the current value of the Timer 1 counter (TCNT1) into the Input Capture Register (ICR1)."
// "A note on Timer 1: Timer 1 is a 16-bit timer. At 1 tick per microsecond, it will overflow and reset to zero every 65.536 milliseconds. Since your RTC fires once per second, Timer 1 will overflow multiple times between RTC pulses. You will need to track these overflows (using the TIMER1_OVF_vect interrupt) to maintain a continuous, multi-second hardware timeline."

// I should check if this fixes the issue by itself, because otherwise it's not worth it to rewire all the wave gauges

#include <SPI.h>
#include <SD.h>

constexpr uint8_t  SD_CS_PIN          = 4;    // Same as feathergauge_code
constexpr uint8_t  LED_PIN            = 13;
constexpr uint16_t NUM_FLUSH_EVENTS   = 100;
constexpr uint32_t SERIAL_BAUD        = 57600;

// ---------------------------------------------------------------------------
// Test 1 constants
// ---------------------------------------------------------------------------

// Payload sizes (bytes) cycling across the 100 flush events.
// Chosen to bracket a single 512-byte SD sector.
const uint16_t WRITE_SIZES[] = {256, 384, 512, 640, 768};
constexpr uint8_t NUM_SIZES  = 5;

// 64-byte fill chunk; repeated to reach any target size without a large allocation.
const char FILL_CHUNK[]          = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz01";
constexpr uint8_t FILL_CHUNK_SIZE = 64;  // length of FILL_CHUNK (no null terminator counted)

// ---------------------------------------------------------------------------
// Test 2 constants - mirror feathergauge_code defines
// ---------------------------------------------------------------------------

// Number of rows written before each flush - matches BUFFER_WRITE_COUNT in feathergauge_code.h
constexpr uint8_t ROWS_PER_FLUSH = 32;

// Simulated sensor values representative of real feathergauge output
constexpr float   SIM_PRESSURE    = 1013.2f;  // mbar
constexpr float   SIM_TEMPERATURE = 22.50f;    // deg C
constexpr float   SIM_VOLTAGE     = 3.74f;     // VDC

constexpr int32_t SIM_PRESSURE_FIXED = 10132;
constexpr int32_t SIM_TEMPERATURE_FIXED = 2250;
constexpr int32_t SIM_VOLTAGE_FIXED = 374;

// Simulated fixed date/time components (no RTC needed)
constexpr int SIM_YEAR   = 2026;
constexpr int SIM_MONTH  = 4;
constexpr int SIM_DAY    = 17;
constexpr int SIM_HOUR   = 12;
constexpr int SIM_MINUTE = 43;
constexpr int SIM_SECOND = 0;

const char* SIM_DATE_CACHE = "2026/4/17";

// ---------------------------------------------------------------------------
// Shared state
// ---------------------------------------------------------------------------

File testFile;

char date[9];

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void blinkForever() {
  while (1) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void printSummary(unsigned long totalMs, unsigned long minMs, unsigned long maxMs) {
  Serial.println(F("--- Summary ---"));
  Serial.print(F("Total time   : ")); Serial.print(totalMs);                Serial.println(F(" ms"));
  Serial.print(F("Average/flush: ")); Serial.print(totalMs / NUM_FLUSH_EVENTS); Serial.println(F(" ms"));
  Serial.print(F("Min flush    : ")); Serial.print(minMs);                  Serial.println(F(" ms"));
  Serial.print(F("Max flush    : ")); Serial.print(maxMs);                  Serial.println(F(" ms"));
}

// ---------------------------------------------------------------------------
// Test 1 - raw byte writes
// ---------------------------------------------------------------------------

// Write exactly targetBytes bytes to testFile using the fill pattern.
void writeBytes(uint16_t targetBytes) {
  uint16_t remaining = targetBytes;
  while (remaining >= FILL_CHUNK_SIZE) {
    testFile.write(reinterpret_cast<const uint8_t*>(FILL_CHUNK), FILL_CHUNK_SIZE);
    remaining -= FILL_CHUNK_SIZE;
  }
  if (remaining > 0) {
    testFile.write(reinterpret_cast<const uint8_t*>(FILL_CHUNK), remaining);
  }
}

void runRawByteTest() {
  Serial.println(F("=== Test 1: Raw byte writes ==="));

  if (SD.exists("t1raw.bin")) {
    SD.remove("t1raw.bin");
  }
  testFile = SD.open("t1raw.bin", FILE_WRITE);
  if (!testFile) {
    Serial.println(F("Failed to open t1raw.bin!"));
    blinkForever();
  }

  Serial.println(F("Flush#,Size[B],FlushTime[ms]"));

  unsigned long totalMs = 0;
  unsigned long minMs   = 0xFFFFFFFFUL;
  unsigned long maxMs   = 0;

  for (uint16_t i = 0; i < NUM_FLUSH_EVENTS; i++) {
    uint16_t sz = WRITE_SIZES[i % NUM_SIZES];

    unsigned long t0 = millis();
    writeBytes(sz);
    testFile.flush();
    unsigned long elapsed = millis() - t0;

    totalMs += elapsed;
    if (elapsed < minMs) minMs = elapsed;
    if (elapsed > maxMs) maxMs = elapsed;

    Serial.print(i + 1);
    Serial.print(',');
    Serial.print(sz);
    Serial.print(',');
    Serial.println(elapsed);
  }

  testFile.close();
  printSummary(totalMs, minMs, maxMs);
}

// ---------------------------------------------------------------------------
// Test 2 - feathergauge imitation
// Replicates writeToOutputFile() from feathergauge_code.ino exactly, using
// the same sequence of outputFile.print() calls.  Batches ROWS_PER_FLUSH rows
// then flushes, matching the BUFFER_WRITE_COUNT batch in the main loop.
// ---------------------------------------------------------------------------

// Mirrors writeToOutputFile() in feathergauge_code.ino exactly.
// `millisec` is varied by the caller to simulate real timestamps.
void writeSimulatedRow(int millisec) {
  testFile.print(SIM_YEAR);
  testFile.print('/');
  testFile.print(SIM_MONTH);
  testFile.print('/');
  testFile.print(SIM_DAY);
  testFile.print(',');
  testFile.print(SIM_HOUR);
  testFile.print(':');
  if (SIM_MINUTE < 10) testFile.print('0');
  testFile.print(SIM_MINUTE);
  testFile.print(':');
  if (SIM_SECOND < 10) testFile.print('0');
  testFile.print(SIM_SECOND);
  testFile.print(':');
  if (millisec < 10) {
    testFile.print("00");
  } else if (millisec < 100) {
    testFile.print('0');
  }
  testFile.print(millisec);
  testFile.print(',');
  testFile.print(SIM_PRESSURE);
  testFile.print(',');
  testFile.print(SIM_TEMPERATURE);
  testFile.print(',');
  testFile.print(SIM_VOLTAGE);
  testFile.println();
}

void runFeathergaugeImitationTest() {
  Serial.println(F("=== Test 2: feathergauge imitation ==="));
  Serial.print(F("Rows per flush: "));
  Serial.println(ROWS_PER_FLUSH);

  if (SD.exists("t2fg.csv")) {
    SD.remove("t2fg.csv");
  }
  testFile = SD.open("t2fg.csv", FILE_WRITE);
  if (!testFile) {
    Serial.println(F("Failed to open t2fg.csv!"));
    blinkForever();
  }

  Serial.println(F("Flush#,Rows,FlushTime[ms]"));

  unsigned long totalMs  = 0;
  unsigned long minMs    = 0xFFFFFFFFUL;
  unsigned long maxMs    = 0;
  uint16_t      totalRow = 0;

  for (uint16_t i = 0; i < NUM_FLUSH_EVENTS; i++) {
    unsigned long t0 = millis();

    for (uint8_t r = 0; r < ROWS_PER_FLUSH; r++) {
      // Vary millisec 0..999 across rows to simulate realistic timestamps
      int millisec = (int)((totalRow % 1000));
      writeSimulatedRow(millisec);
      totalRow++;
    }
    testFile.flush();

    unsigned long elapsed = millis() - t0;

    totalMs += elapsed;
    if (elapsed < minMs) minMs = elapsed;
    if (elapsed > maxMs) maxMs = elapsed;

    Serial.print(i + 1);
    Serial.print(',');
    Serial.print(ROWS_PER_FLUSH);
    Serial.print(',');
    Serial.println(elapsed);
  }

  testFile.close();
  printSummary(totalMs, minMs, maxMs);
}

// ---------------------------------------------------------------------------
// Test 3 - Optimized feathergauge
// Same row content and flush cadence as Test 2, but assembles each row in a
// local char buffer using dtostrf() + snprintf(), then issues a single
// file.write() call per row instead of many individual print() calls.
//
// AVR's snprintf does not support %f, so floats are pre-formatted with
// dtostrf() into temporary char buffers before being passed to snprintf().
// ---------------------------------------------------------------------------

// Builds one CSV row into rowBuffer and writes it with a single file.write().
// `millisec` is varied by the caller to simulate real timestamps.
void writeOptimizedRow(int millisec) {
  // Pre-format floats: dtostrf(value, minWidth, decimalPlaces, destBuffer)
  char pressBuf[10], tempBuf[8], voltBuf[7];
  dtostrf(SIM_PRESSURE,    1, 2, pressBuf);
  dtostrf(SIM_TEMPERATURE, 1, 2, tempBuf);
  dtostrf(SIM_VOLTAGE,     1, 2, voltBuf);

  // Build the full row in one shot - matches feathergauge field order exactly.
  // %02d pads minute and second; %03d pads millisec (mirrors feathergauge logic).
  char rowBuffer[64];
  int len = snprintf(rowBuffer, sizeof(rowBuffer),
           "%d/%d/%d,%d:%02d:%02d:%03d,%s,%s,%s\r\n",
           SIM_YEAR, SIM_MONTH, SIM_DAY,
           SIM_HOUR, SIM_MINUTE, SIM_SECOND, millisec,
           pressBuf, tempBuf, voltBuf);

  testFile.write(reinterpret_cast<const uint8_t*>(rowBuffer), len);
}

// 1. Use fixed point, not floating point <-- Do now
// 2. Use itoa and pointer arithmetic <-- Do now
// 3. Cache the date/time string <-- Simulate now
// 4. Rip out circular buffer?

// Pressure sensor cannot have leading zeroes in decimal (20.05), but temperature sensor can

// Appends a variable-length integer to ptr; returns bytes written.
static inline void appendInt(char* ptr, int val) {
  itoa(val, ptr, 10);
  while (*ptr) ptr++;
}

// Appends a variable-length 32-bit integer to ptr; returns bytes written.
static inline int append32Int(char* ptr, int32_t val) {
  ltoa(val, ptr, 10);
  while (*ptr) ptr++;
}

// Appends a fixed 2-digit zero-padded integer (0-99) using direct ASCII math.
static inline void appendPadded2(char*& ptr, int val) {
  *ptr++ = '0' + (val / 10);
  *ptr++ = '0' + (val % 10);
}

// Appends a fixed 3-digit zero-padded integer (0-999) using direct ASCII math.
static inline void appendPadded3(char*& ptr, int val) {
  *ptr++ = '0' + (val / 100);
  *ptr++ = '0' + ((val / 10) % 10);
  *ptr++ = '0' + (val % 10);
}

void writeSuperOptimizedRow(int millisec) {
    char rowBuffer[64];
    char* ptr = rowBuffer;
    int32_t pressure_whole = SIM_PRESSURE_FIXED / 10;
    int32_t pressure_dec   = SIM_PRESSURE_FIXED % 10;
    int32_t temp_fixed;
    if (SIM_TEMPERATURE_FIXED < 0) {
      *ptr++ = '-';
      temp_fixed = -SIM_TEMPERATURE_FIXED;
    } else {
      temp_fixed = SIM_TEMPERATURE_FIXED;
    }
    int32_t temp_whole = SIM_TEMPERATURE_FIXED / 100;
    int32_t temp_dec   = SIM_TEMPERATURE_FIXED % 100;
    int voltage_whole  = SIM_VOLTAGE_FIXED / 100;
    int voltage_dec    = SIM_VOLTAGE_FIXED % 100;


    memcpy(ptr, SIM_DATE_CACHE, 9);
    ptr += 9;
    *ptr++ = ',';

    appendInt(ptr, SIM_HOUR);
    *ptr++ = ':';
    appendPadded2(ptr, SIM_MINUTE);
    *ptr++ = ':';
    appendPadded2(ptr, SIM_SECOND);
    *ptr++ = '.';
    appendPadded3(ptr, millisec);
    *ptr++ = ',';

    append32Int(ptr, pressure_whole);
    *ptr++ = '.';
    *ptr++ = '0' + pressure_dec;
    *ptr++ = ',';

    append32Int(ptr, temp_whole);
    *ptr++ = '.';
    appendPadded2(ptr, temp_dec);
    *ptr++ = ',';

    appendInt(ptr, voltage_whole);
    *ptr++ = '.';
    appendPadded2(ptr, voltage_dec);

    *ptr++ = '\r';
    *ptr++ = '\n';

    testFile.write(static_cast<const uint8_t*>(rowBuffer), static_cast<size_t>(ptr - rowBuffer));
}

// (Direct ASCII math is much faster than itoa for fixed 2-digit values)
// Only use itoa for values that aren't a fixed number of digits, otherwise the direct math is faster
// put absolute value in this function
// Also add line ending

void runOptimizedFeathergaugeTest() {
  Serial.println(F("=== Test 3: Optimized feathergauge ==="));
  Serial.print(F("Rows per flush: "));
  Serial.println(ROWS_PER_FLUSH);

  if (SD.exists("t3opt.csv")) {
    SD.remove("t3opt.csv");
  }
  testFile = SD.open("t3opt.csv", FILE_WRITE);
  if (!testFile) {
    Serial.println(F("Failed to open t3opt.csv!"));
    blinkForever();
  }

  Serial.println(F("Flush#,Rows,FlushTime[ms]"));

  unsigned long totalMs  = 0;
  unsigned long minMs    = 0xFFFFFFFFUL;
  unsigned long maxMs    = 0;
  uint16_t      totalRow = 0;

  for (uint16_t i = 0; i < NUM_FLUSH_EVENTS; i++) {
    unsigned long t0 = millis();

    for (uint8_t r = 0; r < ROWS_PER_FLUSH; r++) {
      int millisec = (int)(totalRow % 1000);
      writeOptimizedRow(millisec);
      totalRow++;
    }
    testFile.flush();

    unsigned long elapsed = millis() - t0;

    totalMs += elapsed;
    if (elapsed < minMs) minMs = elapsed;
    if (elapsed > maxMs) maxMs = elapsed;

    Serial.print(i + 1);
    Serial.print(',');
    Serial.print(ROWS_PER_FLUSH);
    Serial.print(',');
    Serial.println(elapsed);
  }

  testFile.close();
  printSummary(totalMs, minMs, maxMs);
}

void runSuperOptimizedFeathergaugeTest() {
  Serial.println(F("=== Test 4: Super Optimized feathergauge ==="));
  Serial.print(F("Rows per flush: "));
  Serial.println(ROWS_PER_FLUSH);

  if (SD.exists("t4opt.csv")) {
    SD.remove("t4opt.csv");
  }
  testFile = SD.open("t4opt.csv", FILE_WRITE);
  if (!testFile) {
    Serial.println(F("Failed to open t4opt.csv!"));
    blinkForever();
  }

  Serial.println(F("Flush#,Rows,FlushTime[ms]"));

  unsigned long totalMs  = 0;
  unsigned long minMs    = 0xFFFFFFFFUL;
  unsigned long maxMs    = 0;
  uint16_t      totalRow = 0;

  for (uint16_t i = 0; i < NUM_FLUSH_EVENTS; i++) {
    unsigned long t0 = millis();

    for (uint8_t r = 0; r < ROWS_PER_FLUSH; r++) {
      int millisec = (int)(totalRow % 1000);
      writeSuperOptimizedRow(millisec);
      totalRow++;
    }
    testFile.flush();

    unsigned long elapsed = millis() - t0;

    totalMs += elapsed;
    if (elapsed < minMs) minMs = elapsed;
    if (elapsed > maxMs) maxMs = elapsed;

    Serial.print(i + 1);
    Serial.print(',');
    Serial.print(ROWS_PER_FLUSH);
    Serial.print(',');
    Serial.println(elapsed);
  }

  testFile.close();
  printSummary(totalMs, minMs, maxMs);
}

// ---------------------------------------------------------------------------
// Setup - runs all three tests sequentially
// ---------------------------------------------------------------------------

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(SERIAL_BAUD);
  while (!Serial);

  Serial.println(F("SD Write Performance Test"));
  Serial.println(F("Initializing SD card..."));

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD card init failed!"));
    blinkForever();
  }
  Serial.println(F("SD card initialized."));

  runRawByteTest();
  runFeathergaugeImitationTest();
  runOptimizedFeathergaugeTest();
  runSuperOptimizedFeathergaugeTest();

  Serial.println(F("All tests complete."));
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  // Nothing to do after setup completes
}
