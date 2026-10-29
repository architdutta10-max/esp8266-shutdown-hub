import serial
import time
import os
import sys
import threading
import json
import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

# ===== CONFIG =====
SERIAL_PORT        = 'COM8'
BAUD_RATE          = 115200
AUDIO_FILE         = r"C:\Users\archi\OneDrive\Desktop\mission_fixed.wav"

BASE_DIR           = os.path.dirname(os.path.abspath(__file__))
EVENTS_LOG         = os.path.join(BASE_DIR, "pc_events.log")
HISTORY_FILE       = os.path.join(BASE_DIR, "pc_history.jsonl")

HTTP_PORT          = 8000
# ==================

_audio_lock    = threading.Lock()
_abort_flag    = threading.Event()
_sequence_lock = threading.Lock()
_log_lock      = threading.Lock()


def log_event(event, detail=""):
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"{ts} | {event} | {detail}\n"
    with _log_lock:
        try:
            with open(EVENTS_LOG, "a", encoding="utf-8") as f:
                f.write(line)
        except Exception as e:
            print(f"[event-log error] {e}")
        try:
            with open(HISTORY_FILE, "a", encoding="utf-8") as f:
                f.write(json.dumps({
                    "ts": datetime.datetime.now().isoformat(timespec="seconds"),
                    "event": event, "detail": detail,
                }) + "\n")
        except Exception as e:
            print(f"[hist error] {e}")


def log_hist(event, detail=""):
    log_event(event, detail)


def play_audio():
    if not os.path.isfile(AUDIO_FILE):
        print(f"[AUDIO] missing: {AUDIO_FILE}"); return
    try:
        import winsound
        winsound.PlaySound(
            AUDIO_FILE,
            winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT
        )
        print("[AUDIO] playing theme"); return
    except Exception as e:
        print(f"[AUDIO] winsound failed ({e})")
    try:
        from playsound import playsound
        threading.Thread(target=playsound, args=(AUDIO_FILE,), daemon=True).start()
    except Exception as e:
        print(f"[AUDIO] playsound failed ({e})")


def stop_audio():
    try:
        import winsound
        winsound.PlaySound(None, winsound.SND_PURGE)
    except Exception:
        pass


def speak_text(text):
    if _abort_flag.is_set(): return
    with _audio_lock:
        try:
            import pyttsx3
            eng = pyttsx3.init()
            eng.setProperty('rate', 170)
            eng.say(text); eng.runAndWait(); eng.stop()
        except Exception as e:
            print(f"[tts] {e}")


def beep(freq=1500, ms=200):
    try:
        import winsound
        winsound.Beep(freq, ms)
    except Exception:
        pass


def shutdown_pc():
    print("Executing shutdown...")
    log_event("SHUTDOWN_EXECUTED")
    if sys.platform.startswith('win'):
        os.system("shutdown /s /f /t 0")
    else:
        os.system("sudo shutdown -h now")


def mission_impossible():
    with _sequence_lock:
        _abort_flag.clear()
        print("\n*** SELF-DESTRUCT ***")
        log_event("SELF_DESTRUCT_STARTED")
        speak_text("This PC will self destruct in 10 seconds")
        for i in range(10, 0, -1):
            if _abort_flag.is_set():
                stop_audio()
                speak_text("Self destruct cancelled")
                log_event("ABORT_RECEIVED", str(i))
                return
            threading.Thread(target=beep, daemon=True).start()
            speak_text(str(i))
            print(f"Shutdown in {i}...")
        if _abort_flag.is_set():
            stop_audio(); return
        speak_text("Goodbye")
        time.sleep(0.3)
        stop_audio()
        shutdown_pc()


def trigger_sequence():
    if _sequence_lock.locked():
        print("[SEQ] already running"); return
    threading.Thread(target=mission_impossible, daemon=True).start()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith("/ping"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"alive":true}')
            return
        if self.path.startswith("/abort"):
            _abort_flag.set(); stop_audio()
            log_event("ABORT_HTTP")
            self.send_response(200); self.end_headers()
            self.wfile.write(b"ok")
            return
        if self.path.startswith("/shutdown"):
            log_event("HTTP_SHUTDOWN")
            self.send_response(200); self.end_headers()
            self.wfile.write(b"ok")
            trigger_sequence()
            return
        self.send_response(404); self.end_headers()

    def log_message(self, *a): pass


def start_http():
    srv = HTTPServer(("0.0.0.0", HTTP_PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    print(f"[HTTP] listening on :{HTTP_PORT}")


def main():
    print("=== CONTROL HUB PC LISTENER ===")
    print(f"[LOG] events log: {EVENTS_LOG}")
    print(f"[LOG] history:    {HISTORY_FILE}")

    log_event("LISTENER_STARTED")
    start_http()

    print(f"[SERIAL] opening {SERIAL_PORT} ...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        ser.rts = False
        ser.dtr = False
        time.sleep(2)
        ser.reset_input_buffer()
    except Exception as e:
        print(f"[SERIAL] ERROR: {e}")
        log_event("SERIAL_ERROR", str(e))
        sys.exit(1)

    print("[SERIAL] connected. Listening...")

    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line: continue

            if line.startswith("TIMER START:"):
                amount = line[len("TIMER START:"):].strip()
                print(f"\n[TIMER] Started — duration: {amount}\n")
                log_event("TIMER_STARTED", amount)
                continue

            if line.startswith("T-"):
                print(f"[TIMER] {line}")
                continue

            if line == "PLAY_THEME":
                print("[TIMER] Theme trigger (15s before zero)")
                log_event("THEME_15S")
                threading.Thread(target=play_audio, daemon=True).start()
                continue

            if line == "SELF_DESTRUCT":
                print("[TIMER] Countdown reached zero")
                log_event("SELF_DESTRUCT_SERIAL")
                trigger_sequence()
                continue

            if line == "ABORT":
                print("[TIMER] ABORT received")
                _abort_flag.set(); stop_audio()
                log_event("ABORT_SERIAL")
                continue

            print(f"[ESP] {line}")

        except KeyboardInterrupt:
            print("\nStopped."); break
        except Exception as e:
            print(f"[SERIAL] error: {e}"); break

    log_event("LISTENER_STOPPED")
    try: ser.close()
    except Exception: pass


if __name__ == "__main__":
    main()