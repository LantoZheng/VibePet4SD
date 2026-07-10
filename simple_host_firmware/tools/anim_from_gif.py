"""
Convert GIF to SD2 JPEG frame animation and upload via WiFi or serial.

Usage:
  python anim_from_gif.py cute_cat.gif
  python anim_from_gif.py --wifi 192.168.4.1 --name blink animation.gif
  python anim_from_gif.py --port COM4 --fps 10 test.gif

The script:
  1. Extracts all frames from the GIF
  2. Resizes to 240x240  
  3. Saves as JPEG frames
  4. Creates manifest.json
  5. Uploads via WiFi (fast) or serial
"""

import argparse
import json
import os
import struct
import sys
import time

WIDTH = 240
HEIGHT = 240


def extract_gif_frames(gif_path, output_dir):
    """Extract all frames from a GIF, resize to 240x240, save as JPEG."""
    try:
        from PIL import Image, ImageSequence
    except ImportError:
        print("ERROR: pip install Pillow")
        sys.exit(1)

    os.makedirs(output_dir, exist_ok=True)

    gif = Image.open(gif_path)
    frames = []

    for i, frame in enumerate(ImageSequence.Iterator(gif)):
        # Convert to RGB (drop transparency, use black background)
        rgb = Image.new("RGB", frame.size, (0, 0, 0))
        if frame.mode == "RGBA":
            rgb.paste(frame, mask=frame.split()[3])
        elif frame.mode == "P":
            rgb = frame.convert("RGBA")
            bg = Image.new("RGB", rgb.size, (0, 0, 0))
            bg.paste(rgb, mask=rgb.split()[3])
            rgb = bg
        else:
            rgb = frame.convert("RGB")

        # Resize to 240x240
        if rgb.size != (WIDTH, HEIGHT):
            rgb = rgb.resize((WIDTH, HEIGHT), Image.LANCZOS)

        out_path = os.path.join(output_dir, f"frame_{i:02d}.jpg")
        rgb.save(out_path, "JPEG", quality=95, subsampling=0)  # 4:4:4 for clean edges
        frames.append(out_path)

    gif.close()
    return frames


def upload_http(host, anim_name, frames_dir, manifest, frames):
    """Upload via WiFi HTTP."""
    import requests
    base = f"http://{host}"

    # Manifest
    manifest_json = json.dumps(manifest)
    r = requests.post(f"{base}/upload",
        files={"file": (f"/anim/{anim_name}/manifest.json", manifest_json.encode(), "application/json")})
    print(f"  manifest: {r.status_code} {r.text}")

    # Frames
    for i, frame_path in enumerate(frames):
        fname = f"frame_{i:02d}.jpg"
        remote = f"/anim/{anim_name}/{fname}"
        with open(frame_path, "rb") as f:
            r = requests.post(f"{base}/upload", files={"file": (remote, f, "image/jpeg")})
        if r.status_code != 200:
            print(f"  FAIL {fname}: {r.status_code}")
            return False
        if i % 5 == 0 or i == len(frames) - 1:
            print(f"  {fname} OK ({os.path.getsize(frame_path)} bytes) [{i+1}/{len(frames)}]")

    # Play
    r = requests.get(f"{base}/anim/play?name={anim_name}")
    print(f"Play: {r.status_code} {r.text}")
    return True


def read_until(ser, marker, timeout=8):
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        chunk = ser.read(4096)
        if chunk:
            buf.extend(chunk)
            text = buf.decode("utf-8", errors="replace")
            if marker in text:
                return text
        else:
            time.sleep(0.05)
    return buf.decode("utf-8", errors="replace")


def send_command(ser, cmd, marker=None, timeout=5):
    ser.write((cmd + "\n").encode())
    ser.flush()
    if marker:
        return read_until(ser, marker, timeout)
    time.sleep(0.3)
    return ser.read(4096).decode("utf-8", errors="replace")


def upload_serial_file(ser, remote_path, data):
    """Upload one file via IMG_BIN_BEGIN."""
    resp = send_command(ser, f"IMG_BIN_BEGIN {remote_path} {len(data)}", "READY", 5)
    if "ERR" in resp:
        print(f"    ERR: {resp.strip()}")
        return False

    offset = 0
    while offset < len(data):
        chunk = data[offset:offset + 256]
        ser.write(chunk)
        ser.flush()
        offset += len(chunk)
        resp = read_until(ser, "\n", 5)
        if "IMG_BIN_DONE" in resp:
            break
        if "ERR" in resp:
            print(f"    ERR @ {offset}: {resp.strip()}")
            return False

    return True


def main():
    parser = argparse.ArgumentParser(description="Convert GIF to SD2 JPEG anim")
    parser.add_argument("gif", help="Input GIF file")
    parser.add_argument("--name", default=None, help="Animation name (default: GIF filename)")
    parser.add_argument("--fps", type=int, default=10, help="Playback FPS")
    parser.add_argument("--loop", action="store_true", default=True)
    parser.add_argument("--wifi", default=None, help="WiFi host for fast upload")
    parser.add_argument("--port", default="COM4", help="Serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", default=None, help="Output dir for frames (keeps files)")
    args = parser.parse_args()

    anim_name = args.name or os.path.splitext(os.path.basename(args.gif))[0]
    # Sanitize name
    anim_name = "".join(c for c in anim_name if c.isalnum() or c in "._-")

    output_dir = args.out or f"_anim_{anim_name}"

    print(f"Extracting frames from {args.gif}...")
    frames = extract_gif_frames(args.gif, output_dir)
    print(f"  {len(frames)} frames extracted to {output_dir}/")

    total_size = sum(os.path.getsize(p) for p in frames)
    raw_size = len(frames) * WIDTH * HEIGHT * 2
    print(f"  Total JPEG: {total_size} bytes (vs {raw_size} raw = {raw_size//max(total_size,1)}x)")

    manifest = {"fps": args.fps, "loop": args.loop, "frames": len(frames)}
    with open(os.path.join(output_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f)

    if args.wifi:
        print(f"\nUploading to {args.wifi} via WiFi...")
        ok = upload_http(args.wifi, anim_name, output_dir, manifest, frames)
    else:
        import serial
        print(f"\nUploading via serial {args.port}...")
        ser = serial.Serial(args.port, args.baud, timeout=0.25)
        time.sleep(0.5)
        ser.reset_input_buffer()

        # Upload manifest
        mjson = json.dumps(manifest).encode()
        print(f"  manifest.json ({len(mjson)} bytes)")
        if not upload_serial_file(ser, f"/anim/{anim_name}/manifest.json", mjson):
            print("FAILED")
            sys.exit(1)

        # Upload frames
        for i, frame_path in enumerate(frames):
            fname = f"frame_{i:02d}.jpg"
            with open(frame_path, "rb") as f:
                data = f.read()
            print(f"  {fname} ({len(data)} bytes) [{i+1}/{len(frames)}]")
            if not upload_serial_file(ser, f"/anim/{anim_name}/{fname}", data):
                print("FAILED")
                sys.exit(1)

        # Play
        resp = send_command(ser, f"ANIM_PLAY {anim_name}", "OK", 5)
        print(f"Play: {resp.strip()}")
        ser.close()
        ok = True

    if ok:
        print(f"\nDone! Animation '{anim_name}' uploaded and playing.")
        print(f"WiFi: http://{args.wifi or '192.168.4.1'}/anim/play?name={anim_name}")
        print(f"Serial: ANIM_PLAY {anim_name}")
        print(f"Shell: ANIM_LIST, ANIM_STOP")


if __name__ == "__main__":
    main()
