# SD2 ESP8266 Small TV Self-Test Firmware

This firmware verifies the known hardware on the ESP8266/ESP-12E/F SD2-style small TV board:

- ST7789 240x240 SPI display
- PWM backlight on GPIO5/D1
- touch/button input on GPIO4/D2, active low
- WS2812/NeoPixel data on GPIO12/D6
- buzzer on GPIO16/D0
- LittleFS mount and serial diagnostics

Build and upload:

```powershell
C:\Users\Lanto\AppData\Roaming\Python\Python314\Scripts\pio.exe run -d selftest_firmware -t upload
```

Serial monitor:

```powershell
C:\Users\Lanto\AppData\Roaming\Python\Python314\Scripts\pio.exe device monitor -d selftest_firmware -p COM4 -b 115200
```

