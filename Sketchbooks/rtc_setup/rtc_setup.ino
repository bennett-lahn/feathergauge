// RTC Setup Program for Feather 32u4
// This program sets the RTC clock via serial communication
// It waits for a time string from the host computer and sets the RTC accordingly

#include "RTClib.h"

RTC_DS3231 rtc;

// Serial communication settings
const unsigned long SERIAL_TIMEOUT = 30000; // 30 seconds timeout
const int BAUD_RATE = 57600;

// LED pin for status indication
#define LED_PIN 13

void setup() {
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Initialize serial communication
  Serial.begin(BAUD_RATE);
  
  // Wait for serial port to connect (needed for native USB)
  while (!Serial) {
    delay(10);
  }
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("ERROR: Couldn't find RTC");
    blinkError(3);
    while (1) delay(10);
  }
  
  Serial.println("RTC Setup Program Ready");
  Serial.println("Send time in format: YYYY-MM-DD HH:MM:SS");
  Serial.println("Example: 2024-01-15 14:30:00");
  Serial.println("Waiting for time input...");
  
  // Blink LED to indicate ready
  blinkReady();
}

void loop() {
  if (Serial.available()) {
    String timeString = Serial.readStringUntil('\n');
    timeString.trim();
    
    if (timeString.length() > 0) {
      Serial.print("Received: ");
      Serial.println(timeString);
      
      if (setRTCTime(timeString)) {
        Serial.println("SUCCESS: RTC time set successfully");
        displayCurrentTime();
        blinkSuccess();
        delay(200);
        Serial.println("RTC setup complete. Ready for main program.");
        digitalWrite(LED_PIN, HIGH); // Keep LED on to indicate completion
        while (1) delay(1000); // Stay in loop
      } else {
        Serial.println("ERROR: Invalid time format");
        Serial.println("Expected format: YYYY-MM-DD HH:MM:SS");
        blinkError(5);
      }
    }
  }
  
  // Blink LED every 2 seconds while waiting
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 2000) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
  }
}

bool setRTCTime(String timeString) {
  // Expected format: YYYY-MM-DD HH:MM:SS
  // Example: 2024-01-15 14:30:00
  
  if (timeString.length() != 19) {
    return false;
  }
  
  // Check for proper format
  if (timeString.charAt(4) != '-' || timeString.charAt(7) != '-' || 
      timeString.charAt(10) != ' ' || timeString.charAt(13) != ':' || 
      timeString.charAt(16) != ':') {
    return false;
  }
  
  // Extract components
  int year = timeString.substring(0, 4).toInt();
  int month = timeString.substring(5, 7).toInt();
  int day = timeString.substring(8, 10).toInt();
  int hour = timeString.substring(11, 13).toInt();
  int minute = timeString.substring(14, 16).toInt();
  int second = timeString.substring(17, 19).toInt();
  
  // Validate ranges
  if (year < 2000 || year > 2100 || 
      month < 1 || month > 12 || 
      day < 1 || day > 31 || 
      hour < 0 || hour > 23 || 
      minute < 0 || minute > 59 || 
      second < 0 || second > 59) {
    return false;
  }
  
  // Set the RTC
  DateTime newTime(year, month, day, hour, minute, second);
  rtc.adjust(newTime);
  
  return true;
}

void displayCurrentTime() {
  DateTime now = rtc.now();
  
  Serial.print("Current RTC time: ");
  Serial.print(now.year());
  Serial.print("-");
  if (now.month() < 10) Serial.print("0");
  Serial.print(now.month());
  Serial.print("-");
  if (now.day() < 10) Serial.print("0");
  Serial.print(now.day());
  Serial.print(" ");
  if (now.hour() < 10) Serial.print("0");
  Serial.print(now.hour());
  Serial.print(":");
  if (now.minute() < 10) Serial.print("0");
  Serial.print(now.minute());
  Serial.print(":");
  if (now.second() < 10) Serial.print("0");
  Serial.println(now.second());
  
  Serial.print("Unix timestamp: ");
  Serial.println(now.unixtime());
}

void blinkReady() {
  // 3 quick blinks to indicate ready
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
}

void blinkSuccess() {
  // 5 quick blinks to indicate success
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

void blinkError(int count) {
  // Blink LED to indicate error
  for (int i = 0; i < count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    delay(500);
  }
}
