# EmergenceKey: Use a ESP32-S3 BLE as USB HID Bridge

A **phone or laptop becomes a wireless keyboard and mouse**. 

This is half of the project, this repo is the USB dongle. The phone/laptop UI is [newcarp/emergencekey](https://github.com/newcarp/emergencekey).  
Flash this, open that page, and connect.

Any BLE client that speaks the wire protocol can drive it:  Android, iPhone, Windows, Linux, or Mac (Chrome / Edge Web Bluetooth; iPhone needs Safari + the [beacio](https://beacio.com/) extension). The controller talks Bluetooth; this USB dongle (Waveshare ESP32-S3-Zero) presents as a real hardware HID device. No drivers or software needed on the target.

Note - due to limitations of the ESP32-S3 in some bios mouse input doesn't work, only keyboard.

Fork of [KoStard/ESPRemoteControl](https://github.com/KoStard/ESPRemoteControl)
(MIT). See [Attribution](#attribution).

**BLE is open (no pairing).** Treat this as an emergency / same-room tool, not
a general-purpose wireless keyboard.

[Flashing instructions](BUILD_NOTES.md).  

Use it from: [newcarp/emergencekey](https://github.com/newcarp/emergencekey).

---



## Identity (must match the [EmergenceKey webpage](https://github.com/newcarp/emergencekey))


| Field                     | Value                                  |
| ------------------------- | -------------------------------------- |
| Advertised name           | `EmergenceKey` (12 chars)              |
| Service UUID              | `2d2a0001-8a5a-4e76-a2e3-1e57d9a1b001` |
| Write characteristic UUID | `2d2a0002-8a5a-4e76-a2e3-1e57d9a1b001` |


These are the same strings KoStard published. The name is set in **two**
places in each sketch — `NimBLEDevice::init()` and `adv->setName()`. Both
must say `EmergenceKey`. If you change the UUIDs, change the webpage at the
same time.

---



## Builds


| Sketch                                                                                                               | Role                                                                                                                                            |
| -------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `[EmergenceKey/sketch_EmergenceKey/](EmergenceKey/sketch_EmergenceKey/)`                                             | **For most use cases you want this.** Keyboard + mouse on a full OS. In some cases keyboard only in BIOS.                                       |
| `[EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict/](EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict/)` | Extra-minimal keyboard-only version, it may work in some bios that the standard version does not. Same wire protocol; mouse frames are ignored. |


Both require **USB Mode = USB-OTG (TinyUSB)** and **USB CDC On Boot = Disabled**.

---



## Wire protocol

Messages are **3 bytes**, first byte = type:


| Type   | Bytes                                     | Meaning     |
| ------ | ----------------------------------------- | ----------- |
| `0x01` | `[modifiers, keycode]`                    | Type a key  |
| `0x02` | `[dx, dy]` signed two's-complement        | Move mouse  |
| `0x03` | `[button, 0x00]` (0x01=L, 0x02=R, 0x04=M) | Mouse click |
| `0x04` | `[dx, dy]`                                | Scroll      |


**Modifier bitmask:** LCtrl `0x01`, LShift `0x02`, LAlt `0x04`, LGUI `0x08`.
Uppercase = `0x02` + letter.

**HID keycodes:** `a`–`z` = `0x04`–`0x1D`, `1`–`0` = `0x1E`–`0x27`,
Space `0x2C`, Enter `0x28`, Backspace `0x2A`, Esc `0x29`, Tab `0x2B`,
F1–F12 `0x3A`–`0x45`, arrows `0x4F`–`0x52`.

The main build also accepts the upstream v2 `[0xAA, 0x01]` TLV frames.

---



## BIOS / pre-boot notes

Picky firmware USB stacks (some laptop BIOS setup screens) reject this chip
unless:

1. **USB CDC On Boot is Disabled** — CDC on is fine in a full OS and
  **fails in BIOS** (retested: Dell setup dies with CDC on, types with
   CDC off). A CDC ACM interface, or an HID-stall watchdog that
   `ESP.restart()`s, will reboot-loop or disappear.
2. Device class is forced to `0/0/0` in `setup()` (`USB.usbClass(0)` and
  friends). Do not remove that; the esp32 core default (`0xEF/0x02/0x01`)
   is enough for Linux and not enough for some BIOS hosts.
3. USB is started **before** BLE.

The main build already follows all three and has been confirmed in both a
Linux OS (keyboard + mouse) and a Dell BIOS (keyboard). Boot-strict is
insurance only; flash it if some other host will not enumerate the
two-interface device.

---



## LED

On the Waveshare ESP32-S3-Zero WS2812 (GPIO 21 / `RGB_BUILTIN`):


| Pattern                  | Color  | Meaning            |
| ------------------------ | ------ | ------------------ |
| Breathing                | Yellow | Idle, advertising  |
| Solid                    | Green  | BLE connected      |
| Fast blink, then breathe | Red    | Disconnect / error |


---



## Attribution

- **KoStard / ESPRemoteControl** — original BLE → USB HID bridge:  
[https://github.com/KoStard/ESPRemoteControl](https://github.com/KoStard/ESPRemoteControl)

MIT `LICENSE` in this repo; KoStard is also credited in every sketch header.
Upstream READMEs say MIT but, as fetched, those repos did not include a
LICENSE file. Keep this attribution if you distribute the fork.

Hardware: Waveshare ESP32-S3-Zero (ESP32-S3FH4R2, 4 MB flash + 2 MB PSRAM).
BLE: [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) by h2zero.
USB HID: Espressif `arduino-esp32` (TinyUSB).