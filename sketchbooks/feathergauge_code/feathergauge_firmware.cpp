// This file allows the PlatformIO native platform (which does not process .ino files)
// to compile the firmware implementation.  It is a no-op for every other build because
// Arduino IDE and the PlatformIO Arduino framework compile feathergauge_code.ino directly.
#ifdef NATIVE_TEST
  #include "feathergauge_code.ino"
#endif
