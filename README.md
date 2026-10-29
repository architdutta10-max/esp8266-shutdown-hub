# Mission Impossible Shutdown Hub

> A ₹400 ESP8266 device that turns any phone into a remote PC shutdown controller — with a timed self-destruct, Mission Impossible theme, voice countdown, and activity logging.

![ESP8266](https://img.shields.io/badge/ESP8266-NodeMCU-blue)
![Python](https://img.shields.io/badge/Python-3.10+-green)
![License](https://img.shields.io/badge/License-MIT-yellow)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20Mac-lightgrey)
![Cost](https://img.shields.io/badge/Hardware%20Cost-%E2%82%B9400-orange)

---

## Overview

Mission Impossible Shutdown Hub is a self-contained IoT project that lets you schedule a graceful shutdown of any PC — from a phone, over Wi-Fi, with a Mission Impossible–style countdown and voiceover.

The ESP8266 creates its own Wi-Fi network (`ShutdownHub`), serves a PIN-protected web dashboard, and sends shutdown commands to the PC over the USB serial connection that's already plugged in. No cloud, no router, no subscription, no internet required.

Built as a portfolio project for Google DevFest.

---

## Features

- **Phone dashboard** running on the ESP8266 itself
- **Timer with presets** — 1m, 5m, 10m, 30m, 1h, or any custom duration
- **Mission Impossible theme** plays automatically at T-15 seconds
- **Voice countdown** — "This PC will self destruct in 10 seconds" → 10, 9, 8... 1 → "Goodbye"
- **ABORT button** — cancels audio and shutdown at any moment
- **PIN-protected dashboard** — prevents unauthorized shutdowns
- **Activity logger** — records active window, idle time, CPU %, and RAM % every 5 seconds
- **Offline-first** — works with zero internet dependency
- **Cross-platform PC listener** — Windows, Linux, macOS

---

## How It Works

```
┌──────────┐   Wi-Fi    ┌───────────────┐  USB Serial  ┌──────────────┐
│  Phone   │ ────────▶  │   ESP8266     │ ──────────▶  │  Target PC   │
│ Browser  │  HTTP :80  │  192.168.4.1  │  115200 baud │  Listener    │
└──────────┘            └───────────────┘              └──────────────┘
```

1. Phone connects to ESP's own Wi-Fi network `ShutdownHub`
2. Phone opens `http://192.168.4.1` — the PIN-protected dashboard
3. User sets a timer and presses START
4. ESP8266 counts down — each second is pushed to the phone via WebSocket
5. At **T-15 seconds**, ESP sends `PLAY_THEME` over USB → PC plays Mission Impossible theme
6. At **T-0 seconds**, ESP sends `SELF_DESTRUCT` over USB → PC plays voice countdown
7. PC executes `shutdown /s /f /t 0` (Windows) or `sudo shutdown -h now` (Linux/Mac)

The whole thing runs locally. Nothing leaves the room.

---

## Hardware Requirements

| Component | Approx. Cost (INR) |
|---|---|
| ESP8266 NodeMCU board | ₹250 – 400 |
| Micro-USB data cable | ₹50 – 100 |
| USB 5V 1A power adapter (or PC USB port) | ₹100 – 150 |
| **Total** | **₹400 – 650** |

---

## Software Requirements

**For flashing the ESP8266:**
- [Arduino IDE 2.x](https://www.arduino.cc/en/software)
- ESP8266 board package (install via Board Manager)
- WebSockets library by Markus Sattler (install via Manage Libraries)

**For running the PC listener:**
- [Python 3.10+](https://www.python.org/downloads/) — tick "Add Python to PATH" during install
- All Python dependencies listed in `requirements.txt`

Install with:

```bash
pip install -r requirements.txt
```

---

## Setup

### Step 1 — Flash the ESP8266

1. Open `sketch_oct24a.ino` in Arduino IDE
2. **Tools → Board → NodeMCU 1.0 (ESP-12E Module)**
3. **Tools → Port → your COM port** (check Device Manager)
4. Click **Upload**

Open **Tools → Serial Monitor** at **115200 baud**. You should see:

```
=== CONTROL HUB BOOT ===
AP started: YES
AP SSID: ShutdownHub
AP IP: 192.168.4.1
Ready.
```

### Step 2 — Start the PC listener

In a Command Prompt on the PC that will be shut down:

```bash
cd /d "C:\path\to\your\project"
pip install -r requirements.txt
python -u pc_listener.py
```

You should see:

```
=== CONTROL HUB PC LISTENER ===
[HTTP] listening on :8000
[SERIAL] opening COM8 ...
[SERIAL] connected. Listening...
[ESP] AP started: YES
[ESP] Ready.
```

### Step 3 — (Optional) Start the activity logger

In a second Command Prompt:

```bash
python -u pc_activity.py
```

This writes `pc_activity.log` every 5 seconds. It's independent of the shutdown flow.

### Step 4 — Use the dashboard

On your phone:

1. Connect to Wi-Fi **`ShutdownHub`** (password: **`shutdown123`**)
2. Open **`http://192.168.4.1`**
3. Enter PIN **`1234`** → tap **Unlock**
4. Set a timer (e.g., 10 seconds) → tap **START**

> Windows will warn "No internet connection" — tap **Yes** to stay connected. That's expected. The ESP's Wi-Fi has no internet by design.

---

## Configuration

### `sketch_oct24a.ino`

```cpp
const char* ap_ssid      = "ShutdownHub";  // ESP's Wi-Fi name
const char* ap_password  = "shutdown123";  // ESP's Wi-Fi password
const char* DASH_PIN     = "1234";         // Dashboard PIN — change this!
```

### `pc_listener.py`

```python
SERIAL_PORT = 'COM8'                            # Your ESP's COM port
AUDIO_FILE  = r"C:\path\to\mission_fixed.wav"  # Path to the theme audio
```

**To find your ESP's COM port:** open Device Manager (Win + X → Device Manager) → expand **Ports (COM & LPT)** → look for `USB-SERIAL CH340 (COMx)` or `Silicon Labs CP210x (COMx)`.

---

## Project Structure

```
.
├── sketch_oct24a.ino      # ESP8266 firmware — flash to board
├── pc_listener.py         # PC-side serial listener + HTTP server
├── pc_activity.py         # Activity logger (optional, independent)
├── requirements.txt       # Python dependencies
├── README.md              # This file
├── LICENSE                # MIT license
└── .gitignore             # Git ignore rules
```

Runtime files that are generated on first run and **not committed** to Git:

- `pc_events.log` — event log
- `pc_history.jsonl` — JSON event history
- `pc_activity.log` — activity snapshots
- `pc_activity.log.1` — rotated activity log

---

## Use Cases

### Home users
- Shut down gaming PCs automatically after a session
- Enforce kids' screen-time limits without paid apps
- Stop overnight electricity waste

### Small business
- Auto-shutdown of office PCs at closing time
- Fixed session timers for cyber cafés and gaming lounges
- End-of-day shutdown for co-working spaces

### Enterprise
- Session limits on bank teller PCs
- HIPAA-compliant timeout for healthcare workstations
- Audit trail for compliance

### Security-conscious environments
- **Dead-man switch** — PC auto-shuts down if the authorized phone leaves Wi-Fi range
- **Session timeout** — classified or sensitive work limited to a fixed duration
- **Emergency kill switch** — physical button (optional) triggers instant shutdown
- **Air-gapped operation** — no internet dependency, suitable for isolated networks

> This device performs a **legitimate OS shutdown** — it does not delete or corrupt data. For full data protection, combine with disk encryption (BitLocker, LUKS, FileVault).

---

## Why ₹400 Matters

| Solution | Cost | Graceful shutdown? |
|---|---|---|
| **This project** | **₹400** | ✅ Yes |
| Smart plug (Wemo, Wipro) | ₹2,500 | ❌ Hard power cut — corrupts files |
| Commercial timer app | ₹300/month | ❌ Doesn't actually shut down |
| TeamViewer subscription | ₹2,000/year | ❌ Manual only |
| Cloud automation service | ₹500/month | ❌ Depends on internet |

One PC left on 8 hours overnight wastes **₹15–20 per night** — that's **₹5,500+ per year**. This device pays for itself in under a month.

---

## Known Limitations

- ESP8266 supports **2.4 GHz Wi-Fi only** — cannot join 5 GHz networks
- Timer is **not persisted** across ESP reboots (add EEPROM if needed)
- `winsound` on Windows requires **PCM WAV** audio — convert with:
  ```bash
  ffmpeg -i input.mp3 -acodec pcm_s16le -ar 44100 -ac 2 mission_fixed.wav
  ```
- Windows Firewall may block port 8000 — allow it with:
  ```powershell
  New-NetFirewallRule -DisplayName "Control Hub Listener" -Direction Inbound -LocalPort 8000 -Protocol TCP -Action Allow -Profile Any
  ```

---

## Roadmap

- [x] Basic ESP8266 + PC shutdown flow
- [x] Phone dashboard with PIN protection
- [x] Timer with presets (1m / 5m / 10m / 30m / 1h)
- [x] Mission Impossible theme 15s before zero
- [x] 10-second voice countdown
- [x] ABORT button
- [x] Activity logging (window, idle, CPU, RAM)
- [ ] Multi-PC dashboard — control several PCs from one ESP
- [ ] Cloud logging via Firebase
- [ ] Home Assistant integration via MQTT
- [ ] Wake-on-LAN — reverse mode to turn ON the PC
- [ ] OLED display for standalone countdown
- [ ] Voice control via Alexa / Google Home
- [ ] Fingerprint / RFID authentication
- [ ] Scheduled shutdown at fixed times

---

## Contributing

Pull requests are welcome. For major changes, please open an issue first.

1. Fork the repo
2. Create a branch (`git checkout -b feature/my-feature`)
3. Commit (`git commit -m 'Add my feature'`)
4. Push (`git push origin feature/my-feature`)
5. Open a Pull Request

---

## License

Released under the **MIT License** — free for personal and commercial use. See [LICENSE](LICENSE) for the full text.

---

## Author

**[YOUR_NAME]**

- GitHub: [@[architdutta10-max]](https://github.com/[architdutta10-max])
- LinkedIn: [Archit Dutta](https://linkedin.com/in/[archit-dutta-4522693aa])

Built as a portfolio project for **Google DevFest**.

---

## Support

If this project helped you, gave you an idea, or saved you some electricity bill money — consider giving it a ⭐ on GitHub. It helps others find the project.