# Build and flashing guide

## Open the complete project

Clone or download the repository, then open **Tactus.code-workspace** in VS Code.
The workspace contains the project overview plus three independently buildable
PlatformIO projects: **Starter**, **Finish Touchpad**, and **TEST MODE**.

The repository root is an organizing folder, not a fourth firmware target.
Each device project has its own `platformio.ini`. Install VS Code's PlatformIO
extension if it is not already available.

## Build from VS Code

1. Open the PlatformIO sidebar.
2. Under Project Tasks, locate the intended project and expand its `esp32dev` environment.
3. Choose **General → Build**.
4. Check that the build ends with **SUCCESS**.

The tested toolchain is Espressif32 6.13.0 with the Arduino ESP32 2.0.17 framework
package. The platform version is pinned in each configuration. PlatformIO resolves
the library dependencies declared by each project.

## Upload to a device

1. Connect the intended ESP32 by USB. Disconnect the other ESP32 while uploading,
   or explicitly select the correct serial port.
2. Select the correct project in PlatformIO:

   | Hardware and purpose | Firmware project |
   | --- | --- |
   | Starter device | Starter |
   | Touchpad for swim timing | Finish Touchpad |
   | Touchpad for repeated-contact testing | TEST MODE |

3. Choose that project's **General → Upload** and wait for completion.
4. Use a 115,200 baud serial monitor for diagnostic output if needed.

Uploading TEST MODE replaces normal firmware on the touchpad. To restore swim
timing, upload **Finish Touchpad** again. Both projects remain on the computer;
there is no branch to switch and no need to modify the starter for a pad test.

## Command-line equivalents

Run these from the `Tactus` repository root in a terminal with PlatformIO available:

```sh
# Build each project
pio run --project-dir "Starter"
pio run --project-dir "Finish Touchpad"
pio run --project-dir "TEST MODE"

# Upload exactly one selected firmware
pio run --project-dir "Starter" --target upload
pio run --project-dir "Finish Touchpad" --target upload
pio run --project-dir "TEST MODE" --target upload
```

If several serial devices are connected, add `--upload-port` with the actual port,
for example `--upload-port COM5` on Windows. The port number is machine-specific;
use `pio device list` to inspect the available ports. See the official
[PlatformIO command reference](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html).

## First normal-mode check

Configure the peer MAC arrays for the two boards, verify their wiring, and supply
the DFPlayer's two audio tracks. Power both devices. The touchpad should wait for
a start signal, while the starter displays pool selection. Run a configured
effort and confirm that the final touchpad result reaches the starter.

## First TEST MODE check

After upload, the touchpad should show `TACTUS | TEST MODE`, `0/400`, and
`Press button to start`. Press the external GPIO 5 button, then press and fully
release the pad. Accepted touches increment the counter and produce green feedback.
The result locks at `400/400`; a fresh external button press starts another run.

Contacts shorter than 150 ms are intentionally rejected. See the
[test-mode operating guide](../TEST%20MODE/README.md) for the complete detection rules.
