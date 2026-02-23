# GeoSurveyor Pro 🛰️
### Professional ESP32 Field Surveying & Depth Monitoring System

GeoSurveyor Pro is an advanced, field-ready firmware for ESP32 designed for river depth monitoring and geographical surveying. It integrates GPS, Ultrasonic sensing, and a dual-storage logging system to ensure data integrity even in remote environments.

![Platform](https://img.shields.io/badge/Platform-ESP32-blue?style=for-the-badge&logo=espressif)
![Language](https://img.shields.io/badge/Language-C++-green?style=for-the-badge&logo=c%2B%2B)
![Storage](https://img.shields.io/badge/Storage-SD%20%2B%20SPIFFS-orange?style=for-the-badge)

## 🚀 Key Features
- **Dual Storage Engine**: Primary logging to SD Card with automatic transparent fallback to internal SPIFFS flash if the SD card is missing or fails.
- **Precision Surveying**: Combines Ultrasonic distance (depth) with high-accuracy GPS coordinates.
- **Cloud Integration**: Real-time data upload to Google Sheets via Google Apps Script (when WiFi is available).
- **Sci-Fi UI/UX**: Premium SH1106 OLED interface with dynamic sparklines, smooth scrolling menus, and "Sci-Fi" audio feedback.
- **Power Management**: Intelligent deep sleep mode with button wake-up for extended field use.
- **WiFi Config Portal**: On-device AP portal to configure local WiFi credentials without reflashing.

## 🛠️ Hardware Requirements
| Component | Pin (ESP32) | Notes |
|-----------|-------------|-------|
| **Ultrasonic Trig** | GPIO 26 | Depth Sensor |
| **Ultrasonic Echo** | GPIO 27 | Depth Sensor |
| **Buzzer** | GPIO 13 | PWM Audio Feedback |
| **Button A** | GPIO 15 | Navigation / Wake-up |
| **Button B** | GPIO 4 | Navigation |
| **GPS RX** | GPIO 16 | Serial2 |
| **GPS TX** | GPIO 17 | Serial2 |
| **OLED SCL** | GPIO 22 | I2C |
| **OLED SDA** | GPIO 21 | I2C |
| **SD CS** | GPIO 5 | VSPI |

## 📦 Software Dependencies
The following libraries are required:
- `Adafruit SH110X`
- `Adafruit GFX`
- `TinyGPSPlus`
- `Preferences` (Built-in)
- `WiFi` (Built-in)
- `HTTPClient` (Built-in)

## 🔧 Setup & Installation
1. **Clone the Repo**:
   ```bash
   git clone https://github.com/techsavvyomi/RiverDepth.git
   ```
2. **Configure Cloud (Optional)**:
   Update the `GOOGLE_SCRIPT_URL` in `Main.ino` with your Google Apps Script deployment URL.
3. **Flash**: Use Arduino IDE or PlatformIO to flash `Main.ino` to your ESP32.
4. **Calibrate**: Use the provided scripts in the `tests/` directory to verify individual sensor performance.

## �️ Firmware Operation & Menu Guide

The GeoSurveyor Pro features a non-blocking, interrupt-driven UI. Navigation is handled via two buttons (**BTN_A** and **BTN_B**).

### Navigation Controls
- **BTN_A (Short Press)**: Select / Enter / Action (e.g., Save point).
- **BTN_A (Long Press)**: Special actions (e.g., Entering log sub-menus).
- **BTN_B (Short Press)**: Next Item / Scroll / Back.
- **Auto-Return**: The system automatically returns to **Live View** from any menu after 10 seconds of inactivity.

### Main Menu Breakdown
1. **Live View**: The default home screen. Features a real-time sparkline graph of depth, GPS coordinates, satellite count, and storage status.
2. **Capture Mode**: Dedicated screen for pinpointing a survey location. Provides a large "Hold A to Save" prompt.
3. **GPS Detail**: Displays high-precision coordinates, HDOP (accuracy), and a "Radar" animation. Allows **GPS Hold** to lock coordinates for manual site notes.
4. **View Logs**: Browse recorded surveys. Supports scrolling through ID, Depth, and Lat/Lon per record.
5. **Upload Data**: Syncs all stored records to the cloud. Features a graphical progress bar during batch transmission.
6. **Status**: Quick diagnostic overview of Memory (SD/Internal), GPS Fix, Mute status, and WiFi connectivity.
7. **Settings**: 
    - **Toggle Mute**: Enable/Disable Sci-Fi audio feedback.
    - **Connect WiFi**: Attempts to connect to the last saved network.
    - **Config WiFi**: Launches a 192.168.4.1 portal for mobile configuration.
8. **Clear Logs**: Formats the survey file (deletes all records). Requires confirmation sound.

## 🔉 Sci-Fi Audio System
The firmware uses a custom frequency-sweep audio engine to provide non-visual feedback:
- **Satellite Laser Blip**: High-pitched chirp when a new satellite is acquired.
- **GPS Lock Sweep**: Ascending triple-tone when a 3D fix is first established.
- **Search Ping**: Periodic "sonar" ping when in GPS Detail mode without a fix.
- **Save Confirmation**: Tri-tone melody indicating successful data persistence.
- **Error Tone**: Low-frequency long buzz for hardware failure or missing memory.

## 📊 Data Format (CSV)
Logs are stored in `/geosurvey.csv` with the following structure:
`ID, DISTANCE_CM, LATITUDE, LONGITUDE, SATELLITE_COUNT`

Example: `42, 125, 19.0760, 72.8777, 9`

## 📜 License
This project is open-source. Please attribute the original author when using in your own projects.
