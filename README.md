# Mitsubishi Split Controller

An ESP32-based IR bridge that turns a Mitsubishi mini-split into a Wi-Fi connected thermostat. The board hosts a modern web UI with manual controls, 4 on/off schedules, timezone-aware NTP syncing, and an onboarding flow that lets you join it to your home network without recompiling firmware.

## Hardware Overview
- **MCU**: ESP-WROOM-32 / ESP32-DevKit modules (tested with the ESP32 ESP-32S Development Board).
- **IR output**: Uses the *Gikfun Infrared Diode LED (IR emission) module* from Amazon. Drive the LED from GPIO 4 (`IR_SEND_PIN`) through a current-limiting resistor or a transistor stage if you need extra range.
- **Status LED / button**: GPIO 2 drives the onboard LED for power state; GPIO 0 is used as a momentary button to toggle power or factory-reset when held >10 s.
- **Storage**: LittleFS stores Wi-Fi credentials, timezone, IP configuration, and schedule data.

## Wiring Reference
Minimal connections when using the Gikfun IR LED pair:

| ESP32 Pin | Connection                               | Notes |
|-----------|------------------------------------------|-------|
| 3V3       | IR LED VCC                                | Use the LED module’s VCC pad. |
| GND       | IR LED GND, button ground                 | Common ground for all components. |
| GPIO 4    | IR LED signal (via resistor or transistor)| `IR_SEND_PIN` defined in code. |
| GPIO 2    | Status LED (onboard)                      | Already available on most dev boards. |
| GPIO 0    | Momentary push button to GND              | Short press toggles power, >10 s wipes settings. |

> For longer IR range, place an NPN transistor between GPIO 4 and the IR LED, add a 100–220 Ω resistor at the base, and power the LED from 5 V (still tie grounds together).

### Optional 3D-Printed Case
I designed a quick ESP32 cover with an IR window that fits the ESP-32S dev board plus the Gikfun LED. Download or remix it on Tinkercad: [ESP32 Cover w/ Hole](https://www.tinkercad.com/things/0Tq4Ht3VCav-esp32-cover-w-hole).

## Features
- Wi-Fi AP onboarding portal; enter SSID/password once, optionally set a static IP and POSIX timezone string.
- mDNS service (`hvac.local`) when connected to your router.
- Simple adaptive UI for manual heat/cool control and 4 independent schedules.
- Compressor protection logic to avoid short-cycling.
- Long-press reset to clear all saved data.

## First-Time Setup
1. **Power the ESP32** with the firmware loaded (see Build section below). When no Wi-Fi credentials are stored it automatically enters setup mode.
2. **Join the temporary AP** named `AC-Controller-Setup` from a phone or laptop.
3. **Browse to** `http://192.168.4.1/`. The captive portal shows the *Controller Setup* form.
4. **Enter Wi-Fi info**:
   - `WiFi SSID` and `WiFi Password` for your home network.
   - Optional `POSIX Timezone` (defaults to `EST5EDT,M3.2.0,M11.1.0` — replace with your region).
   - Tick **Use Static IP** if you prefer to pin the ESP32 to a specific IP; fill in IP/Gateway/Subnet.
5. Submit the form. The device stores the data, reboots, and joins your Wi-Fi. After ~10 s it should be reachable at the DHCP address (or the static IP) as well as `http://hvac.local/` on networks that resolve mDNS.
6. Press the onboard button briefly to toggle power, or visit the web UI to control heat/cool modes and timers.

To re-enter setup mode later, hold the button on GPIO 0 for >10 seconds to format LittleFS and reboot.

## Building & Uploading
1. **Arduino IDE**: Use v2.x or later for ESP32 support.
2. **Board package**: Install the latest *esp32* core via the Boards Manager, then select `ESP32 Dev Module` (or the board that matches your hardware).
3. **Libraries** (latest releases via Library Manager or GitHub):
   - `ESP Async WebServer` (requires `AsyncTCP` on ESP32).
   - `IRremoteESP8266`.
   - `LittleFS` is bundled with the ESP32 core; no extra install needed beyond enabling `LittleFS` as the filesystem.
4. **Sketch files**: Place `mitsubishi-split-controller.ino` and `wifi_setup.h` in the same sketch folder (already set up in this repo).
5. **Partition / FS**:
   - Select a partition scheme that includes LittleFS (e.g., `Default 4MB with spiffs` works; LittleFS piggybacks).
   - Upload the sketch normally; it formats LittleFS on first boot.
6. **Serial monitor** (115200 baud) shows onboarding progress, last action logs, and protection messages. Useful for debugging Wi-Fi joins.

## Usage Tips
- The web UI exposes four timers (`T1`–`T4`). Each can independently enable on/off events in heat or cool mode at a given Fahrenheit temperature.
- The ESP32 syncs against `pool.ntp.org` and `time.google.com` using the timezone you enter, so scheduling follows local time, including DST shifts.
- Compressor protection enforces a 5-minute delay before re-powering to avoid rapid cycling; if you toggle power via the button or UI during this window the UI log will show “Protect Delay Active.”
- `/reboot` endpoint restarts the module after returning `Rebooting...`, handy when changing router settings.

## Troubleshooting
- **Can’t find the setup AP**: Hold the button for >10 s to wipe settings; LED will blink as the device restarts into AP mode.
- **mDNS (`hvac.local`) not resolving**: Some routers block multicast; use the shown IP from the serial monitor/DHCP table instead.
- **IR not controlling the indoor unit**: Verify LED orientation, transistor wiring, and that you are aligned within line-of-sight. You can also hook an IR receiver to confirm pulses are being sent.

Contributions and refinements welcome—open an issue or PR describing hardware variations, UI changes, or protocol improvements.
