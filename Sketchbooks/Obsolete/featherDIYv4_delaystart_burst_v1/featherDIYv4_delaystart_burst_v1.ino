#include <Wire.h>
#include <SD.h>
#include "MS5837.h"
#include <RTClib.h>
#include <TimerOne.h>

/* User Inputs - This is the burst sampling code! */
float sampleFreq = 10; // sampling frequency in Hz
float writeMinutes = 1; // number of minutes to sample
float sleepMinutes = 1; // number of minutes to sleep
const int startYear = 2024; // year for start sampling
const int startMonth = 11; // month for start sampling
const int startDay = 6; // day for start sampling
const int startHour = 8; // hour for start sampling on 24-hr format
const int startMinute = 13; // minute for start sampling

/* Calculations */
unsigned long writeDuration = writeMinutes * 60 * sampleFreq; // change the first number only
unsigned long sleepDuration = sleepMinutes * 60 * 1000UL; // 20 minutes in milliseconds

#define cardSelect 4 // for SD card
MS5837 pressuresensor; //Initializing global variables
RTC_DS3231 rtc;
File datafile;

// Pin where the battery voltage is read
const int batteryPin = A9;  // Replace with the correct analog pin

// Reference voltage for the ADC (3.3V or 5V depending on your setup)
const float referenceVoltage = 3.3;

// The max ADC value for a 10-bit ADC (0-1023)
const int maxADCValue = 1024;

//Set Variables for Intermittant SD logging
String Stringfile = String("");
int n=1;
int i=1;
int j=1;
int k=1;
volatile boolean flag = true;
int Year,Month,Day,Hour,Minute,Second; // variables to differentiate time elements
char filename[15]; //Creates an array of size 15 filled with elements of type 'char'

void dateTime(uint16_t* date, uint16_t* time) { //Void function for timestamp of .csv file
    DateTime now = rtc.now();
    *date = FAT_DATE(now.year(), now.month(), now.day());
    *time = FAT_TIME(now.hour(), now.minute(), now.second());
  }

void error (uint8_t errno) { //This will trigger the LED on pin 13 to blink a certain number of times. This is used to troubleshoot the code.
  while (1) {
  uint8_t i;
  for (i = 0; i < errno; i++) {
    digitalWrite(13, HIGH);
    delay(100);
    digitalWrite(13, LOW);
    delay(100);
    }
  for (i = errno; i < 10; i++) {
    delay(200);
    }
  }
}

void setup() {
  pinMode(13, OUTPUT); //This activates the red LED on pin 13
  pinMode(8, OUTPUT); //This activates the green LED on pin 8
  Serial.begin(9600);
  Serial.println("Starting!");
  Wire.begin();
  rtc.begin();
  pressuresensor.init();
  pressuresensor.setModel(MS5837::MS5837_02BA);
  pressuresensor.setFluidDensity(997); //Freshwater density = 997 kg/m^3 , Seawater density = 1029 kg/m^3
  Serial.print("Initializing SD card...");
 
  DateTime now = rtc.now();
  Year=now.year(); Month=now.month(); Day=now.day();
  Hour=now.hour(); Minute=now.minute(); Second=now.second();
  
  if (!SD.begin(cardSelect)) { //If sd card is not present then this if statement will be triggered
    Serial.println("Card Failed, or Not Present");
    error(1);
    return;
  }
  
  float sampleTime = (1/sampleFreq)*1000000; // convert sampling frequency to microseconds
  Timer1.initialize(sampleTime); // sampling interval in microseconds
  Timer1.attachInterrupt(timerIsr); // service routine

  Serial.println("Card Initialized.");
  strcpy(filename, "LOG00.CSV");
  for (uint8_t i = 0; i < 100; i++) { //This for loop is used to create a new .csv file everytime the sd card is pulled out of card holder
    filename[3] = '0' + i / 10;
    filename[4] = '0' + i % 10;
    if (! SD.exists(filename)) {
    break;
    }
  }
  SdFile::dateTimeCallback(dateTime); //This uses the void function that timestamps the .csv file
  datafile = SD.open(filename, FILE_WRITE); //Opens .csv file
  if (datafile) {
    datafile.print("Timestamp,\"Pressure [mbar]\",\"Temp [deg C]\",\"Battery [VDC]\"");
    datafile.println();
    Serial.println("Header logged");
    } else {
    Serial.println("error opening datalog-case1");
    error(5);
  }
  Serial.println("READY!");
  delay(500);
  Serial.println("Datalogging!");
}

void loop() {


  // Time to begin?
  if((Year >= startYear) && (Month >= startMonth) && (Day >= startDay) && (Hour >= startHour) && (Minute >= startMinute)){

  // Write data for 10 minutes
  if (i < writeDuration) {

    // Read the raw ADC value
    int sensorValue = analogRead(batteryPin);
    
    // Calculate the battery voltage
    // The actual voltage divider ratio should be adjusted as needed
    float batteryVoltage = (sensorValue * referenceVoltage) / maxADCValue;

    // If using a voltage divider, multiply by the ratio to get the actual voltage
    // e.g., if using a 2:1 divider, multiply by 2
    float actualBatteryVoltage = batteryVoltage * 2;  // Adjust multiplier based on your divider


    if(n==11){
      Serial.print(Stringfile);
      datafile.print(Stringfile);
      datafile.flush();
      Stringfile.replace(Stringfile, "");
      n= n - 10;
    }
    else{ 
      // xxx
    }

    DateTime now = rtc.now();
    Year=now.year(); Month=now.month(); Day=now.day();
    Hour=now.hour(); Minute=now.minute(); Second=now.second();

      if(flag == true){
        Year=now.year(); Month=now.month(); Day=now.day();
        Hour=now.hour(); Minute=now.minute(); Second=now.second();
        pressuresensor.read();
        float temp2 = pressuresensor.temperature();
        float pres = pressuresensor.pressure();
        j=j+1;
        //if (j <= 400) {
          digitalWrite(8, HIGH); //Turns on green LED light to ensure that code has processed through to this point.
        // } else {
        //  digitalWrite(8, LOW); //Turns off the green LED light to conserve battery
        // }


        Stringfile += (Year);
        Stringfile += ('/');
        Stringfile += (Month);
        Stringfile += ('/');
        Stringfile += (Day);
        Stringfile += (" ");
        Stringfile += (Hour);
        Stringfile += (':');
        Stringfile += (Minute);
        Stringfile += (':');
        Stringfile += (Second);
        Stringfile += (",");
        Stringfile += (pres);
        Stringfile += (",");
        Stringfile += (temp2);
        Stringfile += (",");
        Stringfile += (actualBatteryVoltage);
        Stringfile += ("\r\n");
        Serial.println("I am logging");
        flag = false;
        n = n + 1;
        i = i + 1;
        j = j + 1;
      } else { 
        // do nothing
      }
  } else {
   // Sleep for 20 minutes
   delay(10);
    digitalWrite(8, LOW); //Turns off the green LED light to conserve battery
   Serial.println("I am sleeping");
   digitalWrite(13, HIGH);
   delay(sleepDuration);
   digitalWrite(13, LOW);
  //Watchdog.sleep(sleepDuration);
  i=1;
  }

    } else {
    // It is not time to begin sampling yet
    DateTime now = rtc.now();
    Year=now.year(); Month=now.month(); Day=now.day();
    Hour=now.hour(); Minute=now.minute(); Second=now.second();
    k=k+1;
    if (k <= 20) {  //Since the delay below is a total of ~1 s, this will last approximately 20 s
    digitalWrite(13, HIGH);
    Serial.println("I am waiting"); 
    // delay total of 1 second and blink the green led while we wait
    delay(500);
    digitalWrite(13, LOW);
    }
    delay(500);
    }
} // end void loop

void timerIsr() {
  flag = true;
  // just pause for logging interval... could put write statement here?
}
