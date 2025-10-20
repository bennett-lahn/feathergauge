// Must define a malloc failed hook function
// restart peripherals???
// add interrupt detaches

// ===========================
// USER-DEFINED FLAGS: 
// Please set the following three flags according to your sensor type and preferred collection type.
// ===========================

// Set to true for MS5837 (new pressure sensor)
// Set to false for MS5803 (old pressure sensor)
#define USE_NEW_SENSOR               false

// Set to false for rapid start
// Set to true for delayed start, using selected start date
#define DELAY_START                  false

// Set to true for burst sampling
// Set to false for constant sampling
#define BURST_SAMPLING               true

#define BURST_SAMPLING_ONE_SAMPLE    true

// ===========================
// USER INPUTS:
// Please set the below variables to reflect your sampling preferences.
// ===========================
#define SAMPLE_FREQ                   1        // Sampling frequency in Hz

// Burst sampling alternates between writing and sleeping according to set periods below
const uint8_t writeSeconds = 5;    // Number of seconds to sample data in burst sampling

const uint8_t sleepSeconds = 120;   // Number of seconds to sleep in burst sampling. Currently burst sampling only works if sleeping for at least 15 seconds

// Edit for DELAY start ONLY
#if DELAY_START // Date to start sampling
  const int startYear   = 2025; // Year to start sampling
  const int startMonth  = 9;    // Month to start sampling
  const int startDay    = 17;   // Day to start sampling
  const int startHour   = 13;   // Hour to start sampling (24-hr format)
  const int startMinute = 00;   // Minute to start sampling
  bool hasStarted = false;
#else // Early dummy date to satisfy later conditional check
  const int startYear   = 2000; // DO NOT MODIFY
  const int startMonth  = 1;    // DO NOT MODIFY
  const int startDay    = 1;    // DO NOT MODIFY
  const int startHour   = 12;   // DO NOT MODIFY
  const int startMinute = 0;    // DO NOT MODIFY
#endif

// =========================== USER: DO NOT EDIT BELOW THIS LINE ===========================

// ===========================
// LIBRARIES
// =========================== 

#include <Wire.h> // Arduino library
#include <SPI.h> // Arduino library
#include <SD.h> // Arduino library
#include <EEPROM.h> // Arduino library (?)
#include <avr/power.h>

#if USE_NEW_SENSOR
  #include "MS5837.h"
#else
  #include "SparkFun_MS5803_I2C.h"
#endif

// A note on using the SparkFun library:
// A very minor change to the SparkFun library is needed for this code to work. The sensorWait function in SparkFun_MS5803_I2C.h must be declared as virtual.
// This change is used to override sensorWait for better power management.

#include "RTClib.h"
#include "TimerOne.h"
#include "LowPower.h"

// Create custom MS5803 class to override sensorWait function for better power management
class CustomMS5803 : public MS5803 {
  public:
    void sensorWait(uint8_t time) {
      // sensorWait in SparkFun library is 1-10ms; the minimum sleep time is 16ms; In this sketch timer 0 wakes from idle after 1 ms regardless of sleep time
      // Turn off everything except timer 0 (used for millis), timer 1 (used for sampling), and TWI (used to communicate with pressure sensor)
      // ADC, timer 3, and timer 4 are set to ON so LowPower library doesn't accidentally turn them back on, they are already off
      LowPower.idle(SLEEP_15MS, ADC_ON, TIMER4_ON, TIMER3_ON, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
    }
};

// Timer 4 PRR bit is currently not defined in iom32u4.h
#ifndef PRTIM4
  #define PRTIM4                       4       // Power Reduction Register bit for Timer 4
#endif

// Timer 4 power reduction macro is not defined currently in power.h
#ifndef power_timer4_disable
  #define power_timer4_disable()	(PRR1 |= (uint8_t)(1 << PRTIM4))  // Disable Timer 4 to save power
#endif

#ifndef power_timer4_enable
  #define power_timer4_enable()		(PRR1 &= (uint8_t)~(1 << PRTIM4))  // Enable Timer 4
#endif

// ===========================
// MAGIC NUMBER DEFINITIONS
// ===========================
// Pin definitions
#define LED_PIN                       13      // Built-in LED pin for status indication
#define RTC_INTERRUPT_PIN             1       // RTC alarm interrupt pin (DS3231 SQW)
#define SD_CARD_SELECT_PIN            4       // SPI chip select pin for SD card
#define BATTERY_VOLTAGE_PIN           A9      // Analog pin for battery voltage measurement

// Timing and frequency definitions
#define MICROSECONDS_PER_SECOND       1000000 // Conversion factor for time calculations
#define SAMPLE_TIME                   (1.0f / SAMPLE_FREQ)*MICROSECONDS_PER_SECOND  // Timer1 period in microseconds
#define TWI_CLOCK_SPEED               400000  // I2C communication speed (400kHz)
#define SERIAL_BAUD_RATE              57600   // Serial communication speed for debugging
#define ERROR_BLINK_DELAY             100     // LED on/off time during error indication (ms)
#define ERROR_PAUSE_DELAY             200     // Pause between error blink cycles (ms)
#define ERROR_BLINK_CYCLE             10      // Total blinks per error cycle

// ADC and voltage definitions
#define MAX_ADC_VALUE                 1024    // Maximum ADC reading (10-bit resolution)
#define REFERENCE_VOLTAGE             3.3     // ADC reference voltage (V)
#define BATTERY_VOLTAGE_MULTIPLIER    2       // Voltage divider compensation factor

// Buffer and data definitions
#define BUFFER_SIZE                   36      // Circular buffer size (limited by 32u4 SRAM, only 2560 bytes)
#define BUFFER_WRITE_COUNT            5       // Buffer and data definitions 32
#define FILENAME_LENGTH               13      // Filename length, limited by FAT16 filesystem to 8.3 format (13th char for null terminator)
#define FRESHWATER_DENSITY            997     // Freshwater density (kg/m³) for pressure calculations
#define SALTWATER_DENSITY            1025     // Saltwater density (kg/m³) for pressure calculations

// Error code definitions
#define ERROR_SD_CARD_FAILED          1       // Error code for SD card initialization failure
#define ERROR_FILE_OPEN_FAILED        5       // Error code for file creation failure

#define SERIAL_NUMBER_ADDRESS         0       // EEPROM address for device serial number

// ===========================
// DATA STRUCTURE FOR CIRCULAR BUFFER
// ===========================
struct DataPoint {
  float pressure;
  float temperature;
  float batteryVoltage;
  uint16_t millisec;
  DateTime now;
  bool valid; // Flag to indicate if this data point is valid
};

// ===========================
// INITIALIZING GLOBAL VARIABLES
// ===========================
#if USE_NEW_SENSOR
  MS5837 newSensor;
#else
  MS5803 oldSensor;
#endif

#if BURST_SAMPLING
  TimeSpan elapsed;
#endif

RTC_DS3231 rtc;
File outputFile;           // Used to open, write to, and close files on the SD card

// Circular buffer variables
DataPoint dataBuffer[BUFFER_SIZE];
int bufferHead        = 0;  // Points to next write position
int bufferTail        = 0;  // Points to next read position
int bufferCount       = 0;  // Number of items in buffer
bool bufferOverflow   = false; // Flag to indicate buffer overflow

DateTime timeAtBurstSwitch; // Time burst switched between sleep and record
DateTime currentSecond;     // Time of current second
float currentVoltage;       // Voltage of battery, updated every second

// Sampling flag for ISR to main loop communication
volatile bool samplingFlag   = false;
volatile bool resetTimerFlag = false;
volatile bool deepSleepFlag  = false;
volatile bool burstSleepFlag = false;
volatile bool sleeping       = false;

unsigned long millisAtInterrupt = 0; // Number of milliseconds at last 1 Hz interrupt from RTC

// ===========================
// SETUP - SENSOR, TIMESTAMP, SD CARD
// ===========================

void setup() {
  power_timer3_disable();
  power_timer4_disable();
  pinMode(LED_PIN, OUTPUT); // Activates the red LED on pin 13
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial); 
  Wire.begin();
  Wire.setClock(TWI_CLOCK_SPEED);
  
  if (!rtc.begin()) {
    Serial.println(F("RTC Failed to initialize"));
    error(2);
    return;
  }

  // DS3231 SQW pin requires an external pull-up resistor. The internal pull-up resistors included in the 32u4 are too weak 
  // for square wave oscillator output but interrupts still work.
  rtc.disable32K();
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);

  if (rtc.lostPower()) {
        // This will adjust to the date and time at compilation; recompile everytime MCU is flashed for accuracy
        // If the DS3231 has had continous MCU/battery power since last program flash, the time will not be changed
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        DateTime setTime = rtc.now();
        Serial.print(F("RTC time set to: "));
        Serial.print(setTime.year());
        Serial.print('/');
        Serial.print(setTime.month());
        Serial.print('/');
        Serial.print(setTime.day());
        Serial.print(' ');
        Serial.print(setTime.hour());
        Serial.print(':');
        Serial.print(setTime.minute());
        Serial.print(':');
        Serial.println(setTime.second());
  }

  currentSecond = rtc.now();
  currentVoltage = getBatteryVoltage();
  power_adc_disable();
  
  #if USE_NEW_SENSOR
    if (!newSensor.init()) {
      Serial.println("New pressure sensor failed to initialize");
      error(3);
      return;
    }
    newSensor.setModel(MS5837::MS5837_02BA);
    newSensor.setFluidDensity(FRESHWATER_DENSITY);
  #else
    oldSensor.reset();
    oldSensor.begin();
  #endif

    Serial.print(F("Initializing SD card..."));
  
  if (!SD.begin(SD_CARD_SELECT_PIN)) { 
    Serial.println(F("Card Failed or Not Present"));
    error(ERROR_SD_CARD_FAILED);
    return;
  }
  
  for (int i = 0; i < BUFFER_SIZE; i++) {
    dataBuffer[i].valid = false;
  }

  Serial.println(F("Card Initialized."));
  char fileName[FILENAME_LENGTH];
  makeFileName(fileName);
  
  SdFile::dateTimeCallback(setTimeStamp); // Timestamps the .csv file (inserts as metadata)
  outputFile = SD.open(fileName, FILE_WRITE); // Opens .csv file

  if (outputFile) {
    Serial.println(F("File opened"));
    char serialNumber[16];
    EEPROM.get(SERIAL_NUMBER_ADDRESS, serialNumber);
    outputFile.print(F("W.G. Num: "));
    outputFile.print(serialNumber);
    outputFile.print(',');
    outputFile.print(F("Timestamp,Pressure [mbar],Temp [deg C],Battery [VDC]"));
    outputFile.println();
    outputFile.flush();
  } else {
    Serial.println(F("error opening datalog-case1"));
    error(ERROR_FILE_OPEN_FAILED);
  }

  #if DELAY_START
    enterDelayDeepSleep();      
  #else
    DateTime now = rtc.now();
    rtc.disableAlarm(2);
    // Set alarm to go off 1 second from now, DS3231_A1_PerSecond triggers alarm when seconds match
    rtc.clearAlarm(1);
    rtc.setAlarm1(rtc.now() + TimeSpan(1), DS3231_A1_PerSecond); 
    #if !BURST_SAMPLING_ONE_SAMPLE
      Timer1.initialize(SAMPLE_TIME);
      Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling
      attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING);
    #endif
    millisAtInterrupt = millis();
    // Serial.println(F("READY!"));
    #if BURST_SAMPLING
      timeAtBurstSwitch = rtc.now();
      Serial.print(F("[BURST] Start write window at "));
      Serial.print(timeAtBurstSwitch.hour()); Serial.print(':');
      Serial.print(timeAtBurstSwitch.minute()); Serial.print(':');
      Serial.println(timeAtBurstSwitch.second());
      Serial.print(F("[BURST] writeSeconds=")); Serial.print(writeSeconds);
      Serial.print(F(", sleepSeconds=")); Serial.println(sleepSeconds);
    #endif
  #endif
}

// ===========================
// DATA COLLECTION LOOP
// ===========================

void loop() {
    if (deepSleepFlag) {
      digitalWrite(LED_PIN, HIGH);
      // delay(3000);
      digitalWrite(LED_PIN, LOW);
      deepSleepLog();
    }
    #if BURST_SAMPLING
      // If true, write time ended; write to SD card and sleep
      if (burstSleepFlag) {
        timeAtBurstSwitch = rtc.now();
        #if !BURST_SAMPLING_ONE_SAMPLE
          Timer1.stop();
          attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING);
        #endif
        resetTimer();
        Serial.print(F("[BURST] Wake -> start write window at "));
        Serial.print(timeAtBurstSwitch.hour()); Serial.print(':');
        Serial.print(timeAtBurstSwitch.minute()); Serial.print(':');
        Serial.println(timeAtBurstSwitch.second());
        burstSleepFlag = false;
      } else if (elapsed.totalseconds() > writeSeconds || BURST_SAMPLING_ONE_SAMPLE) {
        digitalWrite(LED_PIN, HIGH);
        delay(1);
        digitalWrite(LED_PIN, LOW);
        #if BURST_SAMPLING_ONE_SAMPLE
          performSensorReading();
        #endif
        DateTime endTime = rtc.now();
        DataPoint data;
        while (readFromBuffer(&data)) {
          writeToOutputFile(data.now, data.millisec, data.pressure, data.temperature, data.batteryVoltage);
        }
        outputFile.flush();
        Serial.print(F("[BURST] End write window at "));
        Serial.print(endTime.hour()); Serial.print(':');
        Serial.print(endTime.minute()); Serial.print(':');
        Serial.println(endTime.second());
        Serial.print("Elapsed time: ");
        Serial.println(elapsed.totalseconds());
        enterBurstDeepSleep(endTime);
      }
    #endif

    if (resetTimerFlag) {
      // Serial.print("Resetting millis, was: ");
      // Serial.println(millisAtInterrupt);
      resetTimer();
      resetTimerFlag = false;
      // Serial.print("Now: ");
      // Serial.println(millisAtInterrupt);
    }

    if (samplingFlag) {
      #if !BURST_SAMPLING_ONE_SAMPLE
        performSensorReading();
      #endif
      samplingFlag = false;
    }

    if (bufferCount > BUFFER_WRITE_COUNT - 1) {
      DataPoint data;
      int writeCount = 0;
      while (readFromBuffer(&data) && writeCount < BUFFER_WRITE_COUNT) {
        writeToOutputFile(data.now, data.millisec, data.pressure, data.temperature, data.batteryVoltage);
        writeCount++;
        if (samplingFlag) {
          performSensorReading();
          samplingFlag = false;
        }
      }
      Serial.print(F("Starting write at time: "));
      Serial.println(millis());
      outputFile.flush();
      Serial.print(F("Wrote "));
      Serial.print(writeCount);
      Serial.print(F(" data points to SD card at time "));
      Serial.println(millis());
    }
    // Finished writing to SD card, sleep until next sampling time or interrupt from timers/RTC
    // When waking from burst sleep, this idle command mean the MCU will sleep until the next ms, then check the flag
    setForeverIdleSleep();
}

// ===========================
// DATA ACQUISITION FUNCTIONS
// ===========================
/**
 * performSensorReading
 * Purpose: Read pressure and temperature from the active sensor and enqueue a data point with timestamp and battery voltage.
 * Inputs:
 *   - None (uses globals: currentSecond, millisAtInterrupt, currentVoltage, currentPressure, currentTemperature)
 * Usage: Called when `samplingFlag` is set or in one-sample burst mode.
 */
void performSensorReading() {
    #if USE_NEW_SENSOR
      newSensor.read();
      currentTemperature = newSensor.temperature();
      currentPressure = newSensor.pressure();
    #else
      oldSensor.getSensorReadings(CELSIUS, ADC_4096, ADC_512, &currentPressure, &currentTemperature);
    #endif

    // Compute timestamp relative to last RTC tick without mutating global currentSecond
    uint16_t millisec = millis() - millisAtInterrupt;
    DateTime now = currentSecond;
    // while (millisec >= 1000) {
    //   millisec -= 1000;
    //   now = now + TimeSpan(1);
    // }

    addToBuffer(now, millisec, currentPressure, currentTemperature, currentVoltage);
}

/**
 * getBatteryVoltage
 * Purpose: Measure battery voltage via ADC on `BATTERY_VOLTAGE_PIN` using divider ratio.
 * Inputs: None
 * Returns: float — battery voltage in volts.
 * Usage: Call once per second or as needed; ADC MUST enabled/disabled by caller.
 */
float getBatteryVoltage() {
  int sensorValue = analogRead(BATTERY_VOLTAGE_PIN);
  float batteryVoltage = (sensorValue * REFERENCE_VOLTAGE) / MAX_ADC_VALUE; // Adafruit docs says this should also multiply by 2?
  float actualBatteryVoltage = batteryVoltage * BATTERY_VOLTAGE_MULTIPLIER; // Adjust multiplier based on your divider
  return actualBatteryVoltage;
}

// ===========================
// RTC/TIME/SD UTILITIES
// ===========================

/**
 * resetTimer
 * Purpose: Synchronize timer-driven sampling to the RTC second tick; updates timekeeping and schedules next RTC alarm.
 * Inputs: None
 * Usage: Call after every RTC interrupt or when starting new sampling windows.
 */
void resetTimer() {
  power_adc_enable();
  millisAtInterrupt = millis();
  currentSecond = rtc.now();
  Serial.print(F("Reset timer at: "));
  Serial.print(currentSecond.year());
  Serial.print('/');
  Serial.print(currentSecond.month());
  Serial.print('/');
  Serial.print(currentSecond.day());
  Serial.print(' ');
  Serial.print(currentSecond.hour());
  Serial.print(':');
  Serial.print(currentSecond.minute());
  Serial.print(':');
  Serial.println(currentSecond.second());
  #if BURST_SAMPLING
    elapsed = currentSecond - timeAtBurstSwitch;
  #endif
  currentVoltage = getBatteryVoltage();
  #if !BURST_SAMPLING_ONE_SAMPLE
    rtc.clearAlarm(1);
    rtc.setAlarm1(currentSecond + TimeSpan(1), DS3231_A1_PerSecond);
  #endif
  power_adc_disable();
}


/**
 * makeFileName
 * Purpose: Generate a daily-rotating CSV filename in the form MM-DD-XX.csv where XX increments from 00..99.
 * Inputs:
 *   - fileName: char* (out) — caller-provided buffer of length FILENAME_LENGTH.
 * Usage: Call once during setup before opening the SD file.
 */
void makeFileName(char* fileName) {
  // Format: MM-DD-00.csv (e.g., 04-23-01.csv)
  DateTime now = rtc.now();
  
  // Build filename: MM-DD-
  fileName[0] = '0' + (now.month() / 10);
  fileName[1] = '0' + (now.month() % 10);
  fileName[2] = '-';
  fileName[3] = '0' + (now.day() / 10);
  fileName[4] = '0' + (now.day() % 10);
  fileName[5] = '-';
  fileName[6] = '0';
  fileName[7] = '0';
  fileName[8] = '.';
  fileName[9] = 'c';
  fileName[10] = 's';
  fileName[11] = 'v';
  fileName[12] = '\0';
  // Find the next available file index for this day
  for (uint8_t fileIndex = 1; fileIndex < 100; fileIndex++) {
    if (!SD.exists(fileName)) {
      break;
    }
    fileName[6] = '0' + (fileIndex / 10);
    fileName[7] = '0' + (fileIndex % 10);
  }
  
  Serial.print(F("File name: "));
  Serial.println(fileName);
}

/**
 * setTimeStamp
 * Purpose: Provide FAT filesystem date/time for file creation/modification via SD library callback.
 * Inputs:
 *   - date: uint16_t* (out) — FAT date packed with year, month, day.
 *   - time: uint16_t* (out) — FAT time packed with hour, minute, second.
 * Usage: Registered as SdFile::dateTimeCallback handler.
 */
void setTimeStamp(uint16_t* date, uint16_t* time) {
  DateTime now = rtc.now();
  *date = FAT_DATE(now.year(), now.month(), now.day());
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

/**
 * writeToOutputFile
 * Purpose: Append a CSV line to the open SD file with timestamp, pressure, temperature, and battery voltage.
 * Inputs:
 *   - now: DateTime — timestamp (date and time to seconds).
 *   - millisec: int — millisecond offset within the second (0..999).
 *   - pressure: float — pressure in mbar.
 *   - temperature: float — temperature in °C.
 *   - batteryVoltage: float — battery voltage in V.
 * Usage: Call repeatedly to serialize buffered samples.
 */
void writeToOutputFile(DateTime now, int millisec,float pressure, float temperature, float batteryVoltage) {
  // Write data to SD card
  outputFile.print(now.year());
  outputFile.print('/');
  outputFile.print(now.month());
  outputFile.print('/');
  outputFile.print(now.day());
  outputFile.print(",");
  outputFile.print(now.hour());
  outputFile.print(':');
  if (now.minute() < 10) {
    outputFile.print("0");
  }
  outputFile.print(now.minute());
  outputFile.print(':');
  if (now.second() < 10) {
    outputFile.print("0");
  }
  outputFile.print(now.second());
  outputFile.print(':');
  if (millisec < 10) {
    outputFile.print("00");
  } else if (millisec < 100) {
    outputFile.print("0");
  }
  outputFile.print(millisec);
  outputFile.print(",");
  outputFile.print(pressure);
  outputFile.print(",");
  outputFile.print(temperature);
  outputFile.print(",");
  outputFile.print(batteryVoltage);
  outputFile.println();
}

// ===========================
// CIRCULAR BUFFER FUNCTIONS
// ===========================

/**
 * addToBuffer
 * Purpose: Push a data point into the circular buffer; overwrites oldest when full.
 * Inputs:
 *   - now: DateTime — timestamp for the sample (second resolution).
 *   - millisec: int — millisecond offset within the current second (0..999).
 *   - pressure: float — pressure reading in mbar.
 *   - temperature: float — temperature reading in °C.
 *   - batteryVoltage: float — battery voltage in V.
 * Usage: Call immediately after each sensor read to store data between SD writes.
 */
void addToBuffer(DateTime now, int millisec, float pressure, float temperature, float batteryVoltage) {
  if (bufferCount >= BUFFER_SIZE) {
    bufferTail = (bufferTail + 1) % BUFFER_SIZE;
  } else {
    bufferCount++;
  }
  
  dataBuffer[bufferHead].now = now;
  dataBuffer[bufferHead].millisec = millisec;
  dataBuffer[bufferHead].pressure = pressure;
  dataBuffer[bufferHead].temperature = temperature;
  dataBuffer[bufferHead].batteryVoltage = batteryVoltage;
  dataBuffer[bufferHead].valid = true;
  
  bufferHead = (bufferHead + 1) % BUFFER_SIZE;
}

/**
 * readFromBuffer
 * Purpose: Pop the oldest valid data point from the circular buffer.
 * Inputs:
 *   - data: DataPoint* (out) — caller-allocated struct to receive data.
 * Returns: bool — true if a valid data point was returned; false if buffer empty.
 * Usage: Use to remove and return the oldest data point from the buffer.
 */
bool readFromBuffer(DataPoint* data) {
  if (bufferCount == 0) {
    return false; // Buffer is empty
  }
  
  // Copy data from buffer
  data->now = DateTime(dataBuffer[bufferTail].now.unixtime());
  data->millisec = dataBuffer[bufferTail].millisec;
  data->pressure = dataBuffer[bufferTail].pressure;
  data->temperature = dataBuffer[bufferTail].temperature;
  data->batteryVoltage = dataBuffer[bufferTail].batteryVoltage;
  data->valid = dataBuffer[bufferTail].valid;
  
  // Mark as invalid and move tail
  dataBuffer[bufferTail].valid = false;
  bufferTail = (bufferTail + 1) % BUFFER_SIZE;
  bufferCount--;
  
  return data->valid;
}

// ===========================
// SLEEP MANAGEMENT
// ===========================

/**
 * deepSleepLog
 * Purpose: Handle wake events during delayed start: if start time reached, initialize sampling; otherwise log a sample and return to deep sleep.
 * Inputs: None
 * Usage: Called from loop when `deepSleepFlag` is set by RTC interrupt during DELAY_START.
 */
void deepSleepLog() {
  deepSleepFlag = false;
  DateTime now = rtc.now();
  DateTime startDateTime(startYear, startMonth, startDay, startHour, startMinute, 0);
  if (now.unixtime() >= startDateTime.unixtime()) {
    // digitalWrite(LED_PIN, HIGH);
    // delay(100);
    // digitalWrite(LED_PIN, LOW);
    rtc.clearAlarm(1);
    rtc.clearAlarm(2);
    rtc.disableAlarm(2);
    // Set alarm to go off 1 second from now, DS3231_A1_PerSecond triggers alarm when seconds match
    rtc.setAlarm1(now + TimeSpan(1), DS3231_A1_PerSecond);
    #if BURST_SAMPLING
      timeAtBurstSwitch = rtc.now();
    #endif
    #if !BURST_SAMPLING_ONE_SAMPLE
      Timer1.initialize(SAMPLE_TIME);
      Timer1.attachInterrupt(triggerSampling);
      attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING);
    #endif
    millisAtInterrupt = millis();
    // Now continue with regular program execution
  } else {
    // digitalWrite(LED_PIN, HIGH);
    // delay(10000);
    // digitalWrite(LED_PIN, LOW);
    power_adc_enable();
    performSensorReading();
    currentVoltage = getBatteryVoltage(); // Perform voltage reading after sensor reading to give ADC time to settle
    power_adc_disable();
    DataPoint data;
    readFromBuffer(&data);
    writeToOutputFile(data.now, data.millisec, data.pressure, data.temperature, currentVoltage); // Don't use batteryVoltage from buffer because it is old
    outputFile.flush();
    enterDelayDeepSleep();
  }
}

/**
 * enterDelayDeepSleep
 * Purpose: Configure RTC alarms for the target start date/time and enter deep sleep until then.
 * Inputs: None
 * Usage: Used when DELAY_START is enabled.
 */
void enterDelayDeepSleep() {
  // Set RTC alarm to correct date
  // Serial.println(F("Entering RTC deep sleep"));
  DateTime startDateTime(startYear, startMonth, startDay, startHour, startMinute, 0);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), deepSleepInterrupt, FALLING);
  rtc.setAlarm1(startDateTime, DS3231_A1_Date); // Alarm 1 triggers at the start time
  rtc.setAlarm2(startDateTime, DS3231_A2_Hour); // Alarm 2 triggers every day, taking a sample to track when/if battery dies
  Serial.println(F("Entering delay deep sleep"));
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  // digitalWrite(LED_PIN, HIGH);
  // delay(1000);
  // digitalWrite(LED_PIN, LOW);
}

/**
 * enterBurstDeepSleep
 * Purpose: Schedule next wake time after a burst write window and enter deep sleep until then.
 * Inputs:
 *   - endTime: DateTime — timestamp when the last write window ended.
 * Usage: Used only when BURST_SAMPLING is enabled.
 */
void enterBurstDeepSleep(DateTime endTime) {
  rtc.clearAlarm(1);
  DateTime now = rtc.now();
  if (now.unixtime() > endTime.unixtime() + sleepSeconds) {
    // digitalWrite(LED_PIN, HIGH);
    // delay(1000000UL);
    // digitalWrite(LED_PIN, LOW);
    timeAtBurstSwitch = rtc.now();
    resetTimer();
    attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING); // TODO: Should this run in single sample mode?
    #if !BURST_SAMPLING_ONE_SAMPLE
      Timer1.initialize(SAMPLE_TIME);
      Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling
    #endif
    burstSleepFlag = true;
    return;
  }
  DateTime nextWake(endTime + TimeSpan(sleepSeconds));
  Serial.print(F("[BURST] Next wake scheduled at "));
  Serial.print(nextWake.hour()); Serial.print(':');
  Serial.print(nextWake.minute()); Serial.print(':');
  Serial.println(nextWake.second());
  rtc.clearAlarm(1);
  rtc.setAlarm1(nextWake, DS3231_A1_Date);
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), burstSleepInterrupt, FALLING);

  // LowPower.idle(SLEEP_FOREVER, ADC_ON, TIMER4_ON, TIMER3_ON, TIMER1_OFF, TIMER0_OFF, SPI_OFF, USART1_OFF, TWI_OFF, USB_OFF);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  delay(50);  // This delay is important to allow MCU components to stabilize
  Wire.end();
  Wire.begin();
  Wire.setClock(TWI_CLOCK_SPEED);
  Serial.begin(SERIAL_BAUD_RATE);
  rtc.clearAlarm(1);
  timeAtBurstSwitch = rtc.now();
  resetTimer();
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING); // TODO: Should this run in single sample mode?
  #if !BURST_SAMPLING_ONE_SAMPLE
    Timer1.initialize(SAMPLE_TIME);
    Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling
  #endif
}
/**
 * setForeverIdleSleep
 * Purpose: Enter idle sleep indefinitely while keeping timers 0/1 running for millis tracking and sampling cadence.
 * Inputs: None
 * Usage: Use at end of loop to minimize power between events while preserving timer state.
 */
void setForeverIdleSleep() {
  // TODO: See if turning on SPI/TWI improves data loss
  // ADC, timer 3, and timer 4 are set to ON so LowPower library doesn't accidentally turn them back on, they are already off
  LowPower.idle(SLEEP_FOREVER, ADC_ON, TIMER4_ON, TIMER3_ON, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
}

// ===========================
// ERRORS/TROUBLESHOOTING
// ===========================
// Triggers the LED on pin 13 to blink a certain number of times during unrecoverable errors
/**
 * error
 * Purpose: Indicate unrecoverable error by blinking LED a number of times equal to `errno`, repeated forever.
 * Inputs:
 *   - errno: uint8_t — error code and blink count.
 * Usage: Call on fatal conditions; function does not return.
 */
void error (uint8_t errno) {
  while (1) {
    uint8_t blinkCount; // Counts error blinks
    for (blinkCount = 0; blinkCount < errno; blinkCount++) {
      digitalWrite(LED_PIN, HIGH);
      delay(ERROR_BLINK_DELAY);
      digitalWrite(LED_PIN, LOW);
      delay(ERROR_BLINK_DELAY);
    }
    for (blinkCount = errno; blinkCount < ERROR_BLINK_CYCLE; blinkCount++) {
      delay(ERROR_PAUSE_DELAY);
    }
  }
}

// ===========================
// INTERRUPT SERVICE ROUTINES
// ===========================

/**
 * triggerSampling (ISR context)
 * Purpose: Flag main loop to perform a sensor read (Timer1 compare interrupt).
 * Inputs: None
 */
void triggerSampling() {
    samplingFlag = true;
}

/**
 * resetTimerInterrupt (ISR context)
 * Purpose: Flag main loop to resynchronize timers on each RTC second tick.
 * Inputs: None
 */
void resetTimerInterrupt() {
  resetTimerFlag = true;
}

/**
 * deepSleepInterrupt (ISR context)
 * Purpose: Flag main loop that the RTC alarm fired during delay-start deep sleep.
 * Inputs: None
 */
void deepSleepInterrupt() {
  deepSleepFlag = true;
}

/**
 * burstSleepInterrupt (ISR context)
 * Purpose: Flag main loop that the RTC alarm fired to end burst sleep.
 * Inputs: None
 */
void burstSleepInterrupt() {
  // Do nothing
}
