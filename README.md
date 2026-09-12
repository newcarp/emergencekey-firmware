# EmergenceKey: Use a ESP32-S3 BLE as USB HID Bridge

A **phone or laptop becomes a wireless keyboard, mouse, and media remote**. 

This is half of the project, this repo is the USB dongle. The phone/laptop UI is [newcarp/emergencekey](https://github.com/newcarp/emergencekey).  
Flash this, open that page, and connect.

Any BLE client that speaks the wire protocol can drive it:  Android, iPhone, Windows, Linux, or Mac (Chrome / Edge Web Bluetooth; iPhone needs Safari + the [beacio](https://beacio.com/) extension). The controller talks Bluetooth; this USB dongle (Waveshare ESP32-S3-Zero) presents as a real hardware HID device. No drivers or software needed on the target.

Note - due to limitations of the ESP32-S3 in some bios mouse input doesn't work, only keyboard.

Fork of [KoStard/ESPRemoteControl](https://github.com/KoStard/ESPRemoteControl)
(MIT). See [Attribution](#attribution).

**BLE uses LESC Just Works** (encrypted link, no PIN). First time: native
OS/browser Pair dialog, one tap — then the webpage Connect. Later reconnects
are silent (Connect on the page only; no Pair again).

On Linux, Chrome often never shows that dialog. Pair **EmergenceKey** in the
laptop Bluetooth settings first, then open the page and Connect. Same bond;
same silent reconnects after that.

Treat this as an emergency / same-room tool, not a general-purpose wireless
keyboard.

[Flashing instructions](BUILD_NOTES.md).  

Use it from: [newcarp/emergencekey](https://github.com/newcarp/emergencekey).

---

## Builds


| Sketch                                                                                                               | Role                                                                                                                                            |
| -------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| `[EmergenceKey/sketch_EmergenceKey/](EmergenceKey/sketch_EmergenceKey/)`                                             | **For most use cases you want this.** Keyboard + mouse + media keys on a full OS. In some cases keyboard only in BIOS.                          |
| `[EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict/](EmergenceKey_BootStrict/sketch_EmergenceKey_BootStrict/)` | Extra-minimal keyboard-only version, it may work in some bios that the standard version does not. Same wire protocol; mouse and media frames are ignored. |


Both require **USB Mode = USB-OTG (TinyUSB)** and **USB CDC On Boot = Disabled**.

---



## Wire protocol

Messages are **3 bytes**, first byte = type:


| Type   | Bytes                                     | Meaning                          |
| ------ | ----------------------------------------- | -------------------------------- |
| `0x01` | `[modifiers, keycode]`                    | Type a key                       |
| `0x02` | `[dx, dy]` signed two's-complement        | Move mouse                       |
| `0x03` | `[button, 0x00]` (0x01=L, 0x02=R, 0x04=M) | Mouse click                      |
| `0x04` | `[dx, dy]`                                | Scroll                           |
| `0x05` | `[action, 0x00]`                          | Media tap (main build, OS only)  |


**Modifier bitmask:** LCtrl `0x01`, LShift `0x02`, LAlt `0x04`, LGUI `0x08`.
Uppercase = `0x02` + letter.

**HID keycodes:** `a`–`z` = `0x04`–`0x1D`, `1`–`0` = `0x1E`–`0x27`,
Space `0x2C`, Enter `0x28`, Backspace `0x2A`, Esc `0x29`, Tab `0x2B`,
F1–F12 `0x3A`–`0x45`, arrows `0x4F`–`0x52`.

**Media action byte** (one write = one tap; the dongle presses and releases):

| Action | Key          | Example write        |
| ------ | ------------ | -------------------- |
| `0x01` | Play / Pause | `05 01 00`           |
| `0x02` | Next track   | `05 02 00`           |
| `0x03` | Previous     | `05 03 00`           |
| `0x04` | Stop         | `05 04 00`           |
| `0x05` | Volume up    | `05 05 00`           |
| `0x06` | Volume down  | `05 06 00`           |
| `0x07` | Mute         | `05 07 00`           |

Media keys hit the **USB host** (the machine the dongle is plugged into), not the phone. Old firmware and boot-strict drop `0x05`. The live page can keep sending it; nothing breaks if the stick does not support it yet.

Hold-to-repeat (volume) = send the same 3-byte frame again from the page. Do not invent a press/release pair; there is not one on v1.

The main build also accepts the upstream v2 `[0xAA, 0x01]` TLV frames. Media there is TLV cmd `0x20`, len `1`, payload = the same action byte.

---



## Identity (must match the [EmergenceKey webpage](https://github.com/newcarp/emergencekey))


| Field                     | Value                                  |
| ------------------------- | -------------------------------------- |
| Advertised name           | `EmergenceKey` (12 chars)              |
| Service UUID              | `2d2a0001-8a5a-4e76-a2e3-1e57d9a1b001` |
| Write characteristic UUID | `2d2a0002-8a5a-4e76-a2e3-1e57d9a1b001` |


These are the same strings KoStard published. The name is set in **two** places in each sketch — `NimBLEDevice::init()` and `adv->setName()`. Both must say `EmergenceKey`. If you change the UUIDs, change the webpage at the same time.

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
Linux OS (keyboard + mouse) and a Dell BIOS (keyboard). It now also
exposes a Consumer Control interface for media keys. That third HID
interface is for a full OS only — do not expect media keys in BIOS.
Boot-strict is insurance only; flash it if some other host will not
enumerate the composite device.

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