from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse
import json


ROOT = Path(__file__).resolve().parent
PAGES = ROOT / "pages"

DEVICE_INFO = {
    "id": 928998,
    "v": "3.0.3",
    "sd": "CMCC-ZbdA",
    "sp": 0,
    "st": 1,
    "hc": 64928,
    "mc": 2047,
    "sc": 64928,
    "oc": 65535,
    "bt": 50,
    "pe": False,
    "pb": 50,
    "pbs": 19,
    "pbe": 6,
    "ro": 0,
    "cc": "101220101",
    "cn": "合肥",
    "le": False,
    "ls": 1,
    "lb": 100,
    "lc": 255,
    "tz": 8,
    "NTP": "ntp6.aliyun.com",
    "xc": 0,
    "xb": 0,
    "ai": False,
    "ae1": 0,
    "at1": 1,
    "ah1": 20,
    "am1": 8,
    "ae2": 0,
    "at2": 1,
    "ah2": 20,
    "am2": 8,
    "ae3": 0,
    "at3": 1,
    "ah3": 20,
    "am3": 8,
    "pen": 31,
    "ten": 2043,
}

PAGE_MAP = {
    "/": "page_17_3aa000.html",
    "/index.html": "page_17_3aa000.html",
    "/setalarmclock.html": "page_01_20a000.html",
    "/setbrightness.html": "page_02_20e000.html",
    "/setcity.html": "page_03_210000.html",
    "/setcolor.html": "page_04_212000.html",
    "/setgif.html": "page_05_214000.html",
    "/setimage.html": "page_06_218000.html",
    "/setledlight.html": "page_07_21a000.html",
    "/setpage.html": "page_08_21c000.html",
    "/setscreenrotation.html": "page_09_21e000.html",
    "/setstock.html": "page_10_220000.html",
    "/settimestyle.html": "page_11_222000.html",
    "/settimezone.html": "page_12_224000.html",
    "/setwifi.html": "page_13_226000.html",
    "/setxscreen.html": "page_14_228000.html",
    "/getmore.html": "page_15_2ce000.html",
    "/help.html": "page_16_2d0000.html",
}


class FirmwareHandler(BaseHTTPRequestHandler):
    def send_bytes(self, data: bytes, content_type: str, status: int = 200) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, value, status: int = 200) -> None:
        data = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_bytes(data, "application/json; charset=utf-8", status)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path

        if path in PAGE_MAP:
            data = (PAGES / PAGE_MAP[path]).read_bytes()
            self.send_bytes(data, "text/html; charset=utf-8")
            return

        if path == "/GetDeviceInfo":
            self.send_json(DEVICE_INFO)
            return

        if path == "/GetStatus":
            self.send_json({"used": "1646592", "total": "2072576"})
            return

        if path == "/GetImageList":
            self.send_json([{"name": "1.jpg", "size": 7602}])
            return

        if path == "/GetGifList":
            self.send_json({})
            return

        if path == "/GetWiFiList":
            self.send_json({"ssidList": []})
            return

        if path.startswith("/Set"):
            print(f"write-like request captured: {self.path}")
            self.send_bytes(b"OK", "text/plain; charset=utf-8")
            return

        self.send_bytes(b"404 Not Found", "text/plain; charset=utf-8", 404)

    def log_message(self, fmt: str, *args) -> None:
        print(f"{self.address_string()} - {fmt % args}")


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 8086), FirmwareHandler)
    print("Firmware HTTP emulator listening on http://127.0.0.1:8086")
    server.serve_forever()
