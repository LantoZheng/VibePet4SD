#!/usr/bin/env python3
"""Codex Pets → SD2 Upload Tool

Converts a Codex Pets package (pet.json + spritesheet.png/webp) into
SD2-compatible JPEG frames and uploads them to the device.

Usage:
  python tools/upload_pet.py <pet_dir> [--port COM4] [--scale 120]

The pet_dir must contain:
  pet.json          — Codex Pets metadata
  spritesheet.{png,webp} — 8 rows × 9 cols spritesheet
"""

import argparse, json, os, serial, struct, sys, time
from pathlib import Path
from io import BytesIO

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow required. Install with: pip install Pillow")
    sys.exit(1)

# Animation state row mapping (Codex Pets standard)
STATE_ROWS = {
    "idle": 0, "wave": 1, "run": 2, "failed": 3,
    "review": 4, "jump": 5, "extra1": 6, "extra2": 7,
}

COLS = 9
ROWS = 8


def find_spritesheet(pet_dir: Path) -> Path:
    for ext in (".png", ".webp", ".jpg", ".jpeg"):
        p = pet_dir / f"spritesheet{ext}"
        if p.exists():
            return p
    raise FileNotFoundError(f"No spritesheet found in {pet_dir}")


def slice_and_convert(pet_dir: Path, frame_size: int, quality: int = 75):
    """Slice spritesheet into individual JPEG frames, scaled to frame_size."""
    pet_json = pet_dir / "pet.json"
    if not pet_json.exists():
        raise FileNotFoundError(f"{pet_json} not found")

    with open(pet_json) as f:
        meta = json.load(f)

    name = meta.get("name", pet_dir.name)
    slug = meta.get("slug", pet_dir.name)
    states = meta.get("animationStates", meta.get("states", ["idle"]))

    spritesheet_path = find_spritesheet(pet_dir)
    img = Image.open(spritesheet_path).convert("RGBA")
    fw = img.width // COLS
    fh = img.height // ROWS
    print(f"Spritesheet: {img.width}x{img.height}, frame={fw}x{fh}")

    frames = {}
    for state_name in states:
        row = STATE_ROWS.get(state_name)
        if row is None:
            print(f"  Skip unknown state: {state_name}")
            continue
        state_frames = []
        for col in range(COLS):
            left, top = col * fw, row * fh
            frame = img.crop((left, top, left + fw, top + fh))
            # Scale to fit SD2 screen (with border margin)
            scale = frame_size / max(fw, fh)
            new_w, new_h = int(fw * scale), int(fh * scale)
            frame = frame.resize((new_w, new_h), Image.LANCZOS)
            # Convert to RGB JPEG bytes
            buf = BytesIO()
            frame.convert("RGB").save(buf, "JPEG", quality=quality)
            state_frames.append(buf.getvalue())
        frames[state_name] = state_frames
        print(f"  {state_name}: {len(state_frames)} frames")

    return name, slug, frames


def upload_via_serial(port: str, name: str, slug: str, frames: dict,
                       baudrate: int = 115200):
    """Upload pet frames to SD2 via serial port."""
    ser = serial.Serial(port, baudrate, timeout=0.5)
    # Wait for boot to complete — look for shell marker
    print("Waiting for device...", end=" ", flush=True)
    t0 = time.time()
    buf = ""
    while time.time() - t0 < 10:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
        if "SD2-OS Shell" in buf:
            print("ready!")
            break
        time.sleep(0.1)
    else:
        print("timeout")
        ser.close()
        return False

    def cmd(c):
        ser.write((c + "\n").encode())
        ser.flush()
        out = ""
        t0 = time.time()
        while time.time() - t0 < 3:
            if ser.in_waiting:
                out += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                if "OK" in out or "ERR" in out:
                    return out
            time.sleep(0.02)
        return out

    # Start pet upload session
    r = cmd(f"PET_UPLOAD {slug} {len(frames)}")
    if "OK" not in r:
        print(f"ERROR starting upload: {r.strip()}")
        ser.close()
        return False

    for state_name, state_frames in frames.items():
        r = cmd(f"PET_STATE {state_name} {len(state_frames)}")
        if "OK" not in r:
            print(f"ERROR state {state_name}: [{r.strip()[:80]}]")
            ser.close()
            return False
        sys.stdout.write(f"Uploading {state_name} ({len(state_frames)} frames...")

        for i, jpeg_data in enumerate(state_frames):
            size = len(jpeg_data)
            ser.write(f"PET_FRAME {i} {size}\n".encode())
            ser.flush()
            # Wait for READY
            t0 = time.time(); ready = False
            while time.time() - t0 < 5:
                if ser.in_waiting:
                    r = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    if "READY" in r: ready = True; break
                time.sleep(0.02)
            if not ready:
                print(f"Frame {i} ERROR: no READY"); ser.close(); return False
            # Send JPEG data
            ser.write(jpeg_data); ser.flush()
            time.sleep(0.3)
            # Wait for OK
            t0 = time.time(); ok = False; ack = ""
            while time.time() - t0 < 3:
                if ser.in_waiting:
                    ack += ser.read(ser.in_waiting).decode("utf-8", errors="replace")
                    if "OK PET_FRAME" in ack: ok = True; break
                time.sleep(0.02)
            if not ok:
                print(f"Frame {i}/{len(state_frames)} sz={size} ERROR: [{ack.strip()[:100]}]"); ser.close(); return False

        print("OK")

    # Finalize
    r = cmd("PET_SAVE")
    print(f"Save: {r.strip()}")

    # Select this pet
    r = cmd(f"PET_SELECT {slug}")
    print(f"Select: {r.strip()}")

    ser.close()
    return True


def main():
    parser = argparse.ArgumentParser(description="Upload Codex Pets to SD2")
    parser.add_argument("pet_dir", help="Path to pet directory (with pet.json + spritesheet)")
    parser.add_argument("--port", default="COM4", help="Serial port")
    parser.add_argument("--scale", type=int, default=100, help="Max frame dimension in pixels")
    parser.add_argument("--quality", type=int, default=75, help="JPEG quality (1-100)")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    pet_dir = Path(args.pet_dir)
    if not pet_dir.is_dir():
        print(f"ERROR: {pet_dir} is not a directory")
        sys.exit(1)

    print(f"Processing pet from: {pet_dir}")
    name, slug, frames = slice_and_convert(pet_dir, args.scale, args.quality)

    print(f"\nUploading '{name}' ({slug}) to {args.port}...")
    ok = upload_via_serial(args.port, name, slug, frames, args.baud)

    if ok:
        print(f"\n✅ Pet '{name}' uploaded! Use PET_SHOW to display on SD2.")
    else:
        print("\n❌ Upload failed.")
        sys.exit(1)


if __name__ == "__main__":
    main()
