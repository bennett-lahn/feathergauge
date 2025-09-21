/* serial_number_generator.ino
	Luke Miller 2015-05-28
	
	This sketch is used to burn a  serial 
	number to the EEPROM of an AVR 32u4.
	
	The serial number is manually entered by the user and should match the
   identifier present on the corresponding wave gauge.
		
	After writing the serial number to the start of
	the eeprom (starting at address 0), it reads the
	value back and prints it out to the serial monitor.

*/
#include <EEPROM.h>

// Serial number buffer
char serialnumber[16] = "-1\0";
char userInput[16] = "";

void setup(){
	Serial.begin(9600);
	
	// Wait for serial connection
	while (!Serial);
	
	Serial.println("Serial Number Generator");
	Serial.print("Enter the serial number as it appears on the wave gauge (max 14 characters): ");
	
	// Wait for user input
	while (!Serial.available()); // wait for user input
	
	// Read user input
	int i = 0;
	while (Serial.available() && i < 31) {
		char c = Serial.read();
		if (c == '\n' || c == '\r') {
			break; // End of input
		}
		userInput[i] = c;
		i++;
	}
	userInput[i] = '\0'; // Null terminate the string
	
	// Generate serial number from user input
	initSN(userInput);
	Serial.print("Generated serial number: ");
	Serial.println(serialnumber);
	
	// Put the contents of serialnumber in EEPROM starting at
	// address 0
	EEPROM.put(0, serialnumber);
	
	char output[sizeof(serialnumber)];
	EEPROM.get(0, output);
	Serial.print("Read serial number from EEPROM: ");
	Serial.println(output);
			
}

void loop() {
	// Do nothing in main loop.
	while(1);
}

//------------------------------------------------------------------------------
// initSN - a function to create a serialnumber based on user input
// The character array 'serialnumber' was defined as a global array 
// at the top of the sketch.
void initSN(char* input) {
	// Clear the serial number array
	memset(serialnumber, 0, sizeof(serialnumber));
	
	// Copy user input to serial number
	strcpy(serialnumber, input);
	
	// Ensure the string is properly null-terminated
	serialnumber[sizeof(serialnumber) - 1] = '\0';
	
} // end of initSN function