# EmergenceKey — Build & flash

Verified against Espressif `arduino-esp32` and NimBLE-Arduino 2.5.1. You probably want to use the **main** sketch. Boot-strict is an optional keyboard-only fallback.

---

## Environment


| Setting                     | Value                                                                |
| --------------------------- | -------------------------------------------------------------------- |
| IDE                         | Arduino IDE 2                                                        |
| Board manager URL           | `https://espressif.github.io/arduino-esp32/package_esp32_index.json` |
| Core                        | **esp32** by Espressif                                               |
| Library                     | **NimBLE-Arduino 2.5.1** by h2zero (the only extra library)          |
| Board preset                | **Waveshare ESP32-S3-Zero**                                          |
| **Tools > USB Mode**        | **USB-OTG (TinyUSB)** — required (default is `Hardware CDC+JTAG`)    |
| **Tools > USB CDC On Boot** | **Disabled** — required for both builds                              |
| **PSRAM**                   | **Disabled**                                                         |
| Port (typical Linux)        | `/dev/ttyACM0`                                                       |
| Serial perms (Linux)        | user in `dialout` (`sudo usermod -aG dialout $USER`, then re-login)  |




### Upload quirks

- Select **both** the board preset **and** the port.
- The board **vanishes / re-enumerates mid-flash**. That is normal for a
native-USB S3; the IDE re-attaches.
- `Hard resetting via RTS pin...` in the log means success. Wait for
the size report / "done" before unplugging.
- After a good flash the WS2812 should **breathe** (idle, no BLE). It is on
GPIO 21, not the BOOT button. A dark LED usually means the Waveshare
ESP32-S3-Zero preset was not selected (`RGB_BUILTIN` missing).
- If `/dev/ttyACM0` is still present after flash, CDC-on-boot is still
enabled. Set **USB CDC On Boot = Disabled** and re-flash. Neither sketch
uses `Serial`.

---



## Flash the main build (keyboard + mouse)

1. Open `EmergenceKey/sketch_EmergenceKey/sketch_EmergenceKey.ino`.
2. Confirm the table above (USB-OTG, CDC **Disabled**, PSRAM **Disabled**).
3. Plug the board in, select the port, click Upload.
4. On `Hard resetting via RTS pin...`, check the new HID device:

```text
lsusb -v -d 303a:822b
```

Expect HID keyboard, mouse, **and** consumer-control (media) interfaces
and **no** CDC Comm/Data interface.

---



## Flash boot-strict (keyboard only, optional)

Use this only if the main build is invisible in a particular BIOS.
Combo keyboard+mouse receivers usually work in BIOS, so do not strip the
mouse first.

1. Open `EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict.ino`.
2. Same settings as the table (CDC **Disabled**).
3. Upload. Enumeration should be a single boot keyboard, no mouse, no CDC.
  The sketch `#warning`s at compile time if CDC-on-boot is still on.

---



## Verify on a Linux host

- `dmesg` shows a `303a:822b` HID boot keyboard after a USB-OTG flash.
- `lsusb -v -d 303a:822b`:
  - Main: keyboard, mouse, **and** consumer-control HID interfaces, no CDC.
  - Boot-strict: one keyboard interface, `InterfaceClass 3 / SubClass 1 / Protocol 1`.
- The EmergenceKey webpage (`2d2a0001-…` / `2d2a0002-…`) connects over Web
Bluetooth and types at the console. First time: native Pair dialog (iPhone
and similar), then Connect. On Linux, pair **EmergenceKey** in the OS
Bluetooth settings if no dialog appears, then Connect on the page.
Reconnects: Connect only, no Pair again. A stall or error only at that
first Pair moment is expected.
- LED: idle = yellow breathe · BLE connected = solid green · drop = red blink, then breathe.



### BIOS / firmware-setup screens

Pre-connect the dongle, power the machine fully off (not sleep), then power
on and enter setup. A "fast boot" BIOS may skip USB; use a thorough/full
POST if the option exists. If the main build types, you are done. If it
does not, flash boot-strict and retry. If both fail, that host's pre-boot
stack likely will not take an ESP32-S3 — stop stripping interfaces.

---



## Un-brick

If a bad flash wedges native USB so the IDE cannot see a serial port:

1. Hold **BOOT**, tap **RESET**, keep BOOT held — ROM download mode.
2. Flash from Arduino IDE as usual, or with `esptool` to the ROM port.

