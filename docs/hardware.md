# Hardware interface map

This map is derived from the checked-in firmware. It identifies software pin assignments, not a complete electrical schematic.

## Shared display interface

Both physical devices use an ESP32 (`esp32dev`) and an ILI9341 TFT. The same assignments apply when the touchpad runs TEST MODE.

| Display signal | ESP32 GPIO |
| --- | --- |
| CS | 13 |
| Reset | 26 |
| Data/command | 25 |
| MOSI | 23 |
| Clock | 18 |
| MISO | Not used by this firmware |

The display uses hardware SPI at 20 MHz and rotation 3, giving a 320 × 240 landscape coordinate space. Chip select is library-controlled; GPIO 5 remains available for the physical button.

## Starter

| Function | ESP32 GPIO / interface |
| --- | --- |
| Cancel button | GPIO 15, active LOW, `INPUT_PULLUP` |
| Navigation button | GPIO 5, active LOW, `INPUT_PULLUP` |
| Confirm button | GPIO 22, active LOW, `INPUT_PULLUP` |
| Red LED | GPIO 14 |
| White LED | GPIO 27 |
| DFPlayer receive input at ESP32 | GPIO 16, UART2 RX |
| DFPlayer transmit output from ESP32 | GPIO 17, UART2 TX |
| DFPlayer serial configuration | 9,600 baud, 8 data bits, no parity, 1 stop bit |
| Diagnostic serial monitor | 115,200 baud |

Audio track 1 is used for the take-your-marks cue; track 2 is used for the start beep. Supply compatible audio media on the DFPlayer's storage and confirm the module resolves those track numbers correctly. Audio files are not included.

## Touchpad and TEST MODE

| Function | ESP32 GPIO / interface |
| --- | --- |
| Physical pad contact input | GPIO 15, active LOW, `INPUT_PULLUP` |
| External reset/start button | GPIO 5, active LOW, `INPUT_PULLUP` |
| Red LED | GPIO 22 |
| White LED | GPIO 14 |
| Green LED | GPIO 27 |
| Diagnostic serial monitor | 115,200 baud |

The sensor interface is a digital contact signal. The firmware does not measure analog force, pressure, or capacitance. The external GPIO 5 button is distinct from the ESP32 board's EN/reset and BOOT buttons.

## Peer configuration

The prototype's current peer MAC addresses are embedded in the source:

- `touchpadMAC` in `Starter/src/starter_main.cpp` identifies the touchpad.
- `starterMAC` in `Finish Touchpad/src/touchpad_main.cpp` identifies the starter.

If either ESP32 is replaced, update the corresponding peer array before flashing normal mode. Use the board's Wi-Fi station MAC address. TEST MODE does not use peer addresses.

The source does not define the power supply, pad construction, wiring protection, or enclosure design. Reproducing the hardware requires those physical design details in addition to this interface map.
