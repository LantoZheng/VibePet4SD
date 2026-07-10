# VibePet4SD 🖥️🐾

> 致敬 SD2 小电视 —— ESP8266 智能桌面伴侣，AI 信号灯 + 动画宠物 + Copilot 实时同步

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP8266-blue.svg)](https://www.espressif.com/en/products/socs/esp8266)

---

## ✨ 功能亮点

- 🟢🔵🟣 **实时信号灯同步** — WS2812 LED + TFT 24px 渐变呼吸边框，自动追踪 VS Code Copilot 工作状态
- 🐾 **Codex Pets 动画宠物** — 内置 Blob + JPEG 帧序列宠物系统，51 帧 claw-d 宠物
- 🌐 **Web 控制面板** — 手机/电脑浏览器直接控制所有功能
- 💻 **50+ Shell 命令** — 串口 / TCP / WebSocket / HTTP REST 四通道控制
- 🧠 **LLM 桥接** — 可选接入 OpenAI / Claude，小电视直接对话 AI
- 📜 **脚本引擎** — 自定义 `.sh` → `.s2b` 字节码，支持循环、多进程、信号

---

## 📁 项目结构

```
VibeSider/
├── simple_host_firmware/          # 🔧 主固件
│   ├── src/                       #    15 个模块化头文件 + main.cpp
│   ├── data/panel.html            #    Web 控制面板 SPA
│   ├── platformio.ini             #    编译配置
│   └── tools/
│       ├── sd2_signal_hook.py     #    Copilot 实时状态监控
│       ├── upload_gif_pet.py      #    GIF → JPEG 宠物上传
│       └── upload_serial_image.py #    串口图片上传
├── selftest_firmware/             # 🧪 硬件自检固件
├── claw-d-gifs/                   # 🐾 示例宠物素材
├── firmware_emulation/            # 🖥️ 离线模拟器
├── docs/                          # 📚 设计文档
└── sd2aio_reference/              # 📖 原版 SD2 参考
```

---

## 🔧 硬件平台

| 组件 | 型号 |
|------|------|
| MCU | ESP8266EX @ 80MHz |
| RAM | 80KB |
| Flash | 4MB (LittleFS @ 2MB offset) |
| 屏幕 | ST7789 240×240 TFT (SPI) |
| LED | WS2812 NeoPixel ×1 (GPIO12) |
| 按钮 | 触摸/按键 (GPIO4, 中断) |
| 蜂鸣 | 无源蜂鸣器 (GPIO16) |

### 引脚映射

| 功能 | GPIO | 说明 |
|------|------|------|
| TFT_CS | 15 | SPI 片选 |
| TFT_DC | 0 | 数据/命令 |
| TFT_RST | 2 | 复位 |
| TFT_BL | 5 | 背光 PWM (反相) |
| WS2812 | 12 | NeoPixel 数据线 |
| Button | 4 | 按键 (低电平有效) |
| Buzzer | 16 | 无源蜂鸣器 |

---

## 🚀 快速开始

### 编译刷入

```powershell
cd simple_host_firmware
pio run -t upload          # 刷入固件
pio run -t uploadfs        # 上传 Web 面板
```

首次上电：
- WiFi AP: `SD2-TV-XXXXXX`，IP: `192.168.4.1`
- Web 面板: `http://192.168.4.1/panel`
- TCP Shell: `telnet 192.168.4.1 2323` (密码 `sd2`)

### Copilot 监控

```powershell
$env:SD2_PORT="COM4"
python tools/sd2_signal_hook.py --watch
```

---

## 💡 信号灯模式

| 模式 | 边框灯效 | WS2812 | 蜂鸣 |
|------|---------|--------|------|
| `idle` | 🟢 绿色常亮 | 暗绿 | - |
| `working`/`executing` | 🔵 蓝色呼吸 (3s) | 蓝呼吸 | - |
| `thinking` | 🟣 紫色慢脉冲 (5s) | 紫脉冲 | - |
| `attention` | 🟠 黄色闪烁 (500ms) | 黄闪 | 短促提示 |
| `blocked` | 🔴 红色闪烁 (300ms) | 红闪 | 低沉嗡鸣 |

---

## ⌨️ 命令速览

```text
PING  STATUS  SHOW logo|face|bars|status
TEXT <msg>  BRIGHT <0-100>  BEEP [hz] [ms]
SIGNAL idle|working|thinking|attention|blocked
ANIM_LIST/PLAY/STOP/INFO
SCRIPT_COMPILE/RUN  MODULE_LOAD/UNLOAD
PET_LIST/SELECT/SHOW/HIDE/DELETE
WIFI <ssid> <pass> / WIFI? / WIFI_CLEAR
OTA <url>  DNS <host>  FS  MEM  TIME  GPIO
SET key=value / GET key / ENV / UNSET
LLM_START/ASK/LAST/STOP
SPAWN PS KILL KILLALL TIMER CRON HOOK
NET_START/STOP  REBOOT  HELP
```

管道: `STATUS | PARSE heap` 从 JSON 提取字段

---

## 🐾 自定义宠物

```powershell
# 准备 GIF 目录：idle.gif, waving.gif, running.gif, failed.gif
python tools/upload_gif_pet.py my_pet/ --port COM4 --scale 170
```

---

## 🙏 致敬

本项目基于 **SD2 小电视** 开源硬件平台开发。

感谢 [星光微电子工作室](https://github.com) 开源的 SD2 硬件设计（原理图、PCB、外壳），
以及 SD2 小电视 AIO 版本固件提供的参考实现。

> SD2 小电视采用 ESP8266 作为主控，支持 WEB 配网、网络连接显示、天气时钟等功能。
> 商业版本固件可在淘宝店「星光微电子工作室」购买成品。

---

## 🤝 贡献

欢迎 Issue / PR！MIT License.

---

*Made with ❤️ for the open-source hardware community.*
