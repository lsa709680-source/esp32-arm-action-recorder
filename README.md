# esp32-arm-action-recorder
# ESP32 Arm - Action Recorder (Web Teach)

基于 ESP32 + PCA9685 的机械臂网页示教与动作录制系统：滑条实时控制、关键帧录制、保存到 LittleFS、动作库播放（ESP32 端非阻塞）。

## Features
- Web UI 实时控制 6 轴舵机
- 关键帧录制/撤销/保存
- 动作库：列表/执行/加载/删除
- ESP32 端非阻塞播放 + STOP

## Hardware
- ESP32（Arduino-ESP32 core 3.3.3）
- PCA9685 (I2C, 0x40)
- 6x Servo + 独立舵机电源（务必共地）

## Wiring
- ESP32 SDA=21, SCL=22 → PCA9685 SDA/SCL
- GND 必须共地
- 舵机电源不要用 ESP32 5V 直接带（电流不够）

## Build
- Arduino IDE 2.x
- Libraries:
  - Adafruit PWM Servo Driver Library
  - WebSockets
  - ArduinoJson
  - LittleFS (ESP32 core 自带)

## Usage
1. 烧录后 ESP32 会开热点 `ESP32_ARM`（密码在代码里可改）
2. 手机连热点后打开 `http://192.168.4.1`
3. WebSocket 连接成功即可控制与录制
