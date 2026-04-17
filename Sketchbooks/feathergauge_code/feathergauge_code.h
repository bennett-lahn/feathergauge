#ifndef FEATHERGAUGE_CODE_H
#define FEATHERGAUGE_CODE_H

// ===========================
// LIBRARIES
// =========================== 

#include "user_config.h"

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <avr/power.h>
#include <TimerOne.h>
#include <LowPower.h>

// INTERNAL WARNING - Ignore before January 1st, 2037
// RTClib uses a 32-bit variable to track time. After January 19th 2038, this value will overflow
// and the library will have to be updated to use 64-bit variables for correct operation.
// I am happy this code is still being used in 2038. :) 
#include <RTClib.h>

#if USE_NEW_SENSOR
  #include "MS5837.h"
#else
  #include "SparkFun_MS5803_I2C.h"
#endif

// ===========================
// CLASSES
// ===========================

class MS5837;
class MS5803;

// Create custom MS5803 class to override sensorWait function for better power management
class CustomMS5803 : public MS5803 {
  public:
    void sensorWait(uint8_t time) {
      // sensorWait in SparkFun library is 1-10ms; the minimum sleep time is 16ms; In this sketch 
      // timer 0 wakes MCU from idle after 1 ms regardless of sleep time.
      // Turn off everything except timer 0 (used for millis), timer 1 (used for sampling),
      // and TWI (used to communicate with pressure sensor). ADC, timer 3, and timer 4 are set to
      // ON so LowPower library doesn't accidentally turn them back on, they are already off
      LowPower.idle(SLEEP_15MS, ADC_ON, TIMER4_ON, TIMER3_ON, TIMER1_ON, TIMER0_ON, SPI_OFF, 
                    USART1_OFF, TWI_ON, USB_OFF);
    }
};

// ===========================
// MACROS
// ===========================

// Timer 4 PRR bit is currently not defined in iom32u4.h
#ifndef PRTIM4
  #define PRTIM4 4  // Power Reduction Register bit for Timer 4
#endif

// Timer 4 power reduction macro is not defined currently in power.h, define it manually
// This macro manipulates the MCU registers to disable timer 4
#ifndef power_timer4_disable
  #define power_timer4_disable()	(PRR1 |= (uint8_t)(1 << PRTIM4))
#endif

// This macro manipulates the MCU registers to enable timer 4
#ifndef power_timer4_enable
  #define power_timer4_enable()		(PRR1 &= (uint8_t)~(1 << PRTIM4))
#endif

// ===========================
// CONSTANT DEFINITIONS
// ===========================

// Pin definitions
constexpr uint8_t LED_PIN                        = 13;        // Built-in LED pin for status indication
constexpr uint8_t RTC_INTERRUPT_PIN              = 1;         // RTC alarm interrupt pin (DS3231 SQW)
constexpr uint8_t SD_CARD_SELECT_PIN             = 4;         // SPI chip select pin for SD card
constexpr uint8_t BATTERY_VOLTAGE_PIN            = A9;        // Analog pin for battery voltage measurement

// Timing and frequency definitions
constexpr unsigned long MICROSECONDS_PER_SECOND  = 1000000UL; // Conversion factor for time calculations
constexpr float SAMPLE_TIME                      = (1.0f / SAMPLE_FREQ) * MICROSECONDS_PER_SECOND;  // Timer1 period in microseconds
constexpr unsigned long TWI_CLOCK_SPEED          = 400000UL;  // I2C communication speed (400kHz)
constexpr unsigned long SERIAL_BAUD_RATE         = 57600UL;   // Serial communication speed for debugging
constexpr uint8_t ERROR_BLINK_DELAY              = 100;       // LED on/off time during error indication (ms)
constexpr uint8_t ERROR_PAUSE_DELAY              = 200;       // Pause between error blink cycles (ms)
constexpr uint8_t ERROR_BLINK_CYCLE              = 10;        // Total blinks per error cycle

const uint8_t LED_WARMUP_DEFAULT_FLASHES         = 6;         // Number of expected flashes from sensor readings at program start
const uint16_t LED_WARMUP_MANUAL_FLASH_DELAY_MS  = 100;       // How long the single flash at program start for one-sample burst

// ADC and voltage definitions
constexpr uint16_t MAX_ADC_VALUE                 = 1024;      // Maximum ADC reading (10-bit resolution)
constexpr float REFERENCE_VOLTAGE                = 3.3f;      // ADC reference voltage (V)
constexpr uint8_t BATTERY_VOLTAGE_MULTIPLIER     = 2;         // Voltage divider compensation factor

// Buffer and data definitions
constexpr uint8_t BUFFER_SIZE                    = 36;        // Circular buffer size (limited by 32u4 SRAM, only 2560 bytes)
constexpr uint8_t BUFFER_WRITE_COUNT             = 32;        // Buffer and data definitions 32
constexpr uint8_t FILENAME_LENGTH                = 13;        // Filename length, limited by FAT16 filesystem to 8.3 format (13th char for null terminator)
constexpr uint16_t FRESHWATER_DENSITY            = 997;       // Freshwater density (kg/m³) for pressure calculations
constexpr uint16_t SALTWATER_DENSITY             = 1025;      // Saltwater density (kg/m³) for pressure calculations

// Error code definitions
constexpr uint8_t ERROR_SD_CARD_FAILED           = 1;         // Error code for SD card initialization failure
constexpr uint8_t ERROR_FILE_OPEN_FAILED         = 2;         // Error code for file creation failure

constexpr uint8_t SERIAL_NUMBER_ADDRESS          = 0;         // EEPROM address for device serial number

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
// GLOBAL VARIABLE DECLARATIONS
// ===========================
#if DELAY_START
  extern bool hasStarted;
#endif

#if USE_NEW_SENSOR
  extern MS5837 newSensor;
#else
  extern MS5803 oldSensor;
#endif

#if BURST_SAMPLING
  extern TimeSpan elapsed;
#endif

extern RTC_DS3231 rtc;
extern File outputFile;            // Used to open, write to, and close files on the SD card

// Circular buffer variables
extern DataPoint dataBuffer[BUFFER_SIZE];
extern int bufferHead;             // Points to next write position
extern int bufferTail;             // Points to next read position
extern int bufferCount;            // Number of items in buffer
extern bool bufferOverflow;        // Flag to indicate buffer overflow

extern DateTime timeAtBurstSwitch; // Time burst switched between sleep and record
extern DateTime currentSecond;     // Time of current second
extern float currentVoltage;       // Voltage of battery, updated every second

// Sampling flag for ISR to main loop communication
extern volatile bool samplingFlag;
extern volatile bool resetTimerFlag;
extern volatile bool deepSleepFlag;
extern volatile bool burstSleepFlag;
extern volatile bool sleeping;

// Number of milliseconds at last 1 Hz interrupt from RTC
extern unsigned long millisAtInterrupt;

// # of times the LED should be toggled (switched between on/off) during the start-up verification phase
extern uint8_t ledWarmupToggleTarget;

// Current # of times the LED has been toggled
extern uint8_t ledWarmupToggleCount;

// True if LED should be toggled only once (blocking for LED_WARMUP_MANUAL_FLASH_DELAY_MS) during start-up
extern bool ledWarmupManualPulsePending;

// ===========================
// FUNCTION DECLARATIONS
// ===========================

/**
 * performSensorReading
 * Purpose: Read pressure and temperature from the active sensor and enqueue a data point with timestamp and battery voltage.
 * Inputs:
 *   - None (uses globals: currentSecond, millisAtInterrupt, currentVoltage, currentPressure, currentTemperature)
 * Usage: Called when `samplingFlag` is set or in one-sample burst mode.
 */
void performSensorReading();

/**
 * getBatteryVoltage
 * Purpose: Measure battery voltage via ADC on `BATTERY_VOLTAGE_PIN` using divider ratio.
 * Inputs: None
 * Returns: float — battery voltage in volts.
 * Usage: Call once per second or as needed; ADC MUST enabled/disabled by caller.
 */
float getBatteryVoltage();

/**
 * resetTimer
 * Purpose: Synchronize timer-driven sampling to the RTC second tick; updates timekeeping and schedules next RTC alarm.
 * Inputs: None
 * Usage: Call after every RTC interrupt or when starting new sampling windows.
 */
void resetTimer();

/**
 * makeFileName
 * Purpose: Generate a daily-rotating CSV filename in the form MM-DD-XX.csv where XX increments from 00..99.
 * Inputs:
 *   - fileName: char* (out) — caller-provided buffer of length FILENAME_LENGTH.
 * Usage: Call once during setup before opening the SD file.
 */
void makeFileName(char* fileName);

/**
 * setTimeStamp
 * Purpose: Provide FAT filesystem date/time for file creation/modification via SD library callback.
 * Inputs:
 *   - date: uint16_t* (out) — FAT date packed with year, month, day.
 *   - time: uint16_t* (out) — FAT time packed with hour, minute, second.
 * Usage: Registered as SdFile::dateTimeCallback handler.
 */
void setTimeStamp(uint16_t* date, uint16_t* time);

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
void writeToOutputFile(DateTime now, int millisec, float pressure, float temperature, float batteryVoltage);

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
void addToBuffer(DateTime now, int millisec, float pressure, float temperature, float batteryVoltage);

/**
 * readFromBuffer
 * Purpose: Pop the oldest valid data point from the circular buffer.
 * Inputs:
 *   - data: DataPoint* (out) — caller-allocated struct to receive data.
 * Returns: bool — true if a valid data point was returned; false if buffer empty.
 * Usage: Use to remove and return the oldest data point from the buffer.
 */
bool readFromBuffer(DataPoint* data);

/**
 * deepSleepLog
 * Purpose: Handle wake events during delayed start: if start time reached, initialize sampling; otherwise log a sample and return to deep sleep.
 * Inputs: None
 * Usage: Called from loop when `deepSleepFlag` is set by RTC interrupt during DELAY_START.
 */
void deepSleepLog();

/**
 * enterDelayDeepSleep
 * Purpose: Configure RTC alarms for the target start date/time and enter deep sleep until then.
 * Inputs: None
 * Usage: Used when DELAY_START is enabled.
 */
void enterDelayDeepSleep();

/**
 * enterBurstDeepSleep
 * Purpose: Schedule next wake time after a burst write window and enter deep sleep until then.
 * Inputs:
 *   - endTime: DateTime — timestamp when the last write window ended.
 * Usage: Used only when BURST_SAMPLING is enabled.
 */
void enterBurstDeepSleep(DateTime endTime);

/**
 * setForeverIdleSleep
 * Purpose: Enter idle sleep indefinitely while keeping timers 0/1 running for millis tracking and sampling cadence.
 * Inputs: None
 * Usage: Use at end of loop to minimize power between events while preserving timer state.
 */
void setForeverIdleSleep();

/**
 * error
 * Purpose: Indicate unrecoverable error by blinking LED a number of times equal to `errno`, repeated forever.
 * Inputs:
 *   - errno: uint8_t — error code and blink count.
 * Usage: Call on fatal conditions; function does not return.
 */
void error(uint8_t errno);

/**
 * configureLedWarmupIndicator
 * Purpose: Set the correct number of flashes at start-up for the current configuration, used to verify
 *          that the wave gauge is working correctly without serial port access.
 * Inputs: None, configures global variables ledWarmupToggleCount, ledWarmupToggleTarget, ledWarmupManualPulsePending
 * Usage: Run once within setup()
 */
void configureLedWarmupIndicator();

/**
 * updateLedWarmupIndicator
 * Purpose: Toggle LED_PIN at program start for a set number of sensor readings
 * Inputs: None, uses global variables ledWarmupToggleCount, ledWarmupToggleTarget, ledWarmupManualPulsePending
 * Usage: Run within performSensorReadings() if LED diagnostics are desired
 */
void updateLedWarmupIndicator();

/**
 * triggerSampling (ISR context)
 * Purpose: Flag main loop to perform a sensor read (Timer1 compare interrupt).
 * Inputs: None
 */
void triggerSampling();

/**
 * resetTimerInterrupt (ISR context)
 * Purpose: Flag main loop to resynchronize timers on each RTC second tick.
 * Inputs: None
 */
void resetTimerInterrupt();

/**
 * deepSleepInterrupt (ISR context)
 * Purpose: Flag main loop that the RTC alarm fired during delay-start deep sleep.
 * Inputs: None
 */
void deepSleepInterrupt();

/**
 * burstSleepInterrupt (ISR context)
 * Purpose: Flag main loop that the RTC alarm fired to end burst sleep.
 * Inputs: None
 */
void burstSleepInterrupt();

#endif
