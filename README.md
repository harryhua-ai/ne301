# NE301 - STM32N6570 AI Vision Camera

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-STM32N6570-blue)]()
[![License](https://img.shields.io/badge/license-Proprietary-red)]()
[![Version](https://img.shields.io/badge/version-1.0.0.1-blue)]()

> High-performance AI vision camera system based on STM32N6570 Discovery Kit, featuring real-time video processing, neural network acceleration, and modern web interface.

## ✨ Key Features

- 🎥 **Real-time Video Processing** - Multiple camera sensor support (OS04C10, IMX335)
- 🧠 **AI Accelerated Inference** - NPU hardware acceleration, YOLOv8/YOLOX object detection
- 🌐 **Modern Web Interface** - Preact + TypeScript, real-time preview and configuration
- 📡 **Multi-network Support** - Ethernet, WiFi, USB connectivity
- 🔒 **Secure Boot** - TrustZone secure partitioning, firmware signature verification
- 🔄 **OTA Updates** - Secure over-the-air firmware and model upgrade

## 🎬 Preview

### Take a Closer Look at CamThink NeoEyes NE301  
[![Watch the video](https://resources.camthink.ai/video/product.jpg)](https://resources.camthink.ai/video/NE301_product_introduction.mp4)

### Deploy NeoEyes NE301 Anywhere Outdoors  
[![Watch the video](https://resources.camthink.ai/video/deploy.jpg)](https://resources.camthink.ai/video/NE301_deploy_everywhere.mp4)

### Always Awake. Always Ready for Action  
[![Watch the video](https://resources.camthink.ai/video/ready%20for%20action.jpg)](https://resources.camthink.ai/video/NE301_IO_and_MQTT_Wake-up.mp4)  

### Detecting Every Critical Moment  
[![Watch the video](https://resources.camthink.ai/video/aipreview.jpg)](https://resources.camthink.ai/video/NE301_reasoning_preview.mp4) 

[**Learn More**](https://www.camthink.ai/)

## 🏗️ Project Structure
Low Power, Performance, and Edge AI
![img](https://wiki.camthink.ai/img/ne301/overview/U0.png)
NE301 is an AI vision camera system based on STM32N6570, featuring a multi-core architecture:

- **STM32N6 (Main MCU)**: Cortex-M55, handles video processing, AI inference, and network communication
- **STM32U0 (WakeCore)**: Power control unit, manages low-power operation and wake-up
- **Web Frontend**: Preact + TypeScript, provides real-time preview and configuration interface
- **AI Models**: YOLOv8/YOLOX, supports object detection and pose estimation

```
┌─────────────────────────────────────┐
│   Web Service (HTTP/WebSocket)      │  ← User Interface Layer
├─────────────────────────────────────┤
│   Service Layer                     │  ← Service Layer
│   ├─ AI Service                     │
│   ├─ System Service                 │
│   ├─ Communication Service          │
│   ├─ Web Service                    │
│   ├─ MQTT Service                   │
│   └─ OTA Service                    │
├─────────────────────────────────────┤
│   Core Layer                        │  ← Core Layer
│   ├─ Framework                      │
│   ├─ Video Pipeline                 │
│   ├─ Configuration Manager          │
│   └─ Event Bus                      │
├─────────────────────────────────────┤
│   HAL Layer                         │  ← Hardware Abstraction Layer
│   ├─ Camera Driver                  │
│   ├─ Network Driver                 │
│   ├─ NN Driver                      │
│   └─ Storage Driver                 │
└─────────────────────────────────────┘
```
```
ne301/
├── Appli/                  # Stm32n6 Main application
├── FSBL/                   # Stm32n6 First stage bootloader
├── WakeCore/               # Power Ctrl Unit
├── Web/                    # Web frontend 
├── Model/                  # AI models
├── Script/                 # Build and packaging scripts
└── Makefile                # Root build orchestration
```

## 🚀 Quick Start

Go to [WIKI](https://wiki.camthink.ai/docs/neoeyes-ne301-series/quick-start)

## 🛠️ Development Guide 

### Development Environment Setup

#### 🐳 Method 1: Docker (Recommended)

**Prerequisites:** Docker 20.10+  Disk Space > 10GB+

```bash
# 1. Build (Or pull)Docker image
docker build -t ne301-dev:latest .
# or pull (faster)
docker pull camthink/ne301-dev:latest
# 2. Run container
docker run -it --rm --privileged \
  -v $(pwd):/workspace \
  -v /dev/bus/usb:/dev/bus/usb \
  camthink/ne301-dev:latest
# 3. Inside container
make                        # Build all
```

#### 💻 Method 2: Source Installation

**Prerequisites:**
- ARM GCC 13.3+, GNU Make 3.81+, Python 3.8+, Node.js 20+, pnpm 9+, STM32CubeProgrammer(v1.9.0), STM32_SigningTool_CLI(v1.9.0), stedgeai(v2.2,stedgeai0202.stneuralart)

```bash
# 1. Check environment
./check_env.sh
# 2. Auto setup
./setup.sh                  # Linux/macOS
setup.bat                   # Windows
# 3. Verify
./check_env.sh
```

See [SETUP.md](SETUP.md) for detailed installation instructions.

### Hardware Connect

**Prerequisites:**
- NE301 Board * 1
- ST-Link V2 * 1
- 4P 1.25mm pitch male to 2.54mm dupont female adapter (Used for flash N6 chips)
- 3P 2.54mm pitch dual female header dupont wires (Used for flash U0 chips)
- Type C USB cable compatible with the computer's USB port (for e.g., type C to type A if the computer has a type A USB port)

#### When flash apps, web, and models
1. Turn on the dip switch 2 on the board to enter the flash mode.***(After the flash is completed, please turn it off and power it back on or reset it to enter the running mode)*** 

![alt text](https://resources.camthink.ai/wiki/img/NE301_N6_FLASH_1.png)

2. Connect ST Link to the DEBUG port on the board using a 4P adapter cable and connect ST Link to the computer.

![alt text](https://resources.camthink.ai/wiki/img/NE301_N6_FLASH_2.png)

3. Connect the board to a computer or adapter using a type-c USB cable, and the onboard DEBUG indicator light will remain on, indicating that it has entered the flash mode.

![alt text](https://resources.camthink.ai/wiki/img/NE301_N6_FLASH_3.png)

#### When flash wakecore
1. Connect ST-LINK to STM32U0 chip using 3P DuPont wire and connect ST-LINK to computer.

![alt text](https://resources.camthink.ai/wiki/img/NE301_U0_FLASH_1.png)

2. Connect the board to a computer or adapter using a type-c USB cable.

![alt text](https://resources.camthink.ai/wiki/img/NE301_U0_FLASH_2.png)

### Build and Flash

```bash
# Build
make                        # Build all (FSBL + App + Web + Model)
make app                    # Build application only
make web                    # Build web frontend
make model                  # Build AI model
make pkg                    # package for flash or OTA
make info                   # help

# Flash all components
make flash

# Flash specific component
make flash-fsbl
make flash-app
make flash-web
make flash-model
make flash-wakecore
```

## 📄 License

This software is released under a **Dual-License** model.  
- *Community Edition License*   
- *Commercial Edition License*
  
Please see the full terms in [LICENSE](./LICENSE)

---

**Development Team:** CamThink AI Camera Team  
**Contact:** zbing@camthink.ai  
**Last Updated:** 2025-10-21
