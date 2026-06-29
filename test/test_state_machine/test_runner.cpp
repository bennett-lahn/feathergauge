#include <unity.h>
#include "feathergauge_code.h"

// Forward declarations of per-configuration test registration functions.
// Each is defined in its own .cpp file, guarded by the appropriate build flags.
// When compiled under an environment where the guard doesn't match, the function
// is a no-op so the linker still finds a definition.

void register_tests_continuous();
void register_tests_burst_multi();
void register_tests_burst_one();
void register_tests_delay_continuous();
void register_tests_delay_burst_multi();
void register_tests_delay_burst_one();

void setUp()    { mockResetAll(); }
void tearDown() {}

// Unity entry point; dispatches to config-specific test files.
// Each register_tests_* function is compiled for one PlatformIO environment (continuous, burst, delay, etc.).
int main(int argc, char** argv) {
    UNITY_BEGIN();
    register_tests_continuous();
    register_tests_burst_multi();
    register_tests_burst_one();
    register_tests_delay_continuous();
    register_tests_delay_burst_multi();
    register_tests_delay_burst_one();
    return UNITY_END();
}
