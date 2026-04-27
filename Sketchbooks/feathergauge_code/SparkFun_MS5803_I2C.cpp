// **IMPORTANT** This library should only be used with feathergauge_code.ino,
// as it contains assumptions about processor state for sleep management.

/******************************************************************************
MS5803_I2C.cpp
Library for MS5803 pressure sensor.
Casey Kuhns @ SparkFun Electronics
6/26/2014
https://github.com/sparkfun/MS5803-14BA_Breakout

The MS58XX MS57XX and MS56XX by Measurement Specialties is a low cost I2C pressure
sensor.  This sensor can be used in weather stations and for altitude
estimations. It can also be used underwater for water depth measurements.

In this file are the functions in the MS5803 class

Resources:
This library uses the Arduino Wire.h to complete I2C transactions.

Development environment specifics:
	IDE: Arduino 1.0.5
	Hardware Platform: Arduino Pro 3.3V/8MHz
	MS5803 Breakout Version: 1.0

**Updated for Arduino 1.8.8 5/2019**

License: Please see LICENSE.md for more details.

Distributed as-is; no warranty is given.
******************************************************************************/

#include "SparkFun_MS5803_I2C.h"
#include "LowPower.h"

#define MS_TO_MICROSEC 1000

// Base library type I2C
MS5803::MS5803(ms5803_addr address) {
	_address = (uint8_t)address; // set interface used for communication
}

// Reset device I2C
void MS5803::reset(void) {
	if (_i2cPort == nullptr)
		return;

	sendCommand(CMD_RESET);
	sensorWait(3);
}

uint8_t MS5803::begin(TwoWire &wirePort, uint8_t address) {
	_address = address; // set interface used for communication
	return begin(wirePort);
}

// Initialize library for subsequent pressure measurements
uint8_t MS5803::begin(TwoWire &wirePort) {
	_i2cPort = &wirePort; // Grab which port the user wants us to use

	reset(); // Reset the sensor to ensure the coefficients are loaded correctly

	uint8_t i;
	for (i = 0; i <= 7; i++) {
		sendCommand(CMD_PROM + (i * 2));
		_i2cPort->requestFrom(_address, (uint8_t)2);
		uint8_t highByte = _i2cPort->read();
		uint8_t lowByte = _i2cPort->read();
		coefficient[i] = (highByte << 8) | lowByte;
		// Uncomment below for debugging output.
		//	Serial.print("C");
		//	Serial.print(i);
		//	Serial.print("= ");
		//	Serial.println(coefficient[i]);
	}

	return 0;
}

// Return a temperature reading in either F or C.
float MS5803::getTemperature(temperature_units units, precision _precision) {
	getMeasurements(_precision);
	float temperature_reported;
	// If Fahrenheit is selected return the temperature converted to F
	if (units == FAHRENHEIT) {
		temperature_reported = _temperature_actual / 100.0f;
		temperature_reported = (((temperature_reported)*9) / 5) + 32;
		return temperature_reported;
	}

	// If Celsius is selected return the temperature converted to C
	else {
		temperature_reported = _temperature_actual / 100.0f;
		return temperature_reported;
	}
}

// Return a pressure reading
float MS5803::getPressure(precision _precision) {
	getMeasurements(_precision);
	float pressure_reported;
	pressure_reported = _pressure_actual;		   // Units: 0.1mbar
	pressure_reported = pressure_reported / 10.0f; // Convert to mbar (float)
	return pressure_reported;
}

// The Sparkfun library takes both sensor readings for getPressure() and getTemperature(), effectively doubling the time/power it takes to read pressure and temp
// Most of this code is taken straight from different methods in the library, but combined to be more efficient
// The process/math for converting pressure and temperature is described in the MS5803 datasheet
void MS5803::getSensorReadings(temperature_units units, precision _precision_pres, precision _precision_temp, float *currentPressure, float *currentTemperature) {
   int32_t temperature_raw = getADCconversion(TEMPERATURE, _precision_temp);
   int32_t pressure_raw = getADCconversion(PRESSURE, _precision_pres);

   int32_t temp_calc;
   int32_t pressure_calc;
   int32_t dT;

   // Now that we have a raw temperature, let's compute our actual.
   dT = temperature_raw - ((int32_t)coefficient[5] << 8);
   temp_calc = (((int64_t)dT * coefficient[6]) >> 23) + 2000;

   // Now we have our first order Temperature, let's calculate the second order.
   int64_t T2, OFF2, SENS2, OFF, SENS; // working variables

   if (temp_calc < 2000) { // If temp_calc is below 20.0C
      T2 = (3 * ((int64_t)dT * dT)) >> 33;
      OFF2 = 3 * ((temp_calc - 2000) * (temp_calc - 2000)) / 2;
      SENS2 = 5 * ((temp_calc - 2000) * (temp_calc - 2000)) / 8;

      if (temp_calc < -1500) { // If temp_calc is below -15.0C
         OFF2 = OFF2 + 7 * ((temp_calc + 1500) * (temp_calc + 1500));
         SENS2 = SENS2 + 4 * ((temp_calc + 1500) * (temp_calc + 1500));
      }
   } else { // If temp_calc is above 20.0C
      T2 = (7 * ((uint64_t)dT * dT)) >> 37;
      OFF2 = ((temp_calc - 2000) * (temp_calc - 2000)) / 16;
      SENS2 = 0;
   }

   // Now bring it all together to apply offsets
   OFF = ((int64_t)coefficient[2] << 16) + (((coefficient[4] * (int64_t)dT)) >> 7);
   SENS = ((int64_t)coefficient[1] << 15) + (((coefficient[3] * (int64_t)dT)) >> 8);

   temp_calc = temp_calc - T2;
   OFF = OFF - OFF2;
   SENS = SENS - SENS2;

   // Now let's calculate the pressure
   pressure_calc = (((SENS * pressure_raw) / 2097152) - OFF) / 32768;

   _temperature_actual = temp_calc;
   _pressure_actual = pressure_calc;

   float pressure_reported;
   pressure_reported = _pressure_actual;		   // Units: 0.1mbar
   pressure_reported = pressure_reported / 10.0f; // Convert to mbar (float)
   *currentPressure = pressure_reported;

   float temperature_reported;
   if (units == FAHRENHEIT) {
      temperature_reported = _temperature_actual / 100.0f;
      temperature_reported = (((temperature_reported)*9) / 5) + 32;
      *currentTemperature = temperature_reported;
   } else { // If Celsius is selected return the temperature converted to C
      temperature_reported = _temperature_actual / 100.0f;
      *currentTemperature = temperature_reported;
   }
}

void MS5803::getMeasurements(precision _precision) {
	// Retrieve ADC result
	int32_t temperature_raw = getADCconversion(TEMPERATURE, _precision);
	int32_t pressure_raw = getADCconversion(PRESSURE, _precision);

	// Create Variables for calculations
	int32_t temp_calc;
	int32_t pressure_calc;

	int32_t dT;

	// Now that we have a raw temperature, let's compute our actual.
	dT = temperature_raw - ((int32_t)coefficient[5] << 8);
	temp_calc = (((int64_t)dT * coefficient[6]) >> 23) + 2000;

	// TODO TESTING  _temperature_actual = temp_calc;

	// Now we have our first order Temperature, let's calculate the second order.
	int64_t T2, OFF2, SENS2, OFF, SENS; // working variables

	if (temp_calc < 2000) { // If temp_calc is below 20.0C
		T2 = 3 * (((int64_t)dT * dT) >> 33);
		OFF2 = 3 * ((temp_calc - 2000) * (temp_calc - 2000)) / 2;
		SENS2 = 5 * ((temp_calc - 2000) * (temp_calc - 2000)) / 8;

		if (temp_calc < -1500) { // If temp_calc is below -15.0C
			OFF2 = OFF2 + 7 * ((temp_calc + 1500) * (temp_calc + 1500));
			SENS2 = SENS2 + 4 * ((temp_calc + 1500) * (temp_calc + 1500));
		}
	}
	else { // If temp_calc is above 20.0C
		T2 = (7 * ((uint64_t)dT * dT)) >> 37;
		OFF2 = ((temp_calc - 2000) * (temp_calc - 2000)) / 16;
		SENS2 = 0;
	}

	// Now bring it all together to apply offsets

	OFF = ((int64_t)coefficient[2] << 16) + (((coefficient[4] * (int64_t)dT)) >> 7);
	SENS = ((int64_t)coefficient[1] << 15) + (((coefficient[3] * (int64_t)dT)) >> 8);

	temp_calc = temp_calc - T2;
	OFF = OFF - OFF2;
	SENS = SENS - SENS2;

	// Now lets calculate the pressure

	pressure_calc = (((SENS * pressure_raw) / 2097152) - OFF) / 32768;

	_temperature_actual = temp_calc;
	_pressure_actual = pressure_calc; // 10;// pressure_calc;
}

uint32_t MS5803::getADCconversion(measurement _measurement, precision _precision) {
   // Retrieve ADC measurement from the device.
   // Select measurement type and precision
	if (_i2cPort == nullptr)
		return 0;
		
	uint32_t result;
	uint8_t highByte = 0, midByte = 0, lowByte = 0;

	sendCommand(CMD_ADC_CONV + _measurement + _precision);
	// Wait for conversion to complete
	sensorWait(1); // general delay
	switch (_precision) {
	case ADC_256:
		sensorWait(1);

		break;
	case ADC_512:
		sensorWait(2);
		break;
	case ADC_1024:
		sensorWait(3);
		break;
	case ADC_2048:
		sensorWait(6);
		break;
	case ADC_4096:
		sensorWait(10);
		break;
	}

	sendCommand(CMD_ADC_READ);
	_i2cPort->requestFrom(_address, (uint8_t)3);

	// TODO: What happens if I2C doesn't return 3 bytes?
	while (_i2cPort->available()) {
		highByte = _i2cPort->read();
		midByte = _i2cPort->read();
		lowByte = _i2cPort->read();
	}

	result = ((uint32_t)highByte << 16) | ((uint32_t)midByte << 8) | lowByte;

	return result;
}

void MS5803::sendCommand(uint8_t command)
{
	if (_i2cPort == nullptr)
		return;
		
	_i2cPort->beginTransmission(_address);
	_i2cPort->write(command);
	_i2cPort->endTransmission();
}

// IMPORTANT: This function REQUIRES timer0 to be tracking milliseconds to work correctly.
// ADC_256 waits for 1 ms, ADC_512 3 ms, ADC_1024 4 ms, ADC_2048 6 ms, AD_4096 10 ms
// time parameter is in ms
// Note: Setting `USB_OFF` may impact serial connection when debugging
void MS5803::sensorWait(uint8_t time) {
   unsigned long start = millis();
   while (millis() - start < time) {
      // In this sketch timer 0 wakes CPU from idle after 1 ms regardless of sleep time
      // Turn off everything except timer 0 (used for millis), timer 1 (used for sampling), and TWI (used to communicate with pressure sensor)
      // ADC, timer 3, and timer 4 are set to ON so LowPower library doesn't turn them back on, they are already off
      LowPower.idle(SLEEP_FOREVER, ADC_ON, TIMER4_ON, TIMER3_ON, TIMER1_ON, TIMER0_ON, SPI_OFF, USART1_OFF, TWI_ON, USB_OFF);
   }
}
