/* eeprom_test.ino
	
	This sketch is used to print the EEPROM value over serial, used by
   program_feather_boards.sh for automated testing

*/
#include <EEPROM.h>

#define EEPROM_BUFFER_SIZE 16
#define EEPROM_SERIAL_NUM_ADDR 0
#define SERIAL_BAUD_RATE 57600

void setup(){
	Serial.begin(SERIAL_BAUD_RATE);
	
	// Wait for serial connection
	while (!Serial);
	
	char output[EEPROM_BUFFER_SIZE];
	EEPROM.get(EEPROM_SERIAL_NUM_ADDR, output);
	Serial.print("Read serial number from EEPROM: ");
	Serial.println(output);
			
}

void loop() {
	// Do nothing in main loop.
	while(1);
}
