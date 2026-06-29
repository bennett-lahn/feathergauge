#pragma once

#include "mock_registry.h"

// Enums matching the LowPower library API used by idle() and powerDown() in the firmware.

enum period_t  { SLEEP_15MS, SLEEP_30MS, SLEEP_60MS, SLEEP_120MS,
                 SLEEP_250MS, SLEEP_500MS, SLEEP_1S, SLEEP_2S,
                 SLEEP_4S, SLEEP_8S, SLEEP_FOREVER };
enum adc_t     { ADC_OFF, ADC_ON };
enum bod_t     { BOD_OFF, BOD_ON };
enum timer0_t  { TIMER0_OFF, TIMER0_ON };
enum timer1_t  { TIMER1_OFF, TIMER1_ON };
enum timer2_t  { TIMER2_OFF, TIMER2_ON };
enum timer3_t  { TIMER3_OFF, TIMER3_ON };
enum timer4_t  { TIMER4_OFF, TIMER4_ON };
enum spi_t     { SPI_OFF, SPI_ON };
enum usart0_t  { USART0_OFF, USART0_ON };
enum usart1_t  { USART1_OFF, USART1_ON };
enum twi_t     { TWI_OFF, TWI_ON };
enum usb_t     { USB_OFF, USB_ON };

// Stand-in for LowPower: sleep calls are no-ops but counted in mockReg.audit for state-machine tests.
// Real firmware would halt the CPU; native loop() returns immediately after idle()/powerDown().
struct MockLowPower {
    // Deep sleep between burst windows or during delay start; increments mockReg.audit.powerDownCalled.
    // Burst and delay-start modes sleep for long intervals; the counter proves enterBurstDeepSleep() ran.
    void powerDown(period_t, adc_t, bod_t) {
        mockReg.audit.powerDownCalled++;
    }

    // Light sleep at end of each loop() iteration; increments mockReg.audit.idleCalled.
    // Continuous mode calls idle() every second; state-machine tests assert exactly one idle per loop().
    void idle(period_t, adc_t, timer4_t, timer3_t, timer1_t, timer0_t,
              spi_t, usart1_t, twi_t, usb_t) {
        mockReg.audit.idleCalled++;
    }
};

inline MockLowPower LowPower;
