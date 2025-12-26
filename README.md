# ESP32 Mechanical Arm - Action Recorder (Teach Pendant)

一个基于 ESP32 + PCA9685 的机械臂示教编程项目：
- 网页端滑块实时控制 6 轴舵机
- 网页端录制关键帧（pose + hold）
- 将动作保存到 ESP32 的 LittleFS
- 动作库加载 / 删除 / 执行（ESP32 端非阻塞播放）
- WebSocket 通信，手机/电脑均可操作

> 适配机械臂：KM1 松甲（6 轴舵机机械臂）

## 重要：首次装配与归中（强烈建议先做）
为了避免装上后“变形/顶死/啸叫”，建议按下面流程装配：

1. **先给所有舵机归中**：让每个舵机转到 **90°**（中位）。
2. 在舵机都处于 90° 的情况下，把机械臂整体装成 **竖直、自然不受力** 的姿态再固定舵盘/连杆。
3. 安装并上传代码后，如果你发现机械臂初始姿态有轻微偏斜或“变形”，可以在代码里调整每个关节的 **初始位置/offset** 来修正（这是正常的标定步骤）。

> 提示：如果某轴方向反了或一上电就顶死，优先检查：舵机安装角度、dir 方向、限位、offset。

## 硬件
- ESP32（Arduino-ESP32）
- PCA9685 16 路舵机驱动板（I2C）
- 6 个舵机（S1~S6）
- 外接舵机电源（建议 5~6V 大电流），ESP32 与舵机电源共地

## 接线
- ESP32 SDA -> GPIO21
- ESP32 SCL -> GPIO22
- PCA9685 VCC -> 3.3V（或按模块要求）
- PCA9685 GND -> ESP32 GND（必须共地）
- 舵机电源单独供电（不要用 ESP32 供电）

## 使用方式
1. 烧录程序到 ESP32
2. 上电后连接 WiFi 热点：`ESP32_ARM` 密码：`12345678`
3. 浏览器打开：`http://192.168.4.1`
4. 用滑块控制舵机，录制关键帧并保存到动作库

## 软件依赖
- Adafruit PWM Servo Driver Library
- WebSockets
- ArduinoJson
- LittleFS（Arduino-ESP32 自带）

## 目录结构
- `Mechanical_arm.ino` 主程序
- `web_ui.h` 网页 UI（嵌入在固件里）

## 许可协议
MIT License
