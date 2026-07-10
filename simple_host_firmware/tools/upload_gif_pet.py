#!/usr/bin/env python3
"""Convert GIF-based Codex Pets to SD2 format and upload.

Usage:
  python tools/upload_gif_pet.py <pet_dir> [--port COM4] [--scale 100]
  
Pet dir should contain GIFs named: <state>.gif
e.g. idle.gif, waving.gif, running.gif, failed.gif, etc.
"""

import argparse, json, os, serial, sys, time
from pathlib import Path
from io import BytesIO

try:
    from PIL import Image, ImageSequence
except ImportError:
    print("ERROR: Pillow required. pip install Pillow")
    sys.exit(1)

# State name mapping: GIF filename → Codex pet state
STATE_MAP = {
    "idle": "idle", "waiting": "wave", "waving": "wave", "wave": "wave",
    "running": "run", "run": "run",
    "failed": "failed", "error": "failed",
    "review": "review",
    "jumping": "jump", "jump": "jump",
}


def extract_gif_frames(gif_path: Path, frame_size: int, quality: int = 75):
    """Extract all frames from a GIF, resize, return list of JPEG bytes."""
    img = Image.open(gif_path)
    frames = []
    for frame in ImageSequence.Iterator(img):
        rgba = frame.convert("RGBA")
        # Scale
        w, h = rgba.size
        scale = frame_size / max(w, h)
        new_w, new_h = int(w * scale), int(h * scale)
        rgba = rgba.resize((new_w, new_h), Image.LANCZOS)
        # Composite on black background
        bg = Image.new("RGB", (new_w, new_h), (0, 0, 0))
        bg.paste(rgba, (0, 0), rgba)
        buf = BytesIO()
        bg.save(buf, "JPEG", quality=quality)
        frames.append(buf.getvalue())
    return frames


def upload_frames(port: str, slug: str, all_frames: dict, baudrate: int = 115200):
    """Upload pet frames to SD2 via serial."""
    ser = serial.Serial(port, baudrate, timeout=0.5)
    print("Opening serial...", end=" ", flush=True)
    ser = None
    for attempt in range(10):
        try:
            ser = serial.Serial(port, baudrate, timeout=0.5)
            break
        except Exception:
            time.sleep(1)
    if ser is None:
        print("FAILED - port locked")
        return False
    # Wait for boot to complete
    time.sleep(6)
    # Drain everything
    ser.reset_input_buffer()
    while ser.in_waiting:
        ser.read(ser.in_waiting)
        time.sleep(0.2)
    print("ready!")

    def cmd(c, expect="OK"):
        ser.write((c + "\n").encode())
        ser.flush()
        out = ""
        t0 = time.time()
        while time.time() - t0 < 5:
            if ser.in_waiting:
                out += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                if expect in out or "ERR" in out:
                    return out
            time.sleep(0.05)
        return out

    r = cmd(f"PET_UPLOAD {slug} {len(all_frames)}", "OK PET_UPLOAD")
    if "OK" not in r:
        print(f"ERROR: {r.strip()[:80]}")
        ser.close()
        return False

    for state_name, frames in all_frames.items():
        r = cmd(f"PET_STATE {state_name} {len(frames)}", "OK PET_STATE")
        if "OK" not in r:
            print(f"ERROR state {state_name}: {r.strip()[:80]}")
            ser.close()
            return False
        sys.stdout.write(f"  {state_name} ({len(frames)} frames)... ")
        sys.stdout.flush()
        time.sleep(0.5)  # Let firmware settle

        for i, jpeg_data in enumerate(frames):
            size = len(jpeg_data)
            ser.write(f"PET_FRAME {i} {size}\n".encode())
            ser.flush()
            time.sleep(0.3)  # let firmware enter binary mode
            # Send JPEG data directly (firmware reads byte-exact)
            ser.write(jpeg_data)
            ser.flush()
            time.sleep(0.5)
            # Wait for OK (flash write may take several seconds)
            t0 = time.time(); ok = False; ack = ""
            while time.time() - t0 < 8:
                time.sleep(0.1)
                if ser.in_waiting:
                    ack += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    if "OK PET_FRAME" in ack: ok = True; break
            if not ok:
                print(f"\nFrame {i} no ack: [{ack.strip()[:100]}]"); ser.close(); return False
            # Send data
            ser.write(jpeg_data); ser.flush()
            time.sleep(0.3)
            # Wait OK
            t0 = time.time(); ok = False; ack = ""
            while time.time() - t0 < 5:
                if ser.in_waiting:
                    ack += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    if "OK PET_FRAME" in ack: ok = True; break
                time.sleep(0.02)
            if not ok:
                print(f"\nFrame {i} ERROR: {ack.strip()[:80]}"); ser.close(); return False

        print("OK")

    r = cmd("PET_SAVE")
    print(f"Save: {r.strip()}")
    r = cmd(f"PET_SELECT {slug}")
    print(f"Select: {r.strip()}")
    ser.close()
    return True


def main():
    parser = argparse.ArgumentParser(description="Upload GIF-based Codex Pet to SD2")
    parser.add_argument("pet_dir", help="Directory with GIF files per state")
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--scale", type=int, default=100)
    parser.add_argument("--quality", type=int, default=70)
    parser.add_argument("--name", default=None, help="Pet name/slug")
    args = parser.parse_args()

    pet_dir = Path(args.pet_dir)
    if not pet_dir.is_dir():
        print(f"ERROR: {pet_dir} not found")
        sys.exit(1)

    slug = args.name or pet_dir.name

    # Find all GIFs
    gif_files = sorted(pet_dir.glob("*.gif"))
    if not gif_files:
        print("ERROR: no .gif files found")
        sys.exit(1)

    print(f"Found {len(gif_files)} GIFs in {pet_dir}")

    all_frames = {}
    for gif_path in gif_files:
        # Extract state name from filename: "claw-d-idle.gif" → "idle"
        stem = gif_path.stem  # e.g. "claw-d-idle"
        parts = stem.split("-")
        raw_state = parts[-1] if len(parts) > 1 else stem
        state = STATE_MAP.get(raw_state.lower(), raw_state.lower())

        frames = extract_gif_frames(gif_path, args.scale, args.quality)
        if frames:
            all_frames[state] = frames
            print(f"  {gif_path.name} → {state}: {len(frames)} frames")

    if not all_frames:
        print("ERROR: no frames extracted")
        sys.exit(1)

    print(f"\nUploading '{slug}' ({len(all_frames)} states) to {args.port}...")
    ok = upload_frames(args.port, slug, all_frames)
    if ok:
        print(f"\n✅ Pet '{slug}' uploaded! Use PET_SHOW to display.")
    else:
        print("\n❌ Upload failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
