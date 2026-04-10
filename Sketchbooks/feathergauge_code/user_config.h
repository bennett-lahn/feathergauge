#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// ===========================
// USER DEFINED FLAGS:
// Please set the following three flags according to your sensor type and preferred collection type.
// Type "true" (without quotes) or "false" (without quotes)
// ===========================

// Set to false for rapid start
// Set to true for delayed start, using selected start date
// Untested, DO NOT USE. Set to "false"
#define DELAY_START                  false

// Set to true for burst sampling
// Set to false for constant sampling
#define BURST_SAMPLING               false

// Set to true if only one sample should be taken at each burst
// Set to false if samples should be taken according to SAMPLE_FREQ during the
// write period
#define BURST_SAMPLING_ONE_SAMPLE    false

// ===========================
// USER INPUTS:
// Please set the below variables to reflect your sampling preferences.
// ===========================
constexpr uint8_t SAMPLE_FREQ = 16;   // Sampling frequency in Hz

// Burst sampling alternates between writing and sleeping according to set periods below
constexpr uint8_t writeSeconds = 1;   // Number of seconds to sample data in burst sampling

constexpr uint8_t sleepSeconds = 10;  // Number of seconds to sleep in burst sampling

// Edit for DELAY start ONLY
#if DELAY_START // Date to start sampling
  const int startYear   = 2025; // Year to start sampling
  const int startMonth  = 11;   // Month to start sampling
  const int startDay    = 19;   // Day to start sampling
  const int startHour   = 10;   // Hour to start sampling (24-hr format)
  const int startMinute = 0;    // Minute to start sampling
#else // Early dummy date to satisfy later conditional check
  const int startYear   = 2000; // DO NOT MODIFY
  const int startMonth  = 1;    // DO NOT MODIFY
  const int startDay    = 1;    // DO NOT MODIFY
  const int startHour   = 12;   // DO NOT MODIFY
  const int startMinute = 0;    // DO NOT MODIFY
#endif

#endif
