# Validation status and test procedure

## Software evidence

The starter, normal touchpad, and TEST MODE firmware have been built for
`esp32dev` using the Espressif32 6.13.0 platform. Source organization does not
change device logic. The normal firmware source files and test-mode logic are
preserved from their existing projects.

Build verification on October 18, 2026:

| Target | Build result | RAM reported by PlatformIO | Flash reported by PlatformIO |
| --- | --- | --- | --- |
| Starter | Pass | 44,116 bytes | 753,869 bytes |
| Finish Touchpad | Pass | 43,840 bytes | 748,353 bytes |
| TEST MODE | Pass | 21,744 bytes | 285,297 bytes |

These values describe the compiled images, not measured runtime memory peaks.

TEST MODE includes ten C++14 compile-time scenario assertions against the actual
state machine in `include/test_mode_state.h`:

1. Pad activity cannot count before the start button.
2. The button is debounced and a button held at boot cannot start a test.
3. Short and bouncing press signals do not bypass continuous-contact validation.
4. A held pad counts once; release bounce does not rearm it.
5. A pad held when starting must first be released.
6. Counting passes 255 correctly and freezes at exactly 400.
7. Active-run button presses preserve the count; a completed test can restart.
8. A button pressed just before completion cannot erase the result.
9. Feedback timing does not impose the normal race cooldown.
10. Debounce and feedback calculations handle the 32-bit clock rollover.

These checks execute during compilation; they do not require an attached ESP32.
For example, with a host C++14 compiler installed, run from the repository root:

```sh
g++ -std=c++14 -Wall -Wextra -Werror -I "TEST MODE/include" -fsyntax-only "TEST MODE/test/test_counter.cpp"
```

The same source can be checked using the ESP32 toolchain's
`xtensa-esp32-elf-g++`. The build must exit successfully with no failed
`static_assert`. There is no test executable to run. Firmware builds are a separate
check, as described in [flashing.md](flashing.md).

## Touchpad detection measurement

TEST MODE is intended to make repeated hardware testing practical. Record an
independent attempted-press count, then compare it with the display:

```text
Detection rate (%) = detected presses / attempted presses × 100
```

For each run, record the pad configuration, contact method, approximate press
duration, number of attempts, number detected, and relevant test conditions.
The counter stops at 400 successes. Power loss clears the result. The firmware
does not observe failed physical attempts, so it cannot calculate detection rate
by itself.

The 150 ms contact validation matches normal mode. TEST MODE additionally requires
a 30 ms release and omits normal mode's 3 s startup grace and 2 s cooldown.
Detection results must therefore be interpreted alongside those differences.

## Hardware validation still required

- Confirm the button, pad, display, and LEDs operate correctly on the assembled device.
- Characterize missed detections and unintended detections across representative contacts.
- Measure audio onset relative to the touchpad timer origin.
- Compare reported elapsed time with an independent reference; quantify bias and variability.
- Exercise wireless range, packet loss, reset, and abort behavior in the intended environment.
- Verify the physical start/pad arrangement for each supported pool and distance selection.

No measured detection rate, timing error, pool deployment result, or competition
certification is included in this repository. Software compilation establishes
buildability, not hardware performance.
