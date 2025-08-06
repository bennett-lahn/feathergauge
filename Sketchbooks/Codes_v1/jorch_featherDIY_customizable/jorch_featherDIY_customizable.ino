// ===========================
// USER-DEFINED FLAGS: 
// Please set the following three flags according to your sensor type and preferred collection type.
// ===========================
// Set to true for MS5837 (new pressure sensor)
// Set to false for MS5803 (old pressure sensor)
#define USE_NEW_SENSOR false

// Set to false for rapid start
#define DELAY_START false
// TODO: re-implement burst sampling

// ===========================
// USER INPUTS:
// Please set the below variables to reflect your sampling preferences.
// ===========================
float sampleFreq = 16; // Sampling frequency in Hz

// Edit for DELAY start ONLY
#if DELAY_START // Preferred date
  const int startYear = 2025; // Year to start sampling
  const int startMonth = 4; // Month to start sampling
  const int startDay = 23; // Day to start sampling
  const int startHour = 11; // Hour to start sampling (24-hr format)
  const int startMinute = 33; // Minute to start sampling

#else // Early dummy date to satisfy later conditional check
  const int startYear = 2000; // DO NOT MODIFY
  const int startMonth = 1; // DO NOT MODIFY
  const int startDay = 1; // DO NOT MODIFY
  const int startHour = 12; // DO NOT MODIFY
  const int startMinute = 0; // DO NOT MODIFY
#endif

// =========================== USER: DO NOT EDIT BELOW THIS LINE ===========================

// ===========================
// LIBRARIES
// ===========================
#include <Wire.h>
#include <SD.h>

#if USE_NEW_SENSOR
  #include "MS5837.h"
#else
  #include <SparkFun_MS5803_I2C.h>
#endif

// A note on using the SparkFun library:
// A very minor change to the SparkFun library is needed for this code to work. The sensorWait function in SparkFun_MS5803_I2C.h must be declared as virtual.
// This change is used to override sensorWait for better power management.

#include <RTClib.h>
#include <TimerOne.h>
#include <LowPower.h>

// Create custom MS5803 class to override sensorWait function for better power management
class CustomMS5803 : public MS5803 {
  public:
    void sensorWait(uint8_t time) {
      // sensorWait in SparkFun library is 1-10ms; the minimum sleep time is 16ms; THEORETICALLY, TWI activity should wake the MCU early
      // Turn off everything except timer 0 (used for millis), timer 1 (used for sampling), and TWI (used to communicate with pressure sensor)
      LowPower.idle(SLEEP_15MS, ADC_OFF, TIMER4_OFF, TIMER3_OFF, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
    }
};

// ===========================
// MAGIC NUMBER DEFINITIONS
// ===========================
// Pin definitions
#define LED_PIN 13
#define RTC_INTERRUPT_PIN 1
#define SD_CARD_SELECT_PIN 4
#define BATTERY_VOLTAGE_PIN A9

// Timing and frequency definitions
#define SERIAL_BAUD_RATE 9600
#define DELAY_START_CHECK_INTERVAL 500
#define ERROR_BLINK_DELAY 100
#define ERROR_PAUSE_DELAY 200
#define ERROR_BLINK_CYCLE 10

// ADC and voltage definitions
#define MAX_ADC_VALUE 1024
#define REFERENCE_VOLTAGE 3.3
#define BATTERY_VOLTAGE_MULTIPLIER 2

// Buffer and data definitions
#define BUFFER_SIZE 18              // The ATMega 32u4 only has 2560 bytes of SRAM...set buffer size accordingly
#define BUFFER_WRITE_COUNT 12       // Number of data points that must be present before write occurs; shooting for 512 bytes per write for most efficiency
#define FILENAME_LENGTH 18
#define MICROSECONDS_PER_SECOND 1000000
#define FRESHWATER_DENSITY 997
#define SALTWATER_DENSITY 1025

// Error code definitions
#define ERROR_SD_CARD_FAILED 1
#define ERROR_FILE_OPEN_FAILED 5

// ===========================
// MACRO SUBSTITUTION
// ===========================
#define cardSelect SD_CARD_SELECT_PIN // Chip select pin for SD card // DO NOT MODIFY

// ===========================
// DATA STRUCTURE FOR CIRCULAR BUFFER
// ===========================
struct DataPoint {
  DateTime now;
  int millisec;
  float pressure;
  float temperature;
  float batteryVoltage;
  bool valid; // Flag to indicate if this data point is valid
};

// ===========================
// INITIALIZING GLOBAL VARIABLES
// ===========================
#if USE_NEW_SENSOR
  MS5837 newSensor;
#else
  CustomMS5803 oldSensor;
#endif

RTC_DS3231 rtc;
File outputFile; // Used to open, write to, and close files on the SD card

const int batteryPin = BATTERY_VOLTAGE_PIN; // Pin where the battery voltage is read; replace with the correct analog pin as needed
const int maxADCValue = MAX_ADC_VALUE; // The max ADC value for a 10-bit ADC (0-1023)

const float referenceVoltage = REFERENCE_VOLTAGE; // Reference voltage for the ADC (3.3V or 5V depending on your setup)

// Circular buffer variables
volatile DataPoint dataBuffer[BUFFER_SIZE];
volatile int bufferHead = 0; // Points to next write position
volatile int bufferTail = 0; // Points to next read position
volatile int bufferCount = 0; // Number of items in buffer
volatile bool bufferOverflow = false; // Flag to indicate buffer overflow

int currYear,currMonth,currDay,currHour,currMin,currSec; // Variables for different time elements
int millisecAtInterrupt = 0; // Number of milliseconds at last 1 Hz interrupt from RTC

char fileName[FILENAME_LENGTH]; // Creates a char array for file name

float sampleTime = (1/sampleFreq)*MICROSECONDS_PER_SECOND; // Convert sampling frequency to microseconds

// ===========================
// SETUP - SENSOR, TIMESTAMP, SD CARD
// ===========================
void setup() {
  pinMode(LED_PIN, OUTPUT); // Activates the red LED on pin 13
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial); // Wait for serial connection to be established
  Wire.begin();
  
  if (!rtc.begin()) {
    Serial.println("RTC Failed to initialize");
    error(2);
    return;
  }
  
  rtc.writeSqwPinMode(DS3231_OFF); // Might be unnecessary

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

  Serial.print("Initializing SD card...");

  DateTime now = rtc.now();
  currYear = now.year(); currMonth = now.month(); currDay = now.day();
  currHour = now.hour(); currMin = now.minute(); currSec = now.second();
  
  if (!SD.begin(cardSelect)) { // If SD card is not present
    Serial.println("Card Failed or Not Present");
    error(ERROR_SD_CARD_FAILED);
    return;
  }
  
  // Initialize circular buffer
  for (int i = 0; i < BUFFER_SIZE; i++) {
    dataBuffer[i].valid = false;
  }

  Serial.println("Card Initialized.");
  makeFileName(fileName);
  Serial.print("File name: ");
  Serial.println(fileName);

  SdFile::dateTimeCallback(setTimeStamp); // Timestamps the .csv file (inserts as metadata)
  outputFile = SD.open(fileName, FILE_WRITE); // Opens .csv file

  if (outputFile) {
    outputFile.print("Timestamp,\"Pressure [mbar]\",\"Temp [deg C]\",\"Battery [VDC]\"");
    outputFile.println();
  } else {
    Serial.println("error opening datalog-case1");
    error(ERROR_FILE_OPEN_FAILED);
  }
  Serial.println("READY!");

  // DS3231 SQW pin requires an external pull-up resistor. The internal pull-up resistors included in the 32u4 are too weak 
  //for square wave output but should work for interrupts.
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  #if DELAY_START
    enterRTCDeepSleep();  
  #else
    rtc.setAlarm1(rtc.now(), DS3231_A1_PerSecond); // DateTime object shouldn't matter since we're setting the alarm to trigger every second
  #endif
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), resetTimerInterrupt, FALLING); // Interrupt on pin 1 (INT1) for timer reset / rtc alarm
    
  Timer1.initialize(sampleTime);
  Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling
}

// ===========================
// DATA COLLECTION LOOP
// ===========================
void loop() {
    // Check for buffer overflow warning
    if (bufferOverflow) {
      Serial.println("WARNING: Buffer overflow detected - some data may have been lost!");
      // TODO: Add some sort of flag to file to indicate that an error occurred
      bufferOverflow = false;
    }

    // Only write to SD card if buffer has at least 12 data points
    if (bufferCount > BUFFER_WRITE_COUNT - 1) {
      
      // Process data from circular buffer until empty
      DataPoint data;
      int writeCount = 0;
      while (readFromBuffer(&data) && writeCount < BUFFER_WRITE_COUNT) {
        writeToOutputFile(data.now, data.millisec, data.pressure, data.temperature, data.batteryVoltage);
        writeCount++;
      }
      
      // Flush data to SD card after writing all buffered data
      outputFile.flush();
      Serial.print("Wrote ");
      Serial.print(writeCount);
      Serial.println(" data points to SD card");
    }
    // Finished writing to SD card, sleep until next sampling time, then check buffer again
    setForeverIdleSleep();
}

// ===========================
// TRIGGER SAMPLING - TIMER INTERRUPT HANDLER
// ===========================
void triggerSampling(bool writeNow) {
    // Get current time
    DateTime now = rtc.now();
    
    // Read sensor data
    float temp2, pres;
    #if USE_NEW_SENSOR
      newSensor.read();
      temp2 = newSensor.temperature();
      pres = newSensor.pressure();
    #else
      temp2 = oldSensor.getTemperature(CELSIUS, ADC_512);
      pres = oldSensor.getPressure(ADC_4096);
    #endif
    
    // Read battery voltage
    int sensorValue = analogRead(batteryPin);
    float batteryVoltage = (sensorValue * referenceVoltage) / maxADCValue;
    float actualBatteryVoltage = batteryVoltage * BATTERY_VOLTAGE_MULTIPLIER; // Adjust multiplier based on your divider
    int millisec = millis() - millisecAtInterrupt;

    // If writeNow is true, this is a very long interrupt; okay as long as writeNow is only true for periodic logging when in DELAY_START mode
    if (writeNow) {
      writeToOutputFile(now, pres, temp2, actualBatteryVoltage, millisec);
      outputFile.flush();
    } else {
    // Add data to circular buffer
    addToBuffer(now, pres, temp2, actualBatteryVoltage, millisec);
  }
}

// ===========================
// RESET TIMER INTERRUPT
// ===========================
// Resets the timer interrupt every second according to RTC to prevent timer desyncing from the RTC
  void resetTimerInterrupt() {
    Timer1.initialize(sampleTime);
    Timer1.attachInterrupt(triggerSampling);
    millisecAtInterrupt = millis();
  }

  void makeFileName(char charArray[]) {
    // Create a date tag
    charArray[0] = '0' + currYear / 1000;
    charArray[1] = '0' + (currYear % 1000) / 100;
    charArray[2] = '0' + (currYear % 100) / 10;
    charArray[3] = '0' + currYear % 10;
    charArray[4] = '-';
    charArray[5] = '0' + currMonth / 10;
    charArray[6] = '0' + currMonth % 10;
    charArray[7] = '-';
    charArray[8] = '0' + currDay / 10;
    charArray[9] = '0' + currDay % 10;
    charArray[10] = '0';
    charArray[11] = '0';
    charArray[12] = '0';
    charArray[13] = '.';
    charArray[14] = 'C';
    charArray[15] = 'S';
    charArray[16] = 'V';
    charArray[17] = '\0';

  // Find the next available file index, then write the new name once
  for (uint8_t fileIndex = 0; fileIndex < 100; fileIndex++) {
    charArray[10] = '0' + fileIndex / 100;
    charArray[11] = '0' + (fileIndex % 100) / 10;
    charArray[12] = '0' + fileIndex % 10;
    if (! SD.exists(charArray)) {
      break;
    }
  }
}

// ===========================
// SETTING FILE DATE AND TIME
// ===========================
// Sets file creation/modification date and time for the SD card using the RTC
void setTimeStamp(uint16_t* date, uint16_t* time) {
  DateTime now = rtc.now();
  *date = FAT_DATE(now.year(), now.month(), now.day());
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

// ===========================
// ERRORS/TROUBLESHOOTING
// ===========================
// Triggers the LED on pin 13 to blink a certain number of times during unrecoverable errors
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
// CIRCULAR BUFFER FUNCTIONS
// ===========================
// Add data point to circular buffer (called from interrupt)
void addToBuffer(DateTime now, int millisec, float pressure, float temperature, float batteryVoltage) {
  // Check if buffer is full
  if (bufferCount >= BUFFER_SIZE) {
    bufferOverflow = true;
    // Move tail forward to make room (overwrite oldest data)
    bufferTail = (bufferTail + 1) % BUFFER_SIZE;
  } else {
    bufferCount++;
  }
  
  // Add new data point
  dataBuffer[bufferHead].now = now;
  dataBuffer[bufferHead].millisec = millisec;
  dataBuffer[bufferHead].pressure = pressure;
  dataBuffer[bufferHead].temperature = temperature;
  dataBuffer[bufferHead].batteryVoltage = batteryVoltage;
  dataBuffer[bufferHead].valid = true;
  
  // Move head to next position
  bufferHead = (bufferHead + 1) % BUFFER_SIZE;
}

// Read data point from circular buffer (called from main loop)
bool readFromBuffer(DataPoint* data) {
  if (bufferCount == 0) {
    return false; // Buffer is empty
  }
  
  // Copy data from buffer
  data->now = DateTime(dataBuffer[bufferTail].now.unixtime());
  data->pressure = dataBuffer[bufferTail].pressure;
  data->temperature = dataBuffer[bufferTail].temperature;
  data->batteryVoltage = dataBuffer[bufferTail].batteryVoltage;
  data->valid = dataBuffer[bufferTail].valid;
  
  // Mark as invalid and move tail
  dataBuffer[bufferTail].valid = false;
  bufferTail = (bufferTail + 1) % BUFFER_SIZE;
  bufferCount--;
  
  return true;
}

void writeToOutputFile(DateTime now, int millisec,float pressure, float temperature, float batteryVoltage) {
  // Write data to SD card
  outputFile.print(now.year());
  outputFile.print('/');
  outputFile.print(now.month());
  outputFile.print('/');
  outputFile.print(now.day());
  outputFile.print(" ");
  outputFile.print(now.hour());
  outputFile.print(':');
  outputFile.print(now.minute());
  outputFile.print(':');
  outputFile.print(now.second());
  outputFile.print(':');
  outputFile.print(millisec); // For now print raw millisec to see how many are over 1000
  // outputFile.print((data.millisec < 999) ? data.millisec : 999);
  outputFile.print(",");
  outputFile.print(pressure);
  outputFile.print(",");
  outputFile.print(temperature);
  outputFile.print(",");
  outputFile.print(batteryVoltage);
  outputFile.println();
}

void enterRTCDeepSleep() {
  // Set RTC alarm to correct date
  DateTime startDateTime(startYear, startMonth, startDay, startHour, startMinute, 0);
  rtc.setAlarm1(startDateTime, DS3231_A1_Date); // Alarm 1 triggers at the start time
  rtc.setAlarm2(startDateTime, DS3231_A2_Hour); // Alarm 2 triggers every hour, taking a sample to track when/if battery dies
  attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), deepSleepInterrupt, FALLING);
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

void deepSleepInterrupt() {
  DateTime now = rtc.now();
  currYear = now.year(); currMonth = now.month(); currDay = now.day();
  currHour = now.hour(); currMin = now.minute(); currSec = now.second();
  triggerSampling(true);
  // Use now to set alarm 2 to trigger an hour from now
  if (now.hour() == 23) {
    now = DateTime(now.year(), now.month(), now.day(), 0, 0, 0);
  } else {
    now = DateTime(now.year(), now.month(), now.day(), now.hour() + 1, now.minute() , now.second());
  }
  rtc.setAlarm2(now, DS3231_A2_Hour);
}

// Idle sleep should be used whenever millisecond tracking is needed or timer 1 is used for sampling
void setForeverIdleSleep() {
  LowPower.idle(SLEEP_FOREVER, ADC_OFF, TIMER4_OFF, TIMER3_OFF, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
}
