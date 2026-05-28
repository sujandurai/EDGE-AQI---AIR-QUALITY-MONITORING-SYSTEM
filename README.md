# 🌍 EDGE AQI — Air Quality Monitoring System

![Status](https://img.shields.io/badge/Status-Fully%20Completed-brightgreen)
![Platform](https://img.shields.io/badge/Platform-PIC32CM5164LS00048-blue)
![Connectivity](https://img.shields.io/badge/Connectivity-Wi--Fi%20%7C%20MQTT-orange)
![TinyML](https://img.shields.io/badge/AI-TinyML-purple)
![Security](https://img.shields.io/badge/Security-TrustZone-red)

A next-generation **Edge AI powered Air Quality Monitoring System** developed using the powerful **PIC32CM5164LS00048 ARM Cortex®-M23** microcontroller from [Microchip Technology](https://www.microchip.com?utm_source=chatgpt.com).

The system continuously monitors environmental pollution using multiple gas sensors, performs intelligent AQI analysis using TinyML, and uploads live data through MQTT over Wi-Fi for real-time remote monitoring.

---

# 🚀 Project Overview

EDGE AQI is a professional embedded IoT system designed for:

* Real-time pollution monitoring
* Smart AQI analysis
* Edge AI processing
* Secure embedded communication
* Cloud-based monitoring

The project combines:

* Embedded Systems
* TinyML
* IoT Connectivity
* Sensor Fusion
* Secure Firmware Architecture

This system demonstrates advanced embedded engineering concepts using the **PIC32CM LS00 Secure MCU Platform**.

---

# ✨ Unique Features

✅ TinyML Integrated AQI Prediction
✅ Real-Time Multi Gas Monitoring
✅ Custom Designed PCB
✅ Secure Firmware using TrustZone®
✅ MQTT Cloud Connectivity
✅ Real-Time TFT Visualization
✅ Edge AI Processing
✅ High-Speed Embedded Graphics
✅ Industrial Embedded Architecture
✅ Fully Optimized Embedded C Firmware

---

# 🧠 Sensors Used

| Sensor | Purpose                   |
| ------ | ------------------------- |
| MQ-7   | Carbon Monoxide (CO)      |
| MQ-131 | Ozone (O₃) Detection      |
| MQ-135 | NH₃ / Smoke / Air Quality |
| MG-811 | Carbon Dioxide (CO₂)      |
| GP2Y10 | Dust / PM2.5 Monitoring   |
| DHT11  | Temperature & Humidity    |

---

# 🛠️ Hardware Components

| Component          | Description                  |
| ------------------ | ---------------------------- |
| PIC32CM5164LS00048 | ARM Cortex-M23 MCU           |
| ST7735 TFT         | 1.8” SPI TFT Display         |
| ESP-01S            | Wi-Fi Module                 |
| Custom PCB         | Compact Embedded Design      |
| Gas Sensors        | Environmental Monitoring     |
| Power Circuit      | Stable Embedded Power System |

---

# 🔌 System Architecture

### Embedded Processing

The PIC32CM microcontroller handles:

* ADC sensor acquisition
* TFT graphics rendering
* UART communication
* TinyML inference
* MQTT packet handling

### Connectivity

The ESP-01S module acts as:

* Wi-Fi bridge
* MQTT communication interface
* Cloud data uploader

### Display System

The TFT display provides:

* Live AQI values
* Gas concentration visualization
* Environmental status
* TinyML prediction display

---

# 📡 Working Principle

1️⃣ Sensors collect environmental data
2️⃣ PIC32CM processes analog readings
3️⃣ TinyML predicts AQI trends
4️⃣ TFT displays live environmental values
5️⃣ ESP-01S uploads data via MQTT
6️⃣ Cloud dashboard visualizes live monitoring

---

# 🧠 TinyML Integration

The project integrates TinyML for:

* AQI prediction
* Pattern recognition
* Environmental trend analysis
* Smart pollution estimation

TinyML enables:

* Edge AI processing
* Faster local inference
* Reduced cloud dependency
* Low power operation

Research in edge-deployable AQI systems shows TinyML and lightweight CNN models can improve air-quality prediction efficiency on embedded hardware. ([arXiv][1])

---

# 🔐 Security Features

This system utilizes **ARM® TrustZone®** secure architecture.

### Security Benefits

* Secure & Non-Secure firmware isolation
* Protected sensor communication
* Secure memory partitioning
* Safer IoT communication
* Embedded cyber protection

---

# 💻 Development Environment

## Software Used

* MPLAB® X IDE
* MPLAB® Harmony v3
* XC32 Compiler
* MCC (MPLAB Code Configurator)
* Embedded C
* MQTT Protocol
* TinyML Frameworks

---

# ⚡ Advanced Embedded Features

* Direct Register Access
* Optimized ADC Sampling
* Deterministic Delay Handling
* High-Speed SPI Rendering
* Low-Level Driver Development
* Hardware Optimized Embedded Architecture

---

# 📊 Real-Time Monitoring Features

The system supports:

✅ Live AQI Monitoring
✅ Cloud Dashboard Integration
✅ MQTT Data Streaming
✅ Remote IoT Monitoring
✅ Environmental Analytics
✅ Smart Pollution Detection

Community-driven AQI projects commonly combine MQ-series sensors, PM sensors, Wi-Fi modules, and live dashboards for scalable air monitoring systems. ([GitHub][2])

---

# 🖥️ TFT Display Features

The ST7735 TFT display provides:

* Live gas readings
* AQI indicators
* Environmental graphs
* Real-time system status
* Interactive UI screens

---

# 📂 Project Setup

## 1️⃣ Install Required Tools

* MPLAB X IDE
* XC32 Compiler
* Harmony v3 Packages

---

## 2️⃣ Clone Repository

```bash
git clone https://github.com/sujandurai/EDGE-AQI---AIR-QUALITY-MONITORING-SYSTEM.git
```

---

## 3️⃣ Open Project

Open the project in MPLAB X IDE.

---

## 4️⃣ Build & Flash

* Compile the firmware
* Connect PIC32 Curiosity Nano
* Flash the MCU
* Monitor live AQI data

---

# 🌐 Applications

* Smart Cities
* Industrial Pollution Monitoring
* Indoor AQI Systems
* Environmental Research
* IoT Smart Homes
* Health Safety Monitoring
* Smart Campus Monitoring

---

# 🏆 Project Highlights

✅ Edge AI Enabled
✅ IoT Cloud Integrated
✅ Professional Embedded Design
✅ Secure MCU Architecture
✅ Custom PCB Developed
✅ Real-Time AQI Dashboard
✅ Industrial Embedded Workflow

---

# 👨‍💻 Development Team

### Lead Developer

**Sujan D**

### Team Members

* Priya Dharshini S
* Monishwaran S

### Project Guidance

**Daniel Raj**

---

# 🔗 Repository

[EDGE AQI — GitHub Repository](https://github.com/sujandurai/EDGE-AQI---AIR-QUALITY-MONITORING-SYSTEM?utm_source=chatgpt.com)

---

# 📜 License

This project is licensed under the MIT License.

---

# 🏁 Final Note

EDGE AQI demonstrates a complete professional-grade embedded IoT ecosystem by combining:

* Embedded Systems
* Edge AI
* TinyML
* Secure Firmware
* IoT Connectivity
* Environmental Intelligence

into a compact, intelligent, and scalable **Air Quality Monitoring Solution** powered by the advanced PIC32CM secure microcontroller platform from [Microchip Technology](https://www.microchip.com?utm_source=chatgpt.com).

[1]: https://arxiv.org/abs/2509.00353?utm_source=chatgpt.com "AQFusionNet: Multimodal Deep Learning for Air Quality Index Prediction with Imagery and Sensor Data"
[2]: https://github.com/Awais-Asghar/IoT-Based-Air-Quality-Monitoring-and-AQI-Measurement-System?utm_source=chatgpt.com "GitHub - Awais-Asghar/Crowd-Sourced-AQI-Monitoring-System: An IoT-powered system for real-time air quality monitoring and analysis. This project integrates environmental sensors with a machine learning model to predict and assess air quality indices. Features include data visualization, predictive analytics, and automated alerts for actionable insights."
