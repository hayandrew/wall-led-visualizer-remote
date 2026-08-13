# ESP32-C3 LED Music Visualizer

An audio-reactive LED music visualizer built on the ESP32-C3 microcontroller, using a WS2812B LED matrix and an INMP441 I2S digital microphone.

---

## Hardware Configuration

The project utilizes the following hardware configurations defined in `include/project_config.h`:

### LED Matrix

- **Geometry:** 15 Columns × 17 Rows (Serpentine layout)
- **Total LEDs:** 255
- **LED Pin:** `GPIO 2`

### I2S Microphone (INMP441)

- **I2S SCK Pin (Serial Clock):** `GPIO 8`
- **I2S WS Pin (Word Select):** `GPIO 3`
- **I2S SD Pin (Serial Data):** `GPIO 4`
- **Sampling Rate:** 16,000 Hz

### Wi-Fi Access Point Settings

By default, the microcontroller hosts its own Wi-Fi network for OTA updates:

- **SSID:** `ESP32C3-Visualizer`
- **Password:** `12345678`
- **OTA Port:** `3232`

---

## Development Roadmap

This project is divided into several iterative phases:

### **Phase 1: LED Diagnostics & OTA Setup** (Complete)

- Set up basic PlatformIO project.
- Programmed the column-major serpentine matrix coordinate mapping.
- Verified LED connection with a pulsating algebraic heartbeat diagnostics pattern.
- Configured wireless OTA update callbacks to flash the device over Wi-Fi.

### **Phase 2: Audio Capture & Envelope Tracking** (Complete)

- Configured ESP32-C3 I2S interface to sample raw microphone data in a background FreeRTOS task.
- Implemented a DC blocking IIR filter to remove offset bias from raw samples.
- Implemented volume envelope and peak amplitude tracking.
- Connected the volume envelope to scale the diagnostics LED heartbeat animation in real-time.

### **Phase 3: FFT Frequency Analysis** (Complete)

- Integrated the `arduinoFFT` library to compute fast Fourier transforms on incoming audio data.
- Sorted frequencies into 7 bands (Sub-Bass to Brilliance) with dynamic automatic gain control (AGC) tracking.
- Isolated processing from the DMA capture loop to maintain high animation frame rates.

### **Phase 4: Audio-Reactive Visualization Modes** (Complete)

- Implemented linear and symmetric spectrum visualizer bars.
- Added a dual VU meter reacting to peak amplitude and volume envelope.
- Developed custom frequency-reactive effects including Bass Pulsing circles and Sound Ripple expansions.

### **Phase 5: Physical Menu & Controls (SSD1306 & Rotary Encoder)** (Complete)

- Integrated a 128x64 SSD1306 OLED display over custom I2C pins (SDA: GPIO 0, SCL: GPIO 1) to render a local settings menu.
- Connected a rotary encoder (CLK: GPIO 5, DT: GPIO 6, SW: GPIO 7) driven by a bounce-immune quadrature Gray-code state machine.
- Implemented interactive menu states: Navigation Mode (moving selection cursor) and Edit Mode (adjusting visualizer modes, brightness levels, and gain scaling).
- Programmed a real-time scrolling wave graph (oscilloscope) at the bottom of the screen mapping the active volume envelope.
- _(⏳ Remaining)_ Persist user configurations in non-volatile flash memory (Preferences/NVS).

---

## Getting Started

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/) extension installed.
- ESP32-C3 development board.

### Build and Upload

1. Clone the repository and open the folder in VS Code / PlatformIO.
2. Build the project:

   ```bash
   pio run
   ```

3. Upload the firmware over USB:

   ```bash
   pio run --target upload
   ```

4. Subsequent updates can be flashed wirelessly (OTA) by connecting to the `ESP32C3-Visualizer` Wi-Fi AP and uploading:

   ```bash
   pio run --target upload --upload-port <ESP32-AP-IP>
   ```
