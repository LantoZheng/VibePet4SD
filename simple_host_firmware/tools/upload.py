"""
SD2-OS Unified Upload Tool
===========================
Upload files, scripts, and binaries to SD2 ESP8266 via:
  --wifi   HTTP (fast, recommended)  
  --serial COM port (no WiFi needed)
  --ws     WebSocket (real-time progress)

Auto-detects file type:
  .sh   → auto-compile to .s2b after upload
  .s2b  → binary script (ready to run)
  .jpg  → JPEG image (auto-display)
  .raw  → raw RGB565 image
  .json → config/manifest
  other → generic file

Usage:
  python upload.py my_script.sh
  python upload.py --wifi 192.168.4.1 photo.jpg
  python upload.py --serial COM4 animation.gif --as-anim myanim
  python upload.py --ws 192.168.4.1 module.sh --to /mod/mymod/
"""

import argparse
import os
import struct
import sys
import time


def detect_type(filepath):
    """Detect file type and return (category, extension)."""
    ext = os.path.splitext(filepath)[1].lower()
    mapping = {
        ".sh":  ("script", ".sh"),
        ".s2b": ("binary", ".s2b"),
        ".jpg": ("image", ".jpg"),
        ".jpeg":("image", ".jpg"),
        ".png": ("image_convert", ".jpg"),
        ".gif": ("anim", ".gif"),
        ".bmp": ("image_convert", ".jpg"),
        ".raw": ("image", ".raw"),
        ".json":("config", ".json"),
        ".bin": ("binary", ".bin"),
        ".cfg": ("config", ".cfg"),
    }
    return mapping.get(ext, ("file", ext or ".bin"))


# ============================================================
#  HTTP Upload (WiFi)
# ============================================================

def upload_http(host, local_path, remote_path, compile_sh=True):
    """Upload a file via HTTP POST to /upload."""
    import requests

    remote = remote_path if remote_path else "/" + os.path.basename(local_path)
    remote = "/" + remote.lstrip("/")

    with open(local_path, "rb") as f:
        data = f.read()

    print(f"  HTTP → {host}{remote} ({len(data)} bytes)")
    r = requests.post(f"http://{host}/upload",
        files={"file": (remote, data, "application/octet-stream")},
        timeout=30)
    print(f"  ← {r.status_code} {r.text[:120]}")

    if r.status_code != 200:
        return False

    # Auto-compile .sh scripts
    if compile_sh and remote.endswith(".sh"):
        name = remote.replace(".sh", "")
        print(f"  Auto-compiling {name}...")
        r2 = requests.get(f"http://{host}/script/compile?name={name}", timeout=10)
        print(f"  ← {r2.status_code} {r2.text[:120]}")

    return True


# ============================================================
#  Serial Upload (COM port)
# ============================================================

def upload_serial(port, baud, local_path, remote_path, compile_sh=True, show=False):
    """Upload via serial IMG_BIN_BEGIN protocol."""
    import serial

    remote = remote_path if remote_path else "/" + os.path.basename(local_path)
    remote = "/" + remote.lstrip("/")

    with open(local_path, "rb") as f:
        data = f.read()

    ser = serial.Serial(port, baud, timeout=0.3)
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Send begin command
    cmd = f"IMG_BIN_BEGIN {remote} {len(data)}\n"
    ser.write(cmd.encode())
    ser.flush()

    # Wait for READY
    resp = bytearray()
    start = time.time()
    while time.time() - start < 5:
        chunk = ser.read(256)
        if chunk:
            resp.extend(chunk)
            if b"READY" in resp:
                break
        else:
            time.sleep(0.05)

    resp_str = resp.decode("utf-8", errors="replace")
    if "ERR" in resp_str:
        print(f"  ERR: {resp_str.strip()}")
        ser.close()
        return False

    print(f"  SERIAL → {remote} ({len(data)} bytes)")

    # Send data in 256-byte blocks
    offset = 0
    while offset < len(data):
        chunk = data[offset:offset + 256]
        ser.write(chunk)
        ser.flush()
        offset += len(chunk)

        # Wait for ACK or DONE
        ack_start = time.time()
        ack_buf = bytearray()
        while time.time() - ack_start < 3:
            c = ser.read(256)
            if c:
                ack_buf.extend(c)
                if b"IMG_BIN_DONE" in ack_buf or b"ERR" in ack_buf:
                    break
            else:
                time.sleep(0.02)

        ack_str = ack_buf.decode("utf-8", errors="replace")
        if "ERR" in ack_str:
            print(f"  ERR: {ack_str.strip()}")
            ser.close()
            return False
        if "IMG_BIN_DONE" in ack_str:
            print(f"  ← {ack_str.strip()}")
            break

        if offset % 8192 < 256 or offset >= len(data):
            pct = offset * 100 // len(data)
            print(f"  progress: {offset}/{len(data)} ({pct}%)")

    # Auto-compile
    if compile_sh and remote.endswith(".sh"):
        name = remote.replace(".sh", "")
        print(f"  Auto-compiling...")
        ser.write(f"SCRIPT_COMPILE {name}\n".encode())
        ser.flush()
        time.sleep(1)
        resp = ser.read(512).decode("utf-8", errors="replace")
        for line in resp.split("\n"):
            if "COMPILED" in line or "ERR" in line:
                print(f"  ← {line.strip()}")

    # Auto-show images
    if show and remote.lower().endswith((".jpg", ".raw", ".jpeg", ".png")):
        ser.write(f"SHOW_FILE {remote}\n".encode())
        ser.flush()
        time.sleep(0.5)
        resp = ser.read(256).decode("utf-8", errors="replace")
        for line in resp.split("\n"):
            if "OK" in line or "ERR" in line:
                print(f"  ← display: {line.strip()}")

    ser.close()
    return True


# ============================================================
#  WebSocket Upload (WiFi, with progress)
# ============================================================

def upload_ws(host, local_path, remote_path, compile_sh=True):
    """Upload via WebSocket with real-time progress."""
    import asyncio

    remote = remote_path if remote_path else "/" + os.path.basename(local_path)
    remote = "/" + remote.lstrip("/")

    with open(local_path, "rb") as f:
        data = f.read()

    print(f"  WS → {host}:81 {remote} ({len(data)} bytes)")

    try:
        # Use HTTP upload for now (WebSocket chunked upload needs protocol extension)
        # Fall through to HTTP
        print("  (using HTTP, WS upload protocol TBD)")
        return upload_http(host, local_path, remote_path, compile_sh)
    except Exception as e:
        print(f"  WS ERR: {e}")
        return False


# ============================================================
#  Main
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="SD2-OS Unified Uploader")
    parser.add_argument("file", help="File to upload")
    parser.add_argument("--to", default=None, help="Remote path on device")
    parser.add_argument("--wifi", default=None, help="WiFi host (e.g. 192.168.4.1)")
    parser.add_argument("--serial", default=None, help="Serial port (e.g. COM4)")
    parser.add_argument("--ws", default=None, help="WebSocket host")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--show", action="store_true", help="Display image after upload")
    parser.add_argument("--no-compile", action="store_true", help="Skip auto-compile of .sh files")
    parser.add_argument("--as-anim", default=None, help="Upload as animation (convert GIF→JPEG frames)")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"ERROR: file not found: {args.file}")
        sys.exit(1)

    filetype, ext = detect_type(args.file)
    print(f"File: {args.file} → {filetype} ({ext})")

    # Handle animation conversion
    if args.as_anim or (filetype == "anim" and not args.to):
        anim_name = args.as_anim or os.path.splitext(os.path.basename(args.file))[0]
        print(f"Converting GIF to animation '{anim_name}'...")
        try:
            from PIL import Image
            import io

            gif = Image.open(args.file)
            frames_dir = f"_upload_{anim_name}"
            os.makedirs(frames_dir, exist_ok=True)

            import json as jmod
            frame_count = 0
            try:
                while True:
                    gif.seek(frame_count)
                    rgb = gif.convert("RGB")
                    if rgb.size != (240, 240):
                        rgb = rgb.resize((240, 240), Image.LANCZOS)
                    rgb.save(f"{frames_dir}/frame_{frame_count:02d}.jpg", "JPEG", quality=95, subsampling=0)
                    frame_count += 1
            except EOFError:
                pass

            manifest = {"fps": 10, "loop": True, "frames": frame_count}
            with open(f"{frames_dir}/manifest.json", "w") as f:
                jmod.dump(manifest, f)

            print(f"  {frame_count} frames extracted")

            # Upload all frames
            transport = args.wifi or (f"serial::{args.serial}" if args.serial else None)
            if not transport:
                print("ERROR: need --wifi or --serial to upload")
                sys.exit(1)

            host = args.wifi
            ok = True
            # Upload manifest
            remote_base = f"/anim/{anim_name}"
            for i in range(frame_count):
                fpath = f"{frames_dir}/frame_{i:02d}.jpg"
                remote = f"{remote_base}/frame_{i:02d}.jpg"
                if host:
                    ok = upload_http(host, fpath, remote, False) and ok
                else:
                    ok = upload_serial(args.serial, args.baud, fpath, remote, False) and ok

            # Upload manifest
            if host:
                upload_http(host, f"{frames_dir}/manifest.json", f"{remote_base}/manifest.json", False)
            else:
                upload_serial(args.serial, args.baud, f"{frames_dir}/manifest.json", f"{remote_base}/manifest.json", False)

            print(f"Done! Run: ANIM_PLAY {anim_name}")
            sys.exit(0 if ok else 1)

        except ImportError:
            print("ERROR: pip install Pillow to convert GIFs")
            sys.exit(1)

    # Normal upload
    transport = args.wifi or args.serial or args.ws
    if not transport:
        # Auto-detect: prefer WiFi if available
        if args.wifi is None and args.serial is None:
            # Try common WiFi host
            try:
                import requests
                requests.get("http://192.168.4.1/status", timeout=2)
                args.wifi = "192.168.4.1"
                print("Auto-detected WiFi @ 192.168.4.1")
            except:
                if args.serial is None:
                    args.serial = "COM4"
                    print("Falling back to serial COM4")

    ok = False
    if args.wifi:
        ok = upload_http(args.wifi, args.file, args.to, not args.no_compile)
    elif args.ws:
        ok = upload_ws(args.ws, args.file, args.to, not args.no_compile)
    elif args.serial:
        ok = upload_serial(args.serial, args.baud, args.file, args.to,
                          not args.no_compile, args.show)
    else:
        print("ERROR: specify --wifi, --serial, or --ws")
        sys.exit(1)

    if ok:
        print("OK")
    else:
        print("FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
