2. Indoor Air Quality Monitor（室内空气质量监测仪） ★★☆

功能概述：监测 CO₂、VOC、温湿度，结合指示灯或屏幕显示空气状况。
核心技术：
	•	输入：CCS811/SGP30 气体传感器（I²C）+ DHT22（GPIO）
	•	输出：LED 显示空气质量等级 + LCD 显示具体数值 + Buzzer Alarm
	•	涉及主题：ADC、I²C 通信、电源管理、数据滤波
扩展方向：ESP32 连接云端，记录长时间趋势。

5. Voice-Controlled Desk Lamp（语音控制台灯） ★★★

功能概述：通过语音识别模块控制亮度与颜色，支持自动环境光调节。
核心技术：
	•	输入：ASR-33/Elechouse 语音识别模块 + 光敏传感器
	•	输出：PWM 驱动 LED
	•	涉及主题：PWM 调光、串行通信 (UART/I²C)、电源设计
扩展方向：Wi-Fi 远程控制 + 自动睡眠模式。

https://www.elechouse.com/product/speak-recognition-voice-recognition-module-v3/?utm_source=chatgpt.com


2. Indoor Air Quality Monitor


Function Overview:
This device continuously monitors CO₂, volatile organic compounds (VOCs), temperature, and humidity in indoor environments. Based on the readings, it displays air quality levels using LEDs or an LCD screen, and activates a buzzer alarm if poor air quality is detected.


Core Technologies:

	•	Inputs:
	•	CCS811/SGP30 gas sensor (I²C): measures CO₂ and VOC concentrations.
	•	DHT22 (GPIO): measures ambient temperature and humidity.
	•	Outputs:
	•	LED indicator: shows air quality status (e.g., green = good, red = poor).
	•	LCD display: presents precise CO₂, VOC, temperature, and humidity values.
	•	Buzzer alarm: warns when air quality exceeds thresholds.
	•	Key Topics: ADC usage, I²C communication, power management, data filtering.



5. Voice-Controlled Desk Lamp (★★★)

Function Overview:
A smart desk lamp that can adjust brightness and color through voice commands. It also automatically adapts lighting based on ambient light levels for optimal comfort.

Core Technologies:
	•	Inputs:
	•	ASR-33/Elechouse voice recognition module: processes spoken commands for light control.
	•	Light sensor: detects ambient brightness to adjust lamp intensity automatically.
	•	Outputs:
	•	PWM-controlled LED: allows smooth dimming and color changes.
	•	Key Topics: PWM dimming, serial communication (UART/I²C), and power circuit design.



- SGP30 与 DHT20 共享同一 I²C 总线，地址分别为 0x58 与 0x38。  
- MCU 周期性读取环境数据，并将湿度值用于 SGP30 湿度补偿。  
- OLED 通过 SPI 与 SN74LVC125 电平转换器连接，以 3.3 V 驱动显示。  
- 系统根据阈值控制 RGB LED 和蜂鸣器，提示空气质量状态。

---

## 🔋 电气与通信说明

| 模块 | 工作电压 | 说明 |
|------|------------|------|
| ATmega328PB | 5 V | 主控电压 |
| SGP30 模块 | 3.3–5 V（内部含 1.8 V LDO 与 I²C 电平转换） | 与 MCU 共用 I²C 总线 |
| DHT20 模块 | 2.0–5.5 V | 与 SGP30 共线，无需电平转换 |
| OLED (AOM12864A0-0.96WW-ANO) | **3.3 V 专用供电（板载 3.3 V LDO）** | SPI 信号经 SN74LVC125 转换至 3.3 V 域 |

---

## 🧠 软件与算法结构

1. **裸机 C 架构**  
   所有外设（I²C、SPI、Timer、Interrupt、EEPROM）均基于寄存器级驱动，无 Arduino 库。

2. **DHT20 数据读取（I²C）**  
   - I²C 地址：0x38  
   - 命令：`0xAC 0x33 0x00` 启动转换，延时 80 ms 后读取 6 字节数据。  
   - 数据经 CRC 校验后换算温湿度。  

3. **SGP30 数据采集与补偿**  
   - 初始化（命令 0x2003）  
   - 每秒读取（命令 0x2008）  
   - 将 DHT20 湿度换算为绝对湿度后，通过命令 `0x2061` 写入传感器进行补偿  
   - 定期保存基线数据至 EEPROM  

4. **显示与报警逻辑**  
   - OLED 显示 eCO₂、TVOC、温度、湿度  
   - eCO₂ 或 TVOC 超限触发红色 LED 与蜂鸣器  
   - Mute 按键关闭蜂鸣器  

5. **滤波与滞回控制**  
   - 使用指数移动平均（EMA）滤波 (α≈0.25)  
   - 连续超限 N 次后报警，避免抖动。  

---

## 📊 系统特性概览

| 功能模块 | 技术主题 | 是否实现 |
|-----------|-----------|-----------|
| DHT20 I²C 读取 | I²C (TWI0) 驱动 | ✅ |
| SGP30 空气质量检测 | I²C 通信与补偿 | ✅ |
| OLED 显示 | SPI + 电平转换 | ✅ |
| LED/Buzzer 控制 | PWM + GPIO | ✅ |
| EEPROM 数据保存 | NVM 操作 | ✅ |
| 滤波与报警逻辑 | 数字信号处理 | ✅ |
| ADC 功能 | — | 🚫（未使用） |

---

## 🧩 关键特征总结
- 全数字架构（I²C + SPI + GPIO），**无需 ADC**  
- DHT20 与 SGP30 共用 I²C 总线，节省引脚  
- SGP30 实现湿度补偿与基线保存  
- OLED 屏通过 SN74LVC125 实现 5 V→3.3 V 安全驱动  
- PWM 控制蜂鸣器；RGB LED 实时指示空气质量  
- 支持未来扩展 Wi-Fi 云端数据上传  

---

## 📦 输入模块关系

| 模块 | 输出数据 | 提供给谁 | 功能 |
|------|------------|-----------|------|
| **DHT20** | 温度 (°C)、湿度 (%RH) | MCU + SGP30 | 提供环境显示与湿度补偿 |
| **SGP30** | eCO₂ (ppm)、TVOC (ppb) | MCU | 判断空气质量与触发报警 |

### 系统工作流程
1. MCU 通过 I²C 从 DHT20 获取温湿度；  
2. 将湿度换算为绝对湿度写入 SGP30；  
3. MCU 从 SGP30 读取 eCO₂/TVOC；  
4. OLED 显示并控制 LED、蜂鸣器报警。  

---

## 📗 输入模块对比表

| 项目 | SGP30 | DHT20 |
|------|--------|--------|
| 测量类型 | eCO₂、TVOC | 温度、湿度 |
| 通信方式 | I²C (0x58) | I²C (0x38) |
| 接口引脚 | PC4 (SDA), PC5 (SCL) | PC4 (SDA), PC5 (SCL) |
| 是否需 ADC | 否 | 否 |
| 工作电压 | 3.3–5 V（模块内部 1.8V LDO） | 2.0–5.5 V |
| 输出分辨率 | 1 ppm / 1 ppb | 0.01°C / 0.01%RH |
| 采样频率 | 1 Hz | 1 Hz |
| 特殊特性 | 湿度补偿、基线保存 | 校准好、I²C 接口、快速采样 |
| 系统角色 | 主气体检测 | 环境补偿输入 |

---

> **总结**：  
> - 两个传感器均为数字接口（I²C），无需 ADC。  
> - DHT20 替代 DHT22，响应更快，结构更简洁。  
> - MCU 通过 I²C 统一管理 SGP30 与 DHT20，实现实时空气质量监测与报警控制。  