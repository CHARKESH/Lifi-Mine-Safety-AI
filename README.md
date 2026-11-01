# LiFi-Based Underground Mine Safety and Emergency Communication System

An ESP32-based underground mine safety prototype that monitors environmental conditions, transmits data through LiFi optical communication, and generates real-time safety advisories using a locally running AI model.

## Project Overview

Underground mining environments require continuous environmental monitoring and reliable emergency communication. This project integrates an ESP32, BMP180 sensor, LiFi communication, and local AI to improve mine safety.

The system measures temperature and atmospheric pressure, transmits sensor data through an LED-based LiFi link, and uses Ollama with Llama 3.2 1B to generate safety advisories without cloud dependency.

A web dashboard enables the external safety team to monitor environmental conditions and view AI-generated alerts, while workers inside the tunnel can receive emergency instructions through the LiFi communication system.

## Circuit connection picture:

<img width="1029" height="504" alt="image" src="https://github.com/user-attachments/assets/77e14555-e8cf-4730-a755-f0ea129bf985" />

## Key Features

- Real-time environmental monitoring using ESP32 and BMP180.
- LiFi-based optical data transmission using LED and LDR.
- Wireless-free optical communication link.
- Local AI analysis using Ollama and Llama 3.2 1B.
- Live web dashboard with sensor readings and safety advisories.
- Offline AI processing without internet or cloud dependency.
- Emergency alert communication support.

## Technologies Used

| Category | Technologies |
|---|---|
| Microcontroller | ESP32 |
| Programming | Embedded C/C++, Python |
| Sensor | BMP180 |
| Communication | LiFi (LED and LDR) |
| Backend | Python, Flask |
| AI Framework | Ollama |
| AI Model | Llama 3.2 1B |
| Frontend | HTML, CSS, JavaScript |
| Development Tool | Arduino IDE |

## Circuit Connections

### 1. BMP180 Sensor to ESP32

| BMP180 Pin | ESP32 Pin | Description |
|---|---|---|
| VCC | 3V3 | Power supply |
| GND | GND | Ground |
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |

### 2. LiFi LED Transmitter to ESP32

| LED Pin | ESP32 Connection | Description |
|---|---|---|
| Anode (+) | GPIO 25 through 220Ω resistor | Optical signal output |
| Cathode (-) | GND | Ground |

### 3. LDR Receiver Module to ESP32

| LDR Module Pin | ESP32 Pin | Description |
|---|---|---|
| VCC | 3V3 | Power supply |
| GND | GND | Ground |
| AO | GPIO 34 | Analogue light signal input |

**Note:** If your receiver code uses the digital output, connect DO instead of AO and configure the corresponding GPIO as a digital input. Use the actual GPIO numbers defined in your source code.

## Working Principle

1. The BMP180 sensor measures temperature and atmospheric pressure.
2. The ESP32 collects and processes the sensor readings.
3. Sensor data is transmitted through the LiFi LED using modulated light signals.
4. The LDR receiver detects the optical signals and converts them into electrical signals.
5. A Python bridge processes the received sensor data.
6. The bridge sends the readings to the locally running Ollama AI service.
7. Llama 3.2 1B analyses the readings and generates safety advisories.
8. The Flask-based web dashboard displays live sensor readings and AI-generated alerts.

## AI Model Setup

This project uses **Meta Llama 3.2 1B** through Ollama for local AI-based safety analysis.

Install Ollama and download the model:

```bash
ollama pull llama3.2:1b
```

Run the model locally:

```bash
ollama run llama3.2:1b
```

The AI model analyses environmental readings locally without requiring cloud-based AI services.

## System Architecture

```text
BMP180 Sensor
      |
      v
   ESP32
      |
      v
LiFi LED Transmitter
      |
   Light Signal
      |
      v
LDR Receiver
      |
      v
 Python Bridge
      |
      v
Ollama - Llama 3.2 1B
      |
      v
Flask Web Dashboard
      |
      v
Live Monitoring & Safety Advisories
```

## Applications

- Underground mine environmental monitoring.
- Optical communication in underground tunnels.
- Emergency alert and instruction delivery.
- Industrial safety monitoring.
- AI-assisted environmental risk assessment.

## Future Enhancements

- Integration of methane and toxic gas sensors.
- Addition of smoke and fire detection.
- Worker location and tracking capabilities.
- Improved two-way LiFi communication.
- Advanced AI-based hazard prediction.

## Project Objective

To develop a low-cost, ESP32-based underground mine safety prototype that combines environmental monitoring, LiFi optical communication, and local AI analysis to support faster safety decisions and emergency communication.

## Disclaimer

This is an experimental prototype, not a certified mining safety or emergency communication system. BMP180 measures temperature and atmospheric pressure but does not detect methane, oxygen deficiency, or toxic gases. Additional certified sensors, industrial-grade communication equipment, and independent safety systems are required for real underground mine deployment.
