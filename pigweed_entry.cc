// Copyright 2024 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

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
