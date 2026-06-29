#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include <stdint.h>

// ===========================
// USER DEFINED FLAGS:
// Please set the following three flags according to your sensor type and preferred collection type.
// Type "true" (without quotes) or "false" (without quotes)
// ===========================

// Set to false for rapid start
// Set to true for delayed start, using selected start date
#ifndef DELAY_START
  #define DELAY_START                  false
#endif

// Set to true for burst sampling
// Set to false for constant sampling
#ifndef BURST_SAMPLING
  #define BURST_SAMPLING               false
#endif

// Set to true if only one sample should be taken at each burst
// Set to false if samples should be taken according to SAMPLE_FREQ during the
// write period
#ifndef BURST_SAMPLING_ONE_SAMPLE
  #define BURST_SAMPLING_ONE_SAMPLE    false
#endif

// ===========================
// USER INPUTS:
// Please set the below variables to reflect your sampling preferences.
// ===========================
constexpr uint8_t SAMPLE_FREQ = 16;   // Sampling frequency in Hz (16 Hz maximum)

// Burst sampling alternates between writing and sleeping according to set periods below
constexpr uint32_t WRITE_SECONDS = 1;   // Number of seconds to sample data in burst sampling

constexpr uint32_t SLEEP_SECONDS = 10;  // Number of seconds to sleep in burst sampling

// Edit for DELAY start ONLY
// Do not include leading zeroes. (e.g. for January, START_MONTH = 1, not START_MONTH = 01)
#if DELAY_START // Date to start sampling
  const int START_YEAR   = 2025; // Year to start sampling
  const int START_MONTH  = 11;   // Month to start sampling
  const int START_DAY    = 19;   // Day to start sampling
  const int START_HOUR   = 10;   // Hour to start sampling (24-hr format)
  const int START_MINUTE = 0;    // Minute to start sampling
#endif

#endif
