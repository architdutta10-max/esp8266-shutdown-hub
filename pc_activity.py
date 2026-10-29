import time
import os
import json
import datetime
import threading

import psutil
try:
    import win32gui
    HAS_WIN32 = True
except ImportError:
    HAS_WIN32 = False
    print("[ACTIVITY] pywin32 not installed — active window will show 'unknown'")

# ===== CONFIG =====
ACTIVITY_INTERVAL = 5
LOG_MAX_MB        = 10

BASE_DIR     = os.path.dirname(os.path.abspath(__file__))
ACTIVITY_LOG = os.path.join(BASE_DIR, "pc_activity.log")
# ==================

_log_lock = threading.Lock()


def log_activity(win, idle, cpu, ram):
    ts = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"{ts} | win={win} | idle={idle}s | cpu={cpu}% | ram={ram}%\n"
    with _log_lock:
        try:
            with open(ACTIVITY_LOG, "a", encoding="utf-8") as f:
                f.write(line)
        except Exception as e:
            print(f"[log error] {e}")


def rotate_log_if_needed(max_mb=LOG_MAX_MB):
    try:
        if os.path.getsize(ACTIVITY_LOG) > max_mb * 1024 * 1024:
            backup = ACTIVITY_LOG + ".1"
            if os.path.exists(backup):
                os.remove(backup)
            os.rename(ACTIVITY_LOG, backup)
            print(f"[ACTIVITY] rotated log (>{max_mb} MB)")
    except Exception:
        pass


def get_active_window():
    if not HAS_WIN32:
        return "unknown"
    try:
        hwnd = win32gui.GetForegroundWindow()
        title = win32gui.GetWindowText(hwnd)
        return (title or "(no title)")[:80].replace("|", "/")
    except Exception:
        return "unknown"


def get_idle_seconds():
    import ctypes
    class LASTINPUTINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_uint), ("dwTime", ctypes.c_uint)]
    lii = LASTINPUTINFO()
    lii.cbSize = ctypes.sizeof(LASTINPUTINFO)
    if ctypes.windll.user32.GetLastInputInfo(ctypes.byref(lii)):
        millis = ctypes.windll.kernel32.GetTickCount() - lii.dwTime
        return millis // 1000
    return 0


def activity_snapshot():
    try:
        cpu = psutil.cpu_percent(interval=None)
        ram = psutil.virtual_memory().percent
    except Exception:
        cpu = ram = -1
    return {
        "win":  get_active_window(),
        "idle": get_idle_seconds(),
        "cpu":  int(cpu),
        "ram":  int(ram),
    }


def main():
    print("=== PC ACTIVITY MONITOR ===")
    print(f"[LOG] {ACTIVITY_LOG}")
    print(f"[INFO] interval: {ACTIVITY_INTERVAL}s")
    print("[INFO] Press Ctrl+C to stop.\n")

    while True:
        rotate_log_if_needed()
        snap = activity_snapshot()
        log_activity(snap["win"], snap["idle"], snap["cpu"], snap["ram"])
        print(f"[{datetime.datetime.now().strftime('%H:%M:%S')}] {json.dumps(snap)}")

        try:
            time.sleep(ACTIVITY_INTERVAL)
        except KeyboardInterrupt:
            print("\nStopped.")
            return


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
