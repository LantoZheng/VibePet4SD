"""
Upload images to SD2 ESP8266 via serial (COM port) or WiFi HTTP.

Supports:
  - JPEG (.jpg/.jpeg): uploaded as-is, displayed by TJpg_Decoder
  - PNG/BMP/GIF: auto-converted to 240x240 JPEG
  - Raw RGB565 (.raw): uploaded as-is

Usage:
  python upload_serial_image.py image.png --wifi 192.168.4.1
  python upload_serial_image.py --port COM4 photo.jpg
"""

import argparse
import os
import struct
import sys
import time

import serial

WIDTH = 240
HEIGHT = 240
RAW_SIZE = WIDTH * HEIGHT * 2  # 115200


def rgb565(r, g, b):
    value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return struct.pack(">H", value)


def make_test_image():
    data = bytearray()
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if 34 <= x <= 206 and 48 <= y <= 188:
                r = 30 + (x * 160 // WIDTH)
                g = 80 + (y * 120 // HEIGHT)
                b = 220 - (x * 90 // WIDTH)
            else:
                r = x * 80 // WIDTH
                g = y * 80 // HEIGHT
                b = 40

            dx = x - 120
            dy = y - 112
            if dx * dx + dy * dy < 42 * 42:
                r, g, b = 255, 208, 40
            if 82 <= x <= 158 and 170 <= y <= 182:
                r, g, b = 245, 245, 245
            if 92 <= x <= 104 and 100 <= y <= 112:
                r, g, b = 10, 10, 10
            if 136 <= x <= 148 and 100 <= y <= 112:
                r, g, b = 10, 10, 10

            data.extend(rgb565(r, g, b))
    return bytes(data)


def convert_to_jpeg(input_path):
    """Convert any image to 240x240 JPEG. Returns bytes."""
    try:
        from PIL import Image
    except ImportError:
        print("ERROR: pip install Pillow")
        sys.exit(1)
    import io
    img = Image.open(input_path).convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=95, subsampling=0)  # 4:4:4, clean edges
    return buf.getvalue()


def upload_wifi(host, filename, data):
    """Upload via HTTP."""
    import requests
    url = f"http://{host}/upload"
    r = requests.post(url, files={"file": (filename, data, "application/octet-stream")}, timeout=30)
    print(f"HTTP: {r.status_code} {r.text}")
    if r.status_code == 200:
        r2 = requests.get(f"http://{host}/show_file?name={filename}", timeout=10)
        print(f"Show: {r2.status_code} {r2.text}")
        return True
    return False


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
            time.sleep(0.02)
    return buf.decode("utf-8", errors="replace")


def send_command(ser, command, marker=None, timeout=5):
    ser.write((command + "\n").encode("utf-8"))
    ser.flush()
    if marker:
        return read_until(ser, marker, timeout)
    time.sleep(0.2)
    return ser.read(4096).decode("utf-8", errors="replace")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", nargs="?", help="Image file (.jpg/.png/...). Omit for test pattern.")
    parser.add_argument("--port", default="COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--name", default=None)
    parser.add_argument("--show", action="store_true", default=True)
    parser.add_argument("--wifi", default=None, help="WiFi host for faster HTTP upload")
    args = parser.parse_args()

    if args.image:
        ext = os.path.splitext(args.image)[1].lower()
        if ext in (".jpg", ".jpeg"):
            with open(args.image, "rb") as f:
                data = f.read()
            target = args.name or "/" + os.path.basename(args.image)
            print(f"JPEG: {len(data)} bytes ({RAW_SIZE//max(len(data),1)}x vs raw)")
        elif ext == ".raw":
            with open(args.image, "rb") as f:
                data = f.read()
            target = args.name or "/" + os.path.basename(args.image)
            print(f"Raw: {len(data)} bytes")
        else:
            print(f"Converting {args.image} -> JPEG...")
            data = convert_to_jpeg(args.image)
            base = os.path.splitext(os.path.basename(args.image))[0]
            target = args.name or "/" + base + ".jpg"
            print(f"JPEG: {len(data)} bytes ({RAW_SIZE//max(len(data),1)}x vs raw)")
    else:
        print("Generating test pattern...")
        data = make_test_image()
        target = args.name or "/test.raw"
        print(f"Raw: {len(data)} bytes")

    if args.wifi:
        ok = upload_wifi(args.wifi, target, data)
    else:
        ok = upload_serial(args.port, args.baud, target, data, args.show)

    print("OK" if ok else "FAILED")
    sys.exit(0 if ok else 1)
