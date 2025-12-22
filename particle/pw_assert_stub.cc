// Minimal pw_assert stub for Particle P2 firmware
// This provides assert handler functions without depending on pw_sys_io.

#include <cstdlib>

extern "C" {

// Called by pw_assert_basic when an assert fails
void pw_assert_basic_HandleFailure(const char* /* file */,
                                    int /* line */,
                                    const char* /* function */,
                                    const char* /* message */) {
    while (1) {
        __asm volatile("bkpt #0");
    }
}

// Called by PW_ASSERT/PW_DASSERT macros
void pw_assert_HandleFailure() {
    while (1) {
        __asm volatile("bkpt #0");
    }
}

}  // extern "C"
