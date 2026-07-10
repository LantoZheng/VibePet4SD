# VibePet4SD Firmware

致敬 SD2 小电视 —— ESP8266 智能桌面显示固件。详见 [项目 README](../README.md)。

## 快速开始

```powershell
pio run -t upload        # 刷入固件
pio run -t uploadfs       # 上传 Web 面板
pio device monitor -p COM4
```

## 模块架构

```
src/
├── main.cpp           # setup() + loop() (~300行)
├── sd2_config.h       # 引脚/常量/外部声明
├── sd2_signal.h       # WS2812 + TFT 信号灯
├── sd2_font.h         # 5×7 RAM 字体
├── sd2_pet.h          # Blob + JPEG 宠物
├── sd2_anim.h         # JPEG 动画引擎
├── sd2_script.h       # 脚本 VM
├── sd2_proc.h         # 多进程调度
├── sd2_module.h       # 模块管理
├── sd2_llm.h          # OpenAI / Claude
├── sd2_shell.h        # 命令调度 + Shell
├── sd2_web.h          # HTTP + WebSocket
├── sd2_network.h      # WiFi / OTA / DNS / NTP
├── sd2_log.h          # 环形日志
├── sd2_env.h          # 环境变量
└── sd2_time.h         # NTP 时间
```

## 编译配置

```ini
[env:sd2-small-tv-host]
platform = espressif8266
board = esp12e
framework = arduino
board_build.flash_size = 4MB
board_build.ldscript = eagle.flash.4m2m.ld
board_build.filesystem = littlefs

lib_deps =
  bodmer/TFT_eSPI@^2.5.43
  links2004/WebSockets@^2.6.1
  bodmer/TJpg_Decoder@^1.1.0
  adafruit/Adafruit NeoPixel@^1.12.3

build_flags =
  -D ST7789_DRIVER -D TFT_RGB_ORDER=TFT_BGR
  -D TFT_WIDTH=240 -D TFT_HEIGHT=240
  -D TFT_CS=15 -D TFT_DC=0 -D TFT_RST=2 -D TFT_BL=5
  -D SPI_FREQUENCY=27000000 -D LOAD_GLCD
```

## Shell 命令参考

### 显示

| 命令                           | 说明               |
| ------------------------------ | ------------------ |
| `SHOW logo\|face\|bars\|status` | 切换内置画面       |
| `SHOW_FILE /image.jpg`       | 显示 LittleFS 图片 |
| `TEXT hello world`           | 显示文字           |
| `BRIGHT 85`                  | 背光 0-100         |
| `BEEP 2200 70`               | 蜂鸣 (Hz, ms)      |

### 信号灯

| 命令                 | 说明      |
| -------------------- | --------- |
| `SIGNAL idle`      | 🟢 空闲   |
| `SIGNAL working`   | 🔵 工作中 |
| `SIGNAL attention` | 🟠 注意   |
| `SIGNAL blocked`   | 🔴 阻塞   |
| `SIGNAL status`    | 查询      |

### 动画 / 宠物

| 命令                                               | 说明     |
| -------------------------------------------------- | -------- |
| `ANIM_LIST` / `ANIM_PLAY name` / `ANIM_STOP` | 动画控制 |
| `PET_LIST` / `PET_SELECT name`                 | 宠物管理 |
| `PET_SHOW` / `PET_HIDE` / `PET_DELETE name`  |          |

### 网络

| 命令                         | 说明      |
| ---------------------------- | --------- |
| `WIFI ssid pass`           | 连接 WiFi |
| `WIFI?` / `WIFI_CLEAR`   | 查看/清除 |
| `NET_START` / `NET_STOP` | 启停网络  |
| `DNS host`                 | DNS 解析  |

### 系统

| 命令                             | 说明      |
| -------------------------------- | --------- |
| `STATUS`                       | JSON 状态 |
| `PING`                         | 心跳      |
| `MEM` / `FS` / `GPIO`      | 系统信息  |
| `TIME` / `DATE` / `TZ ±8` | NTP 时间  |
| `REBOOT`                       | 重启      |
| `OTA http://...`               | 固件升级  |

### 环境变量 (持久化到 /etc/env.cfg)

| 命令              | 说明     |
| ----------------- | -------- |
| `SET KEY=value` | 设置     |
| `GET KEY`       | 读取     |
| `ENV`           | 列出所有 |
| `UNSET KEY`     | 删除     |

### LLM (需先 SET LLM_API_KEY=sk-...)

| 命令                         | 说明     |
| ---------------------------- | -------- |
| `LLM_START` / `LLM_STOP` | 启停     |
| `LLM_ASK 你好`             | 提问     |
| `LLM_LAST`                 | 上次回复 |

### 脚本与进程

| 命令                                | 说明                |
| ----------------------------------- | ------------------- |
| `SCRIPT_COMPILE test.sh`          | 编译脚本            |
| `SCRIPT_RUN test.s2b`             | 运行                |
| `SPAWN test.s2b 2`                | 后台运行 (prio 0-3) |
| `PS` / `KILL pid` / `KILLALL` | 进程管理            |
| `TIMER ms cmd` / `CRON ms cmd`  | 定时任务            |
| `HOOK event cmd`                  | 事件钩子            |

管道: `STATUS | PARSE heap` 从 JSON 提取字段

## WebSocket API

`ws://192.168.4.1:81/` — 发送文本命令（同 Shell），返回 JSON。

## REST API

| 端点                         | 说明       |
| ---------------------------- | ---------- |
| `GET /status`              | 完整状态   |
| `GET /status.json`         | 精简状态   |
| `GET /cmd?c=CMD`           | 执行命令   |
| `GET /signal?mode=working` | 切换信号灯 |
| `GET /panel`               | Web 面板   |

## 自定义脚本 (.sh)

```bash
# example.sh
SHOW logo
WAIT 1000
BEEP 800 100
TEXT SD2 Ready!
WAIT 2000
ANIM_PLAY boot_anim
```

编译: `SCRIPT_COMPILE example.sh` → `example.s2b`
开机自启: 保存为 `/auto.s2b`
