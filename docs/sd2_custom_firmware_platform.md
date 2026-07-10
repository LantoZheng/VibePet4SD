# ESP8266 SD2 Small TV Custom Firmware Notes

本文档整理当前这块 ESP8266 小电视硬件平台在重新编写/刷入自定义固件前需要掌握的信息。结论分为三类：

- 已实测：来自串口、esptool、现有固件 HTTP 接口、完整 flash 备份。
- 开源参考：来自本地保存的 SD2AIO 开源资料和电路图。
- 推断/待验证：需要用测试固件或万用表/逻辑分析仪进一步确认。

## 1. 本机保存的关键材料

- 完整 flash 备份：`C:\Users\Lanto\Documents\VibeSider\esp8266_COM4_flash_4MB_20260707_182918.bin`
- 备份 SHA256：`AE77E73C946AA33F839CFA63AEDD918D0A602ABEE2B04977D4932DEE4E4698FC`
- 固件资源/HTTP 行为提取：`C:\Users\Lanto\Documents\VibeSider\firmware_emulation`
- SD2AIO 开源参考：`C:\Users\Lanto\Documents\VibeSider\sd2aio_reference`
- SD2AIO 参考源码重点：
  - `sd2aio_reference\unzipped\v1_11\V1.11\SmallDesktopDisplay\SmallDesktopDisplay.ino`
  - `sd2aio_reference\unzipped\v1_11\V1.11\libraries\TFT_eSPI\User_Setup.h`
  - `sd2aio_reference\schematic_text.txt`
  - `sd2aio_reference\开源硬件\SD2闹钟款带触摸感应按钮电路图.pdf`

## 2. 板级识别结论

| 项目 | 结论 | 证据 |
| --- | --- | --- |
| MCU | ESP8266EX | esptool 识别 |
| 模组 | 高概率 ESP-12F，供应商描述为 ESP-12E/F | SD2 原理图标注 `ESP-12F(ESP8266MOD)`；串口无法区分 E/F |
| USB 串口 | CH340/CH340C | Windows 串口枚举为 USB-SERIAL CH340；SD2 原理图标注 CH340C |
| 串口 | COM4，115200 8N1 | 已读取启动日志 |
| Flash | 4MB SPI flash | esptool `flash-id` 检测为 4MB |
| Flash 模式 | DIO, 40 MHz | 镜像头/flash-info |
| 晶振 | 26 MHz | esptool 输出 |
| Wi-Fi MAC | `d8:bf:c0:0e:2c:e6` | esptool 输出 |
| 供电 | USB-C 输入，AMS1117-3.3 稳压 | SD2 原理图 |

注意：ESP-12E 和 ESP-12F 的软件可见差异很少，单靠 esptool 只能确认芯片是 ESP8266EX，不能严格确认模组外壳版本。结合供应商描述和 SD2 原理图，目前把它当 ESP-12F/ESP-12E 兼容开发板处理即可。

## 3. 可驱动设备清单

| 设备 | 引脚/接口 | 开发建议 | 备注 |
| --- | --- | --- | --- |
| TFT 屏幕 | SPI: SCK GPIO14/D5, MOSI GPIO13/D7, CS GPIO15/D8, DC GPIO0/D3, RST GPIO2/D4 | TFT_eSPI | ST7789 240x240，参考配置为 `ST7789_2_DRIVER` |
| 屏幕背光 | GPIO5/D1 | PWM 调光 | 参考代码中为反相调光：亮度越高，PWM 写入越低 |
| 触摸/按键 | GPIO4/D2 | `INPUT_PULLUP`，低电平有效 | 原理图含 TTP233H-HA6；现有固件帮助页也写 D2/GPIO4 低有效 |
| WS2812 幻彩灯 | GPIO12/D6 | FastLED 或 Adafruit_NeoPixel | D6 是 ESP8266 HSPI MISO；屏幕通常不读 MISO，所以可复用 |
| 蜂鸣器 | GPIO16/D0 | 简单开关/软件 tone | GPIO16 不支持普通外部中断，PWM/定时能力也要谨慎 |
| Wi-Fi | ESP8266 内置 | ESP8266WiFi | 可做 STA/AP 配网、HTTP API、NTP |
| Flash 文件系统 | 4MB flash 后半区 | LittleFS | 现有固件 `/GetStatus` 显示约 2MB 文件系统空间 |

## 4. TFT_eSPI 参考配置

SD2AIO 参考的 `User_Setup.h` 核心配置如下，建议在新工程里单独放一个专用 setup 文件，避免以后库升级覆盖：

```cpp
#define ST7789_2_DRIVER
#define TFT_RGB_ORDER TFT_RGB

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_CS   PIN_D8  // GPIO15
#define TFT_DC   PIN_D3  // GPIO0
#define TFT_RST  PIN_D4  // GPIO2
#define TFT_BL   PIN_D1  // GPIO5, optional if you manage PWM yourself

// ESP8266 hardware SPI default:
// SCLK = GPIO14 / D5
// MOSI = GPIO13 / D7
// MISO = GPIO12 / D6, probably unused by this display

#define SPI_FREQUENCY  27000000
```

如果屏幕颜色红蓝互换，优先尝试把 `TFT_RGB_ORDER` 改成 `TFT_BGR`。如果画面偏移、旋转或边缘异常，再检查 ST7789 初始化变体和 `setRotation()`。

## 5. 背光逻辑

SD2AIO 参考源码中有两种写法：

```cpp
analogWrite(LCD_BL_PIN, 255 - (LCD_BL_PWM * 255 / 100));
```

以及新版 ESP8266 PWM 范围风格：

```cpp
analogWrite(LCD_BL_PIN, 1023 - (LCD_BL_PWM * 10));
```

这说明背光大概率是低电平更亮/反相控制。自定义固件建议先做一个 0/25/50/75/100 亮度测试页面，确认不会出现亮度反向或黑屏。

## 6. Flash 分区和文件系统

已读取到的整片 flash 为 4MB。当前商业固件使用类似：

- 应用固件区域：低地址段。
- 文件系统/资源区：约从 `0x200000` 开始。
- 文件系统空间：现有 `/GetStatus` 返回 total 约 `2072576` 字节，接近 2MB。

对自定义固件，建议第一版使用：

- 4MB flash。
- Arduino/PlatformIO + LittleFS。
- 约 2MB 文件系统，用于图片、字体、Web 配置页面、缓存数据。
- 暂时不要追求 OTA，先把串口刷写和屏幕/按键/背光验证稳定。

重要提醒：SPIFFS 和 LittleFS 的片上格式不兼容。切换文件系统或分区布局时，旧文件系统内容会丢失或无法挂载；在刷写前保留当前整片 flash 备份。

## 7. 推荐开发栈

首选：VS Code + PlatformIO + Arduino framework for ESP8266。

理由：

- SD2AIO 开源版本也是 PlatformIO/Arduino 生态。
- TFT_eSPI、FastLED/TJpg_Decoder 等库都可直接使用。
- ESP8266 Arduino core 对 Wi-Fi、HTTP server、NTP、LittleFS、OTA 有成熟支持。

`platformio.ini` 起步模板：

```ini
[env:sd2-small-tv]
platform = espressif8266
board = esp12e
framework = arduino
monitor_speed = 115200
upload_speed = 460800

board_build.flash_size = 4MB
board_build.filesystem = littlefs

lib_deps =
  bodmer/TFT_eSPI
  fastled/FastLED
  bodmer/TJpg_Decoder

build_flags =
  -D USER_SETUP_LOADED
  -D ST7789_2_DRIVER
  -D TFT_RGB_ORDER=TFT_RGB
  -D TFT_WIDTH=240
  -D TFT_HEIGHT=240
  -D TFT_CS=15
  -D TFT_DC=0
  -D TFT_RST=2
  -D TFT_BL=5
  -D SPI_FREQUENCY=27000000
```

说明：TFT_eSPI 的宏是否都能通过 `build_flags` 生效，取决于库版本和 include 顺序。更稳的做法是在工程内提供一个固定的 TFT setup 文件，然后让 `User_Setup_Select.h` include 它。

## 8. 引脚启动约束

ESP8266 的部分 GPIO 是启动绑定位，写固件时必须尊重：

| GPIO | 板上用途 | 启动要求 | 风险 |
| --- | --- | --- | --- |
| GPIO0 / D3 | TFT_DC | 正常启动时必须为高；拉低进入刷机模式 | 外接电路不能在启动时强拉低 |
| GPIO2 / D4 | TFT_RST | 正常启动时必须为高 | 屏幕复位电路不能拖低过久 |
| GPIO15 / D8 | TFT_CS | 正常启动时必须为低 | 片选线路正好适合此要求 |
| GPIO16 / D0 | 蜂鸣器 | 非启动绑定位 | 深睡唤醒专用脚，功能有限 |
| GPIO12 / D6 | WS2812 | 普通 GPIO/HSPI MISO | 若以后接 SPI 读设备会冲突 |

## 9. 第一版测试固件建议

不要一开始就复刻完整小电视 UI。建议按这个顺序做硬件验证：

1. 串口启动日志：输出芯片 ID、flash size、LittleFS mount 结果。
2. 屏幕点亮：纯色红/绿/蓝/白/黑循环，确认驱动、颜色顺序、旋转。
3. 背光调光：0/20/40/60/80/100 梯度，确认反相。
4. 按键/触摸：串口打印 GPIO4 状态，屏幕显示按下次数。
5. WS2812：GPIO12 输出单颗或灯带测试色。
6. 蜂鸣器：GPIO16 短促提示音或开关测试。
7. LittleFS：读取 `/config.json`，显示一张 LittleFS 中的 JPG。
8. Wi-Fi/AP 配网：先实现本地 AP + Web 设置页，再加 STA/NTP/天气。

这样一旦某个设备不工作，可以把问题定位在单一外设，而不是 UI、网络和驱动混在一起。

## 10. 刷写/备份命令参考

读取芯片信息：

```powershell
python -m esptool --chip esp8266 --port COM4 --baud 115200 chip-id
python -m esptool --chip esp8266 --port COM4 --baud 115200 flash-id
```

整片备份：

```powershell
python -m esptool --chip esp8266 --port COM4 --baud 115200 read-flash 0x000000 0x400000 esp8266_backup.bin
```

刷写 PlatformIO 编译出的固件：

```powershell
pio run -t upload
```

上传 LittleFS 数据区：

```powershell
pio run -t uploadfs
```

如果 esptool 提示 COM4 busy/permission denied，先关闭串口监视器、Web 串口工具或任何正在占用 COM4 的程序。

## 11. 和现有商业固件的关系

现有商业固件不是完整开源项目，但已经提供了有价值的行为参考：

- 固件版本：`3.0.3`
- HTTP 状态接口：`/GetDeviceInfo`
- 文件状态接口：`/GetStatus`
- 图片/GIF 列表接口：`/GetImageList`、`/GetGifList`
- 设备功能字段：亮度、城市、NTP、时区、倒计时/番茄钟/闹钟、LED/蜂鸣器等状态。

如果你想兼容原来的 Web 管理页面或手机端控制逻辑，可以保留类似接口；如果只是做自己的系统，可以只借鉴设备模型和资源组织方式。

## 12. 需要进一步验证的点

- 屏幕玻璃/排线上的精确型号：当前按 ST7789 240x240 开发是可靠起点，但最好通过测试固件确认颜色顺序、偏移和旋转。
- WS2812 是否实际焊接在板上，还是只是预留扩展位。
- 蜂鸣器是有源还是无源；GPIO16 是否通过三极管驱动。
- 触摸按键的空闲/按下电平是否与现有帮助页完全一致。
- 当前商业固件文件系统是否严格 LittleFS；从资源区特征看很像，但自定义固件不需要依赖旧格式。

## 13. 外部资料

- PlatformIO ESP8266 平台文档：https://docs.platformio.org/en/latest/platforms/espressif8266.html
- ESP8266 Arduino core 文件系统文档：https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html
- ESP8266 Arduino core flash layout 说明：https://arduino.esp8266.com/Arduino/versions/2.2.0/doc/filesystem.html
- TFT_eSPI 项目：https://github.com/Bodmer/TFT_eSPI
- esptool ESP8266 基础命令：https://docs.espressif.com/projects/esptool/en/latest/esp8266/esptool/basic-commands.html
- SD2AIO Gitee 参考仓库：https://gitee.com/wfm123456/SD2AIO

