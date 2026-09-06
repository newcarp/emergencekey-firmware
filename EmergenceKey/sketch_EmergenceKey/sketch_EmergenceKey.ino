/**
 * EmergenceKey — ESP32-S3 BLE -> USB HID keyboard & mouse bridge
 *
 * Fork of KoStard/ESPRemoteControl
 *   sketch: sketch_uid_keyboard_ble.ino  (github.com/KoStard/ESPRemoteControl)
 *
 * License: MIT (see LICENSE). Attribution:
 *   KoStard/ESPRemoteControl — https://github.com/KoStard/ESPRemoteControl
 *   keefeere/ESPRemoteControl — https://github.com/keefeere/ESPRemoteControl
 *
 * Kept from upstream:
 *   - v1 3-byte frames (0x01 key / 0x02 move / 0x03 click / 0x04 scroll)
 *     plus the v2 [0xAA,0x01] TLV parser
 *   - v1 modifiers: LCtrl 0x01, LShift 0x02, LAlt 0x04, LGUI 0x08
 * This fork:
 *   - UUIDs and advertised name "EmergenceKey" (set in init() AND setName())
 *   - WS2812 on GPIO 21 via RGB_BUILTIN (never GPIO 0 / BOOT)
 *   - USB before BLE; device class forced to 0/0/0 (required for some BIOS)
 *   - CDC off; no Serial; no HID-stall restart
 *   - SET_PROTOCOL hook is observe-only (TinyUSB answers the transfer)
 *   - BLE onWrite only enqueues; HID work runs in loop()
 *
 * Build (Arduino IDE 2):
 *   Board: Waveshare ESP32-S3-Zero
 *   Tools > USB Mode: USB-OTG (TinyUSB)     <-- required
 *   Tools > USB CDC On Boot: Disabled       <-- required
 *   PSRAM: Disabled
 *   Library: NimBLE-Arduino 2.5.1 (h2zero)
 */

#include <Arduino.h>

#include <NimBLEDevice.h>

#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"

// Catch the wrong board-menu selection at compile time.
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#warning "This sketch requires Tools > USB CDC On Boot = Disabled (CDC on breaks some BIOS hosts)."
#endif

// =====================
// OUR IDENTITY (must match the EmergenceKey webpage)
// =====================
static const char* kServiceUUID   = "2d2a0001-8a5a-4e76-a2e3-1e57d9a1b001";
static const char* kWriteCharUUID = "2d2a0002-8a5a-4e76-a2e3-1e57d9a1b001";

// 12 chars, fits the BLE advertising budget.
static const char* kDeviceName = "EmergenceKey";

// =====================
// LED — Waveshare ESP32-S3-Zero onboard WS2812 on GPIO 21.
// RGB_BUILTIN is a virtual pin (SOC_GPIO_PIN_COUNT + 21). digitalWrite /
// rgbLedWrite on that pin speak WS2812. GPIO 0 is the BOOT strap — do not use it.
// =====================
#ifndef EMERGENCE_LED_PIN
#if defined(RGB_BUILTIN)
#define EMERGENCE_LED_PIN RGB_BUILTIN
#elif defined(LED_BUILTIN)
#define EMERGENCE_LED_PIN LED_BUILTIN
#else
#define EMERGENCE_LED_PIN (SOC_GPIO_PIN_COUNT + 21)
#endif
#endif

#ifndef RGB_BRIGHTNESS
#define RGB_BRIGHTNESS 64
#endif

// Per-state hues. Brightness still comes from setLedColor()'s level + RGB_BRIGHTNESS.
static const uint8_t kLedIdleR = 255, kLedIdleG = 180, kLedIdleB = 0;    // yellow
static const uint8_t kLedConnR = 0,   kLedConnG = 255, kLedConnB = 0;    // green
static const uint8_t kLedErrR  = 255, kLedErrG  = 0,   kLedErrB  = 0;    // red

typedef enum {
  LED_STATE_IDLE_BREATH,      // BLE advertising, nothing connected
  LED_STATE_CONNECTED_SOLID,  // BLE host connected
  LED_STATE_ERROR_BLINK       // error (e.g. BLE drop)
} led_state_t;

static volatile led_state_t gLedState = LED_STATE_IDLE_BREATH;

// Animation phase clock for updateLed() (idle-breathe / error-blink timing).
// Declared here (not just near updateLed()) so setLedState() below can be
// used from anywhere in the file, including code defined earlier than the
// LED driver section.
static uint32_t gLedPhaseStartMs = 0;

// Always change gLedState through this helper. It resets the animation
// phase clock so a state entered mid-cycle (e.g. an error while breathing
// is 90% through its fade) always gets its own full animation window,
// instead of inheriting stale `elapsed` time from the previous state.
static void setLedState(led_state_t newState) {
  gLedState = newState;
  gLedPhaseStartMs = millis();
}

// =====================
// Protocol Commands (unchanged from upstream)
// =====================
static const uint8_t CMD_KEY          = 0x01;
static const uint8_t CMD_MOUSE_MOVE   = 0x02;
static const uint8_t CMD_MOUSE_CLICK  = 0x03;
static const uint8_t CMD_MOUSE_SCROLL = 0x04;

// Queued v2 extras (not on the wire — local dispatch tags)
static const uint8_t Q_SET_MODIFIERS  = 0x81;
static const uint8_t Q_KEY_DOWN       = 0x82;
static const uint8_t Q_KEY_UP         = 0x83;
static const uint8_t Q_MOUSE_PRESS    = 0x84;
static const uint8_t Q_MOUSE_RELEASE  = 0x85;

// v2 frame magic (kept verbatim from upstream for compatibility)
static const uint8_t V2_MAGIC   = 0xAA;
static const uint8_t V2_VERSION = 0x01;

// =====================
// USB HID instances
// Keyboard MUST be constructed first: the first USBHID() sets TinyUSB's
// boot-keyboard protocol flag. Later USBHID handles share that singleton.
// =====================
USBHIDKeyboard Keyboard;
USBHIDMouse     Mouse;

// Observes SET_PROTOCOL / SET_IDLE. Not begun() — shares Keyboard's HID
// singleton and adds no extra USB interface.
USBHID HidEvents(HID_ITF_PROTOCOL_KEYBOARD);

static void hidEventCallback(void*, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
  if (eventBase != ARDUINO_USB_HID_EVENTS) return;
  if (eventId == ARDUINO_USB_HID_SET_PROTOCOL_EVENT) {
    arduino_usb_hid_event_data_t* d = (arduino_usb_hid_event_data_t*)eventData;
    (void)d; // observe only — no remap, matches Fedora/report-protocol behavior
  }
}

// =====================
// Keyboard / Mouse state
// =====================
static uint8_t gModifiersMask = 0x00;
static bool    gKeysDown[256] = { false };

static void setModifiers(uint8_t newMask) {
  uint8_t diff = gModifiersMask ^ newMask;
  if (!diff) return;

  if (diff & 0x01) { if (newMask & 0x01) Keyboard.pressRaw(0xE0); else Keyboard.releaseRaw(0xE0); } // LCtrl
  if (diff & 0x02) { if (newMask & 0x02) Keyboard.pressRaw(0xE1); else Keyboard.releaseRaw(0xE1); } // LShift
  if (diff & 0x04) { if (newMask & 0x04) Keyboard.pressRaw(0xE2); else Keyboard.releaseRaw(0xE2); } // LAlt
  if (diff & 0x08) { if (newMask & 0x08) Keyboard.pressRaw(0xE3); else Keyboard.releaseRaw(0xE3); } // LGUI
  if (diff & 0x10) { if (newMask & 0x10) Keyboard.pressRaw(0xE4); else Keyboard.releaseRaw(0xE4); } // RCtrl
  if (diff & 0x20) { if (newMask & 0x20) Keyboard.pressRaw(0xE5); else Keyboard.releaseRaw(0xE5); } // RShift
  if (diff & 0x40) { if (newMask & 0x40) Keyboard.pressRaw(0xE6); else Keyboard.releaseRaw(0xE6); } // RAlt
  if (diff & 0x80) { if (newMask & 0x80) Keyboard.pressRaw(0xE7); else Keyboard.releaseRaw(0xE7); } // RGUI

  gModifiersMask = newMask;
}

static void keyDown(uint8_t keycode) {
  if (keycode == 0x00) return;
  if (gKeysDown[keycode]) return;
  Keyboard.pressRaw(keycode);
  gKeysDown[keycode] = true;
}

static void keyUp(uint8_t keycode) {
  if (keycode == 0x00) return;
  if (!gKeysDown[keycode]) return;
  Keyboard.releaseRaw(keycode);
  gKeysDown[keycode] = false;
}

static void keyTap(uint8_t modifiersMask, uint8_t keycode) {
  if (keycode == 0x00) return;

  bool wasDown = gKeysDown[keycode];
  uint8_t savedMods = gModifiersMask;

  setModifiers(modifiersMask);

  if (!wasDown) {
    keyDown(keycode);
    delay(5);
    keyUp(keycode);
    delay(1);
  }

  setModifiers(savedMods);
}

static void sendMouseMove(int8_t dx, int8_t dy) { Mouse.move(dx, dy); }
static void sendMouseClick(uint8_t button)       { Mouse.click(button); }
static void sendMouseScroll(int8_t dx, int8_t dy){ Mouse.move(0, 0, dy, dx); }

static void dispatchCmd(uint8_t type, uint8_t a, uint8_t b) {
  switch (type) {
    case CMD_KEY:          keyTap(a, b);                         break;
    case CMD_MOUSE_MOVE:   sendMouseMove((int8_t)a, (int8_t)b);  break;
    case CMD_MOUSE_CLICK:  sendMouseClick(a);                    break;
    case CMD_MOUSE_SCROLL: sendMouseScroll((int8_t)a, (int8_t)b); break;
    case Q_SET_MODIFIERS:  setModifiers(a);                      break;
    case Q_KEY_DOWN:       keyDown(a);                           break;
    case Q_KEY_UP:         keyUp(a);                             break;
    case Q_MOUSE_PRESS:    Mouse.press(a);                       break;
    case Q_MOUSE_RELEASE:  Mouse.release(a);                     break;
    default: break;
  }
}

// =====================
// BLE -> loop command queue (do not delay() inside NimBLE onWrite)
// =====================
static constexpr uint8_t kCmdQCap = 48;

struct BleCmd {
  uint8_t type;
  uint8_t a;
  uint8_t b;
};

static BleCmd gCmdQ[kCmdQCap];
static volatile uint8_t gCmdQHead = 0;
static volatile uint8_t gCmdQTail = 0;

static bool enqueueCmd(uint8_t type, uint8_t a, uint8_t b) {
  uint8_t head = gCmdQHead;
  uint8_t next = (uint8_t)((head + 1) % kCmdQCap);
  if (next == gCmdQTail) return false;
  gCmdQ[head].type = type;
  gCmdQ[head].a = a;
  gCmdQ[head].b = b;
  gCmdQHead = next;
  return true;
}

// Arduino IDE auto-inserts prototypes after #includes, before BleCmd exists.
// Keep this signature on uint8_t* so the generated prototype compiles.
static bool dequeueCmd(uint8_t* type, uint8_t* a, uint8_t* b) {
  uint8_t tail = gCmdQTail;
  if (tail == gCmdQHead) return false;
  *type = gCmdQ[tail].type;
  *a = gCmdQ[tail].a;
  *b = gCmdQ[tail].b;
  gCmdQTail = (uint8_t)((tail + 1) % kCmdQCap);
  return true;
}

// =====================
// BLE GATT server
// =====================
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pWriteChar = nullptr;

class WriteCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string v = pCharacteristic->getValue();
    if (v.size() < 3) return;

    // ---- v2: [0xAA, 0x01] + TLV frames (kept from upstream) ----
    if ((uint8_t)v[0] == V2_MAGIC && (uint8_t)v[1] == V2_VERSION) {
      size_t idx = 2;
      while (idx + 1 < v.size()) {
        uint8_t cmd = (uint8_t)v[idx + 0];
        uint8_t len = (uint8_t)v[idx + 1];
        idx += 2;
        if (idx + len > v.size()) break;
        const uint8_t* payload = (const uint8_t*)&v[idx];
        switch (cmd) {
          case 0x01: if (len == 1) enqueueCmd(Q_SET_MODIFIERS, payload[0], 0); break;
          case 0x02: if (len == 1) enqueueCmd(Q_KEY_DOWN, payload[0], 0); break;
          case 0x03: if (len == 1) enqueueCmd(Q_KEY_UP, payload[0], 0); break;
          case 0x04: if (len == 2) enqueueCmd(CMD_KEY, payload[0], payload[1]); break;
          case 0x10: if (len == 2) enqueueCmd(CMD_MOUSE_MOVE, payload[0], payload[1]); break;
          case 0x11: if (len == 2) enqueueCmd(CMD_MOUSE_SCROLL, payload[0], payload[1]); break;
          case 0x12: if (len == 1) enqueueCmd(CMD_MOUSE_CLICK, payload[0], 0); break;
          case 0x13: if (len == 1) enqueueCmd(Q_MOUSE_PRESS, payload[0], 0); break;
          case 0x14: if (len == 1) enqueueCmd(Q_MOUSE_RELEASE, payload[0], 0); break;
          default: break;
        }
        idx += len;
      }
      return;
    }

    // ---- v1: 3-byte frames (THE contract — do not change) ----
    for (size_t i = 0; i + 2 < v.size(); i += 3) {
      enqueueCmd((uint8_t)v[i + 0], (uint8_t)v[i + 1], (uint8_t)v[i + 2]);
    }
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    setLedState(LED_STATE_CONNECTED_SOLID);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    setLedState(LED_STATE_ERROR_BLINK);
    NimBLEDevice::startAdvertising();
  }
};

// =====================
// LED driver (non-blocking): idle=yellow breathe, connected=green, error=red blink.
// gLedPhaseStartMs / setLedState() are declared earlier (near gLedState).
// =====================
static void setLedColor(uint8_t level, uint8_t r, uint8_t g, uint8_t b) {
#if defined(RGB_BUILTIN)
  uint8_t s = (uint8_t)(((uint16_t)level * (uint16_t)RGB_BRIGHTNESS) / 255);
  rgbLedWrite(RGB_BUILTIN,
              (uint8_t)(((uint16_t)s * r) / 255),
              (uint8_t)(((uint16_t)s * g) / 255),
              (uint8_t)(((uint16_t)s * b) / 255));
#else
  digitalWrite(EMERGENCE_LED_PIN, level >= 96 ? HIGH : LOW);
#endif
}

static void updateLed() {
  uint32_t now = millis();
  uint32_t elapsed = now - gLedPhaseStartMs;
  uint32_t period;

  switch (gLedState) {
    case LED_STATE_CONNECTED_SOLID:
      setLedColor(255, kLedConnR, kLedConnG, kLedConnB);
      break;

    case LED_STATE_ERROR_BLINK:
      period = 240;
      setLedColor(((elapsed / period) & 1) ? 255 : 0, kLedErrR, kLedErrG, kLedErrB);
      if (elapsed >= 2000) {
        setLedState(LED_STATE_IDLE_BREATH);
      }
      break;

    case LED_STATE_IDLE_BREATH:
    default: {
      period = 2800;
      uint32_t t    = elapsed % period;
      uint32_t half = period / 2;
      uint8_t level = (t < half)
                    ? (uint8_t)((255 * t) / half)
                    : (uint8_t)((255 * (period - t)) / half);
      setLedColor(level, kLedIdleR, kLedIdleG, kLedIdleB);
      break;
    }
  }

  if (elapsed >= 60000) gLedPhaseStartMs = now;
}

void setup() {
#if defined(RGB_BUILTIN)
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
#else
  pinMode(EMERGENCE_LED_PIN, OUTPUT);
  digitalWrite(EMERGENCE_LED_PIN, LOW);
#endif

  // USB first so a BIOS probe can see HID before BLE finishes starting.
  // Device class 0/0/0 is load-bearing for some firmware USB stacks — the
  // core default 0xEF/0x02/0x01 is fine on Linux and invisible in those
  // hosts. Do not remove without hardware evidence.
  HidEvents.onEvent(hidEventCallback);

  USB.productName("EmergenceKey");
  USB.manufacturerName("Emergence");
  // Interface-defined class. Each HID interface already carries its own
  // boot-protocol class byte. CDC-on-boot must stay Disabled.
  USB.usbClass(0);
  USB.usbSubClass(0);
  USB.usbProtocol(0);

  Keyboard.begin();
  Mouse.begin();
  USB.begin();

  // ---------- THEN BLE ----------
  NimBLEDevice::init(kDeviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = pServer->createService(kServiceUUID);

  pWriteChar = svc->createCharacteristic(
    kWriteCharUUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pWriteChar->setCallbacks(new WriteCallbacks());
  // NimBLEService::start() is a no-op in this NimBLE-Arduino version
  // (services start automatically when the server starts) — call omitted
  // to avoid the deprecation warning.

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kServiceUUID);
  adv->setName(kDeviceName);
  adv->start();

  setLedState(LED_STATE_IDLE_BREATH);
}

void loop() {
  updateLed();

  uint8_t type, a, b;
  uint8_t drained = 0;
  while (drained < 8 && dequeueCmd(&type, &a, &b)) {
    dispatchCmd(type, a, b);
    drained++;
  }

  // No ESP.restart() on HID idle or USB unmount — that reboot-loops picky BIOS hosts.
  if (drained == 0) delay(20);
}
