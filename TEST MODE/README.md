# Tactus TEST MODE

Separate firmware for the **Finish Touchpad ESP32**. Starts with the touchpad's
existing external RESET button, counts successful touchpad presses, and stops at
400. The starter is not required. Normal Finish Touchpad and Starter source files
are unchanged.

## Where this belongs

Keep this folder alongside your existing PlatformIO projects:

```text
Tactus/
  Finish Touchpad/    normal touchpad firmware
  Starter/           normal starter firmware
  TEST MODE/         this independent test firmware
    platformio.ini   build and upload settings
    src/test_mode_main.cpp
    include/test_mode_state.h
```

There is no separate Git branch to switch to. Each folder is its own firmware
project. Do not copy the test source into the normal touchpad's `src` folder.

## Flash it using your existing VS Code / PlatformIO setup

1. Connect the **touchpad ESP32** to your computer by USB. Disconnect the starter
   USB cable while uploading so the intended board is unambiguous.
2. In VS Code choose **File > Open Folder**, then select
   `TEST MODE` inside your local `Tactus` folder.
   Alternatively, use **File > Add Folder to Workspace** to keep all your projects
   open together, or open the included `Tactus Testing.code-workspace` after placing
   this folder alongside the two existing projects.
3. In the PlatformIO sidebar, expand **TEST MODE > esp32dev > General** and click
   **Upload**. Depending on the workspace view, `esp32dev` may appear directly
   under Project Tasks. Confirm you are uploading from **TEST MODE**.
4. Wait for the upload to finish. The touchpad screen should show
   **TACTUS | TEST MODE**, **0/400**, and **Press button to start**.

PlatformIO terminal alternative, from inside this folder:

```powershell
pio run -e esp32dev
pio run -e esp32dev -t upload
```

If more than one serial device remains connected, select the touchpad's port or
use `--upload-port COMn` with its actual port number. These commands follow the
[PlatformIO build/upload documentation](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html).

## Run a test

1. Press and release the touchpad's **external RESET button** (GPIO 5). This is the
   same button used after a normal swim; it is not the ESP32 board's EN or BOOT
   button. The counter starts at zero.
2. Press the pad, then fully release it before the next press. Each accepted press
   increases the large counter and briefly shows **Touch detected!** in green.
   The green LED also flashes. The red LED blinks during the test.
3. At **400/400**, **TEST COMPLETE** appears and the green LED stays on. Further
   pad presses do not change the count.
4. A fresh press of the external RESET button starts another test at zero.
   Button presses during a running test do not clear its count. Power cycling or
   the board's EN/reset button clears the count and returns to the waiting screen.

You can stop pressing at any number to read the counter and calculate your own
success rate: **detected presses / attempted presses x 100%**. Attempted presses
must be counted separately. Counts are kept in memory, not saved across power loss.

## What counts as a successful touch

- Same input wiring as normal mode: GPIO 15, active LOW, internal pull-up.
- Same **150 ms continuous contact** requirement as the normal touchpad firmware.
  A contact shorter than this is deliberately not counted.
- After a counted press, the pad must be released continuously for **30 ms** before
  it can count again. Holding the pad down counts only once; release chatter does
  not create extra counts.
- A pad already held when the test starts must first be released.
- The normal race's **3-second startup grace** and **2-second touch cooldown** are
  omitted for repetitive testing. This tests pad detection with the same contact
  validation, not race timing behavior.
- The button uses 50 ms debounce. A button held at power-on or through completion
  must be released before it can start a new run.
- No Wi-Fi / ESP-NOW setup or starter messages are used.

## Restore normal use

Open the existing **Finish Touchpad** folder in VS Code and use its PlatformIO
**Upload** action to flash your normal firmware again. Flashing changes the
program on the touchpad; both firmware projects remain on your computer.
The starter firmware does not need to be changed.

## Build and verification

Target: `esp32dev`, Espressif32 platform 6.13.0, Arduino framework. Display uses
the existing 20 MHz hardware SPI setup and rotation 3 (320 x 240 landscape), with
the same black, white, cyan, gray, and green style. Only changed counter/status
areas redraw during a test.

Validation uses the installed Adafruit ILI9341 1.6.3, Adafruit GFX 1.12.6, and
Adafruit BusIO 1.17.4 libraries. `test/test_counter.cpp` checks the actual state
machine with ten C++14 compile-time scenario assertions: start gating, button
debounce, short contacts, bounce, held contacts, 400-count limit, restart behavior,
late button presses, feedback timing, and clock rollover. These tests need no
board. They can be compiled with the ESP32 compiler using this folder's `include`
directory. They are not included in the firmware binary by PlatformIO.

The project has been compiled, but physical button, touchpad, LEDs, and display
operation still need to be checked on your device after upload.
