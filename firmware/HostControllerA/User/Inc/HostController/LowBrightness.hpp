#pragma once

// Drives LOW_POWER_ENABLE (PB8). High turns on Q701, which shorts R705 in the
// light-sensor divider and lowers LED_BRIGHTNESS on every board of the chain,
// about 1.17 V -> 0.49 V in bright light. The divider still follows the light.
//
// The net is bussed to all boards, so only the HostController drives it; the
// DisplayController releases its PB8 at startup (Hardware_Review.md M-4).
//
// Both controls, the console's 'display low on|off' and switch 2, save the
// state, and AppVariant applies it at boot. This module only drives the pin;
// the callers save. Safe to call from any task, since the write is a single
// BSRR store.
namespace LowBrightness {

void set(bool enabled);
bool isEnabled();
// Returns the new state.
bool toggle();

} // namespace LowBrightness
