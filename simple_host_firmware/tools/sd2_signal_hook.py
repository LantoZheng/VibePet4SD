"""
VibePet4SD — Copilot Signal Light Hook
=======================================
Monitors VS Code Copilot transcript logs and syncs signal mode
to SD2 small TV hardware via serial or HTTP.

Usage (same as vibecoding-signal-light):
  python sd2_signal_hook.py UserPromptSubmit     # Codex hook
  python sd2_signal_hook.py PreToolUse
  python sd2_signal_hook.py PermissionRequest
  python sd2_signal_hook.py Stop
  python sd2_signal_hook.py working               # direct mode
  python sd2_signal_hook.py idle

Config via environment variables:
  SD2_HOST=192.168.4.1      # WiFi HTTP (preferred)
  SD2_PORT=COM4              # Serial fallback

Lamp language:
  SessionStart/UserPromptSubmit/PreToolUse/PostToolUse → working (cycle)
  PermissionRequest → blocked (red flash)
  PostToolUseFailure → blocked (red flash)
  Notification → attention (yellow flash)
  Stop/SessionEnd → idle (green)
"""

import os
import sys
import time

# Event → signal mapping
EVENT_MAP = {
    "sessionstart":        "idle",
    "userpromptsubmit":    "working",
    "pretooluse":          "working",
    "posttooluse":         "working",
    "posttoolusefailure":  "blocked",
    "permissionrequest":   "blocked",
    "notification":        "attention",
    "stop":                "idle",
    "sessionend":          "idle",
    # Direct modes
    "idle":      "idle",
    "working":   "working",
    "attention": "attention",
    "blocked":   "blocked",
    "permission": "blocked",
    "off":       "off",
    "status":    "status",
}


def send_http(host, mode):
    import requests
    try:
        s = requests.Session()
        s.trust_env = False
        r = s.get(f"http://{host}/signal?mode={mode}", timeout=3)
        return r.status_code == 200
    except Exception as e:
        print(f"HTTP error: {e}", file=sys.stderr)
        return False


def send_serial(port, mode):
    import serial
    try:
        ser = serial.Serial(port, 115200, timeout=0.5)
        time.sleep(0.3)
        ser.write(f"SIGNAL {mode}\n".encode())
        ser.flush()
        time.sleep(0.3)
        resp = ser.read(256).decode("utf-8", errors="replace")
        ser.close()
        return "OK" in resp
    except Exception as e:
        print(f"Serial error: {e}", file=sys.stderr)
        return False


def watch_mode():
    """Continuously monitor VS Code Copilot session log and sync signal."""
    import glob

    port = os.environ.get("SD2_PORT", None)
    host = os.environ.get("SD2_HOST", None)

    ser = None
    if port:
        try:
            import serial
            ser = serial.Serial(port, 115200, timeout=0.5)
            time.sleep(3)  # Wait for firmware boot
            while ser.in_waiting: ser.read(ser.in_waiting)
            # Test communication
            ser.write(b'SIGNAL idle\n'); ser.flush()
            time.sleep(0.3)
            resp = ser.read(256).decode(errors='replace')
            if 'OK' in resp:
                print(f"Serial OK: {port}")
            else:
                print(f"Serial no response: {resp[:50]}")
                ser.close(); ser = None
        except Exception as e:
            print(f"Serial error: {e}")
            ser = None

    def send_mode(mode):
        nonlocal ser
        # Map internal modes to firmware SIGNAL commands
        signal_cmd = mode
        if mode == "executing": signal_cmd = "executing"  # firmware maps to WORKING
        try:
            if ser:
                ser.write(f"SIGNAL {signal_cmd}\n".encode())
                ser.flush()
                time.sleep(0.15)
                while ser.in_waiting: ser.read(ser.in_waiting)
                return True
            elif host:
                import requests
                s = requests.Session(); s.trust_env = False
                r = s.get(f"http://{host}/signal?mode={mode}", timeout=1)
                return r.status_code == 200
        except Exception as e:
            print(f"  Send error: {e}")
        return False

    # Find VS Code session log
    sessions_dir = os.path.expandvars(r"%APPDATA%\Code\User\workspaceStorage")
    
    def find_latest_log():
        best = None
        best_mtime = 0
        for ws_dir in os.listdir(sessions_dir):
            copilot_dir = os.path.join(sessions_dir, ws_dir, 'GitHub.copilot-chat')
            if not os.path.isdir(copilot_dir): continue
            for sub in ['transcripts', 'debug-logs']:
                sub_dir = os.path.join(copilot_dir, sub)
                if not os.path.isdir(sub_dir): continue
                for entry in os.listdir(sub_dir):
                    fp = os.path.join(sub_dir, entry)
                    if fp.endswith('.jsonl'):
                        mtime = os.path.getmtime(fp)
                        if mtime > best_mtime:
                            best_mtime = mtime
                            best = fp
        return best
    
    print(f"Watching Copilot logs...")
    print(f"Press Ctrl+C to stop")
    
    last_mode = "idle"
    last_send = 0
    working_since = 0
    debug_printed = False
    cached_log = None
    cache_time = 0
    last_size = 0
    last_change = 0
    last_turn_start = 0   # timestamp of most recent turn_start (any type)
    
    def parse_turn_starts(tail_text):
        """Extract the latest turn_start timestamp from log tail.
        Looks for both user.turn_start and assistant.turn_start."""
        latest = 0
        pos = 0
        while True:
            pos = tail_text.find('"type":"', pos)
            if pos < 0:
                break
            type_start = pos + 8
            type_end = tail_text.find('"', type_start)
            if type_end < 0:
                break
            evt_type = tail_text[type_start:type_end]
            if evt_type.endswith('.turn_start'):
                # Found a turn_start — extract its timestamp
                ts_marker = '"timestamp":"'
                ts_s = tail_text.find(ts_marker, type_end)
                if ts_s > 0:
                    ts_s += len(ts_marker)
                    ts_e = tail_text.find('"', ts_s)
                    if ts_e > 0:
                        try:
                            from datetime import datetime
                            evt = datetime.fromisoformat(tail_text[ts_s:ts_e].replace('Z','+00:00'))
                            latest = max(latest, evt.timestamp())
                        except:
                            pass
            pos = type_end + 1
        return latest
    
    while True:
        try:
            # Cache the log path — only re-scan every 30 seconds
            now = time.time()
            if not cached_log or now - cache_time > 30:
                cached_log = find_latest_log()
                cache_time = now
                if cached_log and not debug_printed:
                    debug_printed = True
                    print(f"Watching: {cached_log}")
            
            if not cached_log:
                time.sleep(0.5)
                continue
            
            try:
                size = os.path.getsize(cached_log)
                if size < 100: 
                    time.sleep(0.5)
                    continue
                
                now = time.time()
                if size != last_size:
                    last_size = size
                    last_change = now
                
                new_mode = last_mode
                if now - last_change < 3:
                    # File changing — Copilot is active
                    with open(cached_log, 'r', encoding='utf-8', errors='ignore') as f:
                        f.seek(max(0, size - 4096))
                        tail = f.read()
                    has_tools = '"toolRequests"' in tail
                    if has_tools:
                        new_mode = "executing"
                    else:
                        new_mode = "working"
                    # Update last_turn_start while file is active
                    ts = parse_turn_starts(tail)
                    if ts > last_turn_start:
                        last_turn_start = ts
                elif now - last_change > 1.5 and last_mode in ("working","executing"):
                    # Conversation idle detection: look at last TURN_START age,
                    # NOT turn_end. During multi-turn conversations, each new
                    # turn_start resets the timer, preventing false idles.
                    if last_turn_start > 0 and now - last_turn_start > 15:
                        new_mode = "idle"
                    elif now - last_change > 60:
                        new_mode = "idle"  # fallback: no activity at all
                
                if new_mode != last_mode and now - last_send > 0.5:
                    if send_mode(new_mode):
                        dt = now - last_change
                        ts_age = now - last_turn_start if last_turn_start > 0 else -1
                        print(f"[{time.strftime('%H:%M:%S')}] {last_mode} -> {new_mode} (file:{dt:.1f}s, turn_age:{ts_age:.1f}s)")
                        last_mode = new_mode
                        last_send = now
                    
            except Exception as e:
                pass
            
            time.sleep(0.3)
        except KeyboardInterrupt:
            print("\nStopped.")
            if ser: ser.close()
            break


def main():
    if len(sys.argv) < 2:
        print("Usage: sd2_signal_hook.py <event>", file=sys.stderr)
        print("       sd2_signal_hook.py --watch   (auto-monitor Copilot)", file=sys.stderr)
        sys.exit(1)

    if sys.argv[1] == "--watch":
        watch_mode()
        return

    event = sys.argv[1].lower().strip()
    mode = EVENT_MAP.get(event, "working")

    host = os.environ.get("SD2_HOST", "192.168.4.1")
    port = os.environ.get("SD2_PORT", None)

    if port:
        ok = send_serial(port, mode)
    else:
        ok = send_http(host, mode)

    if ok:
        print(f"SD2 signal: {mode}")
    else:
        print(f"SD2 signal failed: {mode}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
