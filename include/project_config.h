#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// LED Matrix Geometry
#define MATRIX_WIDTH 15
#define MATRIX_HEIGHT 17
#define NUM_LEDS (MATRIX_WIDTH * MATRIX_HEIGHT)
#define SERPENTINE true

// Hardware Pins
#define LED_PIN 2

// I2S Microphone Configuration (INMP441)
#define I2S_SCK_PIN 8
#define I2S_WS_PIN  3
#define I2S_SD_PIN  4

// Audio Sampling Configuration
#define I2S_SAMPLE_RATE 16000  // 16kHz sampling rate
#define I2S_BUFFER_SIZE 256    // Number of samples per DMA read block

// Wi-Fi AP Settings
#define AP_SSID "ESP32C3-Visualizer"
#define AP_PASSWORD "12345678"

// Physical Controls Pin Settings
#define ENCODER_CLK_PIN 5
#define ENCODER_DT_PIN  6
#define ENCODER_SW_PIN  7
#define OLED_SDA_PIN    0
#define OLED_SCL_PIN    1

#include <WiFi.h>

extern WiFiClient telnetClient;

class TelnetLogger : public Print {
public:
    void begin(unsigned long baud) {
        ::Serial.begin(baud);
    }
    int available() {
        return ::Serial.available();
    }
    int read() {
        return ::Serial.read();
    }
    size_t write(uint8_t c) override {
        ::Serial.write(c);
        if (xPortInIsrContext() == pdFALSE && telnetClient && telnetClient.connected()) {
            telnetClient.write(c);
        }
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        ::Serial.write(buffer, size);
        if (xPortInIsrContext() == pdFALSE && telnetClient && telnetClient.connected()) {
            telnetClient.write(buffer, size);
        }
        return size;
    }
};

extern TelnetLogger telnetLogger;
#define Serial telnetLogger

#endif // CONFIG_H
