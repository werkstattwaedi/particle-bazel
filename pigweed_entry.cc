// Copyright Offene Werkstatt Wädenswil
// SPDX-License-Identifier: MIT
//
// Particle Device OS entry point glue for Pigweed applications.
//
// This file provides the entry points that the Particle Device OS expects
// (setup, loop, module_user_init_hook, _post_loop) and bridges to the
// standard C++ main() function that Pigweed applications use.
//
// For Pigweed apps using pw_system, setup() calls main() which initializes
// the Pigweed system and never returns. The loop() function is never called.

extern "C" {

// Forward declaration of main() - provided by the Pigweed application.
int main();

// Called once during user module initialization.
void module_user_init_hook() {
    // No-op - Pigweed handles its own initialization in main().
}

// Called once after device initialization.
// This is where we call main() to start the Pigweed system.
void setup() {
    main();
    // main() should never return for pw_system based apps.
}

// Called repeatedly after setup().
// For Pigweed apps, this is never reached since setup() never returns.
void loop() {
    // No-op
}

// Called after each loop() iteration.
// For Pigweed apps, this is never reached since setup() never returns.
void _post_loop() {
    // No-op
}

}  // extern "C"
