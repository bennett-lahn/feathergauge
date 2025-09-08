// Must define a malloc failed hook function

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
#define SAMPLE_FREQ 1.0 // Sampling frequency in Hz

// Edit for DELAY start ONLY
#if DELAY_START // Preferred date
  const uint16_t startYear = 2025; // Year to start sampling
  const uint16_t startMonth = 4; // Month to start sampling
  const uint16_t startDay = 23; // Day to start sampling
  const uint16_t startHour = 11; // Hour to start sampling (24-hr format)
  const int startMinute = 33; // Minute to start sampling

#else // Early dummy date to satisfy later conditional check
  const uint16_t startYear = 2000; // DO NOT MODIFY
  const uint16_t startMonth = 1; // DO NOT MODIFY
  const uint16_t startDay = 1; // DO NOT MODIFY
  const uint16_t startHour = 12; // DO NOT MODIFY
  const uint16_t startMinute = 0; // DO NOT MODIFY
#endif

// =========================== USER: DO NOT EDIT BELOW THIS LINE ===========================

// ===========================
// LIBRARIES
// ===========================
#include <Arduino_FreeRTOS.h>
#include "FreeRTOSConfig.h"
#include <queue.h>
#include <semphr.h>

#include <Wire.h> // Arduino library
#include <SPI.h> // Arduino library
#include <SD.h> // Arduino library
#include <EEPROM.h> // Arduino library (?)

// A note on using the SparkFun library:
// A very minor change to the SparkFun library is needed for this code to work. The sensorWait function in SparkFun_MS5803_I2C.h must be declared as virtual.
// This change is used to override sensorWait for better power management.

#if USE_NEW_SENSOR
  #include <MS5837.h>
#else
  #include <SparkFun_MS5803_I2C.h>
#endif

#include <RTClib.h>
#include <TimerOne.h>
// #include <LowPower.h>

// ===========================
// MAGIC NUMBER DEFINITIONS
// ===========================
// Pin definitions
#define LED_PIN 13
#define RTC_INTERRUPT_PIN 1
#define SD_CARD_SELECT_PIN 4
#define BATTERY_VOLTAGE_PIN A9

// Timing and frequency definitions
#define SERIAL_BAUD_RATE 57600
#define DELAY_START_CHECK_INTERVAL 500
#define ERROR_BLINK_DELAY 100
#define ERROR_PAUSE_DELAY 200
#define ERROR_BLINK_CYCLE 10

// ADC and voltage definitions
#define MAX_ADC_VALUE 1024
#define REFERENCE_VOLTAGE 3.3
#define BATTERY_VOLTAGE_MULTIPLIER 2

// Buffer and data definitions
#define QUEUE_SIZE 10              
#define QUEUE_WRITE_COUNT 2       
#define FILENAME_LENGTH 13
#define MICROSECONDS_PER_SECOND 1000000
#define FRESHWATER_DENSITY 997
#define SALTWATER_DENSITY 1025

// Error code definitions
#define ERROR_SD_CARD_FAILED 1
#define ERROR_FILE_OPEN_FAILED 5

#define SERIAL_NUMBER_ADDRESS 0

// FreeRTOS task priorities
#define SENSOR_TASK_PRIORITY 2
#define SD_WRITE_TASK_PRIORITY 1

// FreeRTOS task stack sizes
#define SENSOR_TASK_STACK_SIZE 150
#define SD_WRITE_TASK_STACK_SIZE 150

// ===========================
// MACRO SUBSTITUTION
// ===========================
#define cardSelect SD_CARD_SELECT_PIN // DO NOT MODIFY

// ===========================
// DATA STRUCTURE FOR FREERTOS QUEUE
// ===========================
struct DataPoint {
  int millisec;
  float pressure;
  float temperature;
  float batteryVoltage;
  DateTime now;
  bool valid;
};

// ===========================
// FREERTOS HANDLES AND GLOBAL VARIABLES
// ===========================
QueueHandle_t dataQueue;

// Task handles
TaskHandle_t sensorTaskHandle = NULL;
TaskHandle_t sdWriteTaskHandle = NULL;

// ===========================
// INITIALIZING GLOBAL VARIABLES
// ===========================
#if USE_NEW_SENSOR
  MS5837 newSensor;
#else
  MS5803 oldSensor;
#endif

RTC_DS3231 rtc;
File outputFile; // Used to open, write to, and close files on the SD card

// Sampling flag for ISR to main loop communication
volatile bool samplingFlag = false;
volatile bool millisResetFlag = false;

int millisecAtInterrupt = 0; // Number of milliseconds at last 1 Hz interrupt from RTC

float sampleTime = (1.0/SAMPLE_FREQ)*MICROSECONDS_PER_SECOND; // Convert sampling frequency to microseconds

// ===========================
// FREERTOS TASK FUNCTIONS
// ===========================
// High priority task for reading sensor values
void sensorTask(void *pvParameters) {
  (void) pvParameters;
  // Serial.println(F("Sensor task start"));
  
  for (;;) {
    // Wait for sampling flag from interrupt
    if (samplingFlag) {
      performSensorReading();
      samplingFlag = false;
    }
    
    // Handle millisecond reset flag
    if (millisResetFlag) {
      millisecAtInterrupt = millis();
      millisResetFlag = false;
    }
    
    // Yield to other tasks
    taskYIELD();
  }
}

// Low priority task for writing to SD card
void sdWriteTask(void *pvParameters) {
  (void) pvParameters;
  // Serial.println(F("SD task start"));
  
  for (;;) {
    // Check if queue has enough data points to write
    if (uxQueueMessagesWaiting(dataQueue) >= QUEUE_WRITE_COUNT) {
      
      // Process data from queue
      DataPoint data;
      uint8_t writeCount = 0;
      
      while (xQueueReceive(dataQueue, &data, 0) == pdTRUE && writeCount < QUEUE_WRITE_COUNT) {
        if (data.valid) {
          writeToOutputFile(data.now, data.millisec, data.pressure, data.temperature, data.batteryVoltage);
          writeCount++;
        }
      }
      
      // Flush data to SD card after writing all queued data
      if (writeCount > 0) {
        outputFile.flush();
        // Serial.println(F("Data written"));
      }
    }
    
    // Sleep for a short time to allow other tasks to run
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ===========================
// SETUP - SENSOR, TIMESTAMP, SD CARD, FREERTOS
// ===========================
void setup() {
  pinMode(LED_PIN, OUTPUT); // Activates the red LED on pin 13
  // pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  // Serial.begin(SERIAL_BAUD_RATE);
  // Serial.println(F("Init start"));
  Wire.begin();
  
  if (!rtc.begin()) {
    // Serial.println(F("RTC fail"));
    // error(2);
    return;
  }
  // Serial.println(F("RTC OK"));
  
  rtc.writeSqwPinMode(DS3231_OFF); // Might be unnecessary

  #if USE_NEW_SENSOR
    if (!newSensor.init()) {
      // Serial.println(F("Sensor fail"));
      // error(3);
      return;
    }
    newSensor.setModel(MS5837::MS5837_02BA);
    newSensor.setFluidDensity(FRESHWATER_DENSITY);
    // Serial.println(F("New sensor OK"));
  #else
    oldSensor.reset();
    oldSensor.begin();
    // Serial.println(F("Old sensor OK"));
  #endif
  
  // if (!SD.begin(cardSelect)) { // If SD card is not present
  //   Serial.println(F("SD fail or not present"));
  //   // error(ERROR_SD_CARD_FAILED);
  //   return;
  // }
  // Serial.println(F("SD started"));
  
  char fileName[FILENAME_LENGTH];
  makeFileName(fileName);
  
  // SdFile::dateTimeCallback(setTimeStamp); // Timestamps the .csv file (inserts as metadata)
  outputFile = SD.open(fileName, FILE_WRITE); // Opens .csv file

  if (outputFile) {
    // Serial.println(F("File open OK"));
    char serialNumber[16];
    EEPROM.get(SERIAL_NUMBER_ADDRESS, serialNumber);
    outputFile.print(F("W.G. Num: "));
    outputFile.print(serialNumber);
    outputFile.print(',');
    outputFile.print(F("Timestamp,\"Pressure [mbar]\",\"Temp [deg C]\",\"Battery [VDC]\""));
    outputFile.println();
    outputFile.flush();
  } else {
    // Serial.println(F("File fail"));
    // error(ERROR_FILE_OPEN_FAILED);
  }

  // Create FreeRTOS queue for data points
  dataQueue = xQueueCreate(QUEUE_SIZE, sizeof(DataPoint));
  if (dataQueue == NULL) {
    // Serial.println(F("Queue fail"));
    // error(4);
    return;
  }
  // Serial.println(F("Queue OK"));

  // Create sensor reading task (high priority)
  if (xTaskCreate(sensorTask, "SensorTask", SENSOR_TASK_STACK_SIZE, NULL, SENSOR_TASK_PRIORITY, &sensorTaskHandle) != pdPASS) {
    // Serial.println(F("Sensor task fail"));
    // error(5);
    return;
  }
  // Serial.println(F("Sensor task OK"));
  
  // Create SD writing task (low priority)
  if (xTaskCreate(sdWriteTask, "SDWriteTask", SD_WRITE_TASK_STACK_SIZE, NULL, SD_WRITE_TASK_PRIORITY, &sdWriteTaskHandle) != pdPASS) {
    // Serial.println(F("SD task fail"));
    // error(6);
    return;
  }
  // Serial.println(F("SD task OK"));

  // DS3231 SQW pin requires an external pull-up resistor. The internal pull-up resistors included in the 32u4 are too weak 
  // for square wave output but should work for interrupts.

  Timer1.initialize(sampleTime);
  Timer1.attachInterrupt(triggerSampling); // Every time Timer1 finishes counting down, calls triggerSampling
  // Serial.println(F("Timer OK"));

  #if DELAY_START
    enterRTCDeepSleep();  
  #else
    // DateTime now = rtc.now(); // Set alarm 1 to continue until now + 5 years (i.e. until battery dies)
    // rtc.setAlarm1(DateTime(now.year() + 5, now.month(), now.day(), now.hour(), now.minute(), now.second()), DS3231_A1_PerSecond); // DateTime object shouldn't matter since we're setting the alarm to trigger every second
  #endif
  
  // Start FreeRTOS scheduler
  vTaskStartScheduler();
  // Serial.println(F("Scheduler started"));
  
  // Should never reach here
  // error(7);
}

// ===========================
// MAIN LOOP (should never be reached with FreeRTOS)
// ===========================
void loop() {
  // This should never be reached as FreeRTOS scheduler takes over
}

// ===========================
// TRIGGER SAMPLING - TIMER INTERRUPT HANDLER
// ===========================
// ISR that sets a flag for sensor reading
void triggerSampling() {
    samplingFlag = true;
    // Debug: blink LED to show ISR is firing
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    // Serial.println(F("ISR"));
}

// ===========================
// RESET TIMER INTERRUPT
// ===========================
// Resets the timer interrupt every second according to RTC to prevent timer desyncing from the RTC
void resetTimerInterrupt() {
  Timer1.initialize(sampleTime);
  Timer1.attachInterrupt(triggerSampling);
  millisResetFlag = true;
  // Serial.println(F("Timer reset"));
}

// ===========================
// SENSOR READING FUNCTION
// ===========================
void performSensorReading() {
    DateTime now = rtc.now();
    
    float temp2, pres;
    #if USE_NEW_SENSOR
      newSensor.read();
      temp2 = newSensor.temperature();
      pres = newSensor.pressure();
    #else
      temp2 = oldSensor.getTemperature(CELSIUS, ADC_512);
      pres = oldSensor.getPressure(ADC_4096);
    #endif
    
    int sensorValue = analogRead(BATTERY_VOLTAGE_PIN);
    float batteryVoltage = (sensorValue * REFERENCE_VOLTAGE) / MAX_ADC_VALUE;
    float actualBatteryVoltage = batteryVoltage * BATTERY_VOLTAGE_MULTIPLIER; // Adjust multiplier based on your divider
    int millisec = millis() - millisecAtInterrupt;

    // Create data point and add to queue
    DataPoint dataPoint;
    dataPoint.now = now;
    dataPoint.millisec = millisec;
    dataPoint.pressure = pres;
    dataPoint.temperature = temp2;
    dataPoint.batteryVoltage = actualBatteryVoltage;
    dataPoint.valid = true;
    
    // Add to queue (non-blocking)
    if (xQueueSend(dataQueue, &dataPoint, 0) != pdTRUE) {
      // Serial.println(F("Queue full"));
    } else {
      // Serial.println(F("Data queued"));
    }
  }

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
// void error (uint8_t errno) {
//   while (1) {
//     uint8_t blinkCount; // Counts error blinks
//     for (blinkCount = 0; blinkCount < errno; blinkCount++) {
//       digitalWrite(LED_PIN, HIGH);
//       delay(ERROR_BLINK_DELAY);
//       digitalWrite(LED_PIN, LOW);
//       delay(ERROR_BLINK_DELAY);
//     }
//     for (blinkCount = errno; blinkCount < ERROR_BLINK_CYCLE; blinkCount++) {
//       delay(ERROR_PAUSE_DELAY);
//     }
//   }
// }

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

// void enterRTCDeepSleep() {
//   // Set RTC alarm to correct date
//   DateTime startDateTime(startYear, startMonth, startDay, startHour, startMinute, 0);
//   rtc.setAlarm1(startDateTime, DS3231_A1_Date); // Alarm 1 triggers at the start time
//   rtc.setAlarm2(startDateTime, DS3231_A2_Hour); // Alarm 2 triggers every hour, taking a sample to track when/if battery dies
//   attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), deepSleepInterrupt, FALLING);
//   // LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
// }

// void deepSleepInterrupt() {
//   DateTime now = rtc.now();
//   performSensorReading(); // Call sensor reading directly since we're not in main loop
//   // Use now to set alarm 2 to trigger an hour from now
//   if (now.hour() == 23) {
//     now = DateTime(now.year(), now.month(), now.day(), 0, 0, 0);
//   } else {
//     now = DateTime(now.year(), now.month(), now.day(), now.hour() + 1, now.minute() , now.second());
//   }
//   rtc.setAlarm2(now, DS3231_A2_Hour);
// }

// Idle sleep should be used whenever millisecond tracking is needed or timer 1 is used for sampling
// void setForeverIdleSleep() {
//   // LowPower.idle(SLEEP_FOREVER, ADC_OFF, TIMER4_OFF, TIMER3_OFF, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
// }
