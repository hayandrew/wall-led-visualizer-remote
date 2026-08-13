#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display_manager.h"
#include "controls_manager.h"
#include "project_config.h"
#include "led_manager.h"
#include "audio_processor.h"
#include <cmath>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Share reset pin with ESP32
#define SCREEN_ADDRESS 0x3C // Standard I2C address for SSD1306

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

namespace DisplayManager {
    // Scrolling diagnostics buffer
    static float history[128] = {0.0f};
    static int historyIndex = 0;

    // Centered text drawing helper
    void drawCenteredText(const char* text, int y, int size) {
        int len = strlen(text);
        int charWidth = (size == 1) ? 6 : 12;
        int x = (128 - (len * charWidth)) / 2;
        if (x < 0) x = 0;
        display.setTextSize(size);
        display.setCursor(x, y);
        display.print(text);
    }

    // Helper to get shortened mode names that fit on the 128px screen width
    const char* getShortModeName(VisualizerMode mode) {
        switch (mode) {
            case MODE_WHITE_WAVEFORM:     return "Waveform";
            case MODE_AUDIO_PARTICLES:    return "Particle";
            case MODE_RAINBOW_WAVE:       return "Rainbow";
            case MODE_SOUND_RIPPLES:      return "Ripple";
            case MODE_LAVA_LAMP:          return "Lava";
            case MODE_DIGITAL_RAIN:       return "Rain";
            case MODE_SPECTRUM_LINEAR:    return "Linear";
            case MODE_PULSING_TUNNEL:     return "Tunnel";
            case MODE_MARIO_RUN:          return "Mario";
            default:                      return "Visual";
        }
    }

    void init() {
        Serial.println("[Display] Initializing Custom I2C for SSD1306...");
        Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

        if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
            Serial.println("[Display] SSD1306 allocation failed. Check wiring!");
            return;
        }

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.display();
        Serial.println("[Display] OLED screen ready.");
    }

    void update() {
        // Redraw at ~30 FPS
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw < 33) return;
        lastDraw = millis();

        // 1. Update the scrolling volume history buffer
        history[historyIndex] = AudioProcessor::getVolumeEnvelope();
        historyIndex = (historyIndex + 1) % 128;

        // 2. Clear buffers and start drawing
        display.clearDisplay();

        // Fetch active settings and controls state
        int cursor = ControlsManager::getMenuCursor();
        bool editing = ControlsManager::isEditing();

        // Header Title (Current Menu Item)
        display.setTextColor(SSD1306_WHITE);
        switch (cursor) {
            case 0: drawCenteredText("Visualizer Mode", 0, 1); break;
            case 1: drawCenteredText("Source Select", 0, 1); break;
            case 2: drawCenteredText("LED Brightness", 0, 1); break;
            case 3: drawCenteredText("Microphone Gain", 0, 1); break;
            case 4: drawCenteredText("Auto-Cycling", 0, 1); break;
        }
        display.drawFastHLine(0, 9, 128, SSD1306_WHITE);

        // Render Focused Parameter Value or Progress Bar
        if (cursor == 0) {
            drawCenteredText(getShortModeName(ControlsManager::getVisualizerModePreview()), 26, 2);
            if (editing) {
                display.fillTriangle(4, 29, 4, 39, 10, 34, SSD1306_WHITE);
                display.fillTriangle(124, 29, 124, 39, 118, 34, SSD1306_WHITE);
            }
        } else if (cursor == 1) {
            drawCenteredText(LEDManager::getSourceName(ControlsManager::getSourcePreview()), 26, 2);
            if (editing) {
                display.fillTriangle(4, 29, 4, 39, 10, 34, SSD1306_WHITE);
                display.fillTriangle(124, 29, 124, 39, 118, 34, SSD1306_WHITE);
            }
        } else if (cursor == 2) {
            int pct = (int)round((ControlsManager::getBrightnessPreview() * 100.0) / 255.0);
            if (editing) {
                // Draw progress bar on the left (80px wide, x=8 to 88)
                int fillWidth = (pct * 80) / 100;
                if (fillWidth > 80) fillWidth = 80;
                if (fillWidth < 0) fillWidth = 0;
                
                display.drawRect(8, 28, 80, 12, SSD1306_WHITE);
                display.fillRect(8, 28, fillWidth, 12, SSD1306_WHITE);
                
                char buf[8];
                sprintf(buf, "%d%%", pct);
                int tx = 112 - (strlen(buf) * 3); // Center of right-hand column (x=96 to 127)
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                display.setCursor(tx, 30);
                display.print(buf);
            } else {
                char buf[8];
                sprintf(buf, "%d%%", pct);
                drawCenteredText(buf, 26, 2);
            }
        } else if (cursor == 3) {
            float g = ControlsManager::getGainPreview();
            if (editing) {
                // Draw progress bar for gain on the left (0.2x to 5.0x mapped to 0-80px)
                float gainPct = ((g - 0.2f) / (5.0f - 0.2f)) * 100.0f;
                int fillWidth = (int)((gainPct * 80.0f) / 100.0f);
                if (fillWidth > 80) fillWidth = 80;
                if (fillWidth < 0) fillWidth = 0;
                
                display.drawRect(8, 28, 80, 12, SSD1306_WHITE);
                display.fillRect(8, 28, fillWidth, 12, SSD1306_WHITE);
                
                char buf[8];
                sprintf(buf, "%.1fx", g);
                int tx = 112 - (strlen(buf) * 3); // Center of right-hand column (x=96 to 127)
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                display.setCursor(tx, 30);
                display.print(buf);
            } else {
                char buf[8];
                sprintf(buf, "%.1fx", g);
                drawCenteredText(buf, 26, 2);
            }
        } else if (cursor == 4) {
            bool ac = ControlsManager::getAutoCyclePreview();
            drawCenteredText(ac ? "ON" : "OFF", 26, 2);
            if (editing) {
                display.fillTriangle(4, 29, 4, 39, 10, 34, SSD1306_WHITE);
                display.fillTriangle(124, 29, 124, 39, 118, 34, SSD1306_WHITE);
            }
        }

        // Draw Divider Line for diagnostics window
        display.drawFastHLine(0, 49, 128, SSD1306_WHITE);

        // Render Scrolling Diagnostics Waveform
        float maxVal = 100.0f; // Minimal floor to prevent tiny noise from auto-maximizing
        for (int j = 0; j < 128; j++) {
            if (history[j] > maxVal) maxVal = history[j];
        }

        for (int x = 0; x < 128; x++) {
            int idx = (historyIndex + x) % 128;
            float val = history[idx];
            
            // Map value into a 12px height container (bottom y-rows 51 to 63)
            int barHeight = (int)((val / maxVal) * 11.0f);
            if (barHeight > 11) barHeight = 11;
            
            // Draw a vertical line from the bottom (63) upward
            display.drawFastVLine(x, 63 - barHeight, barHeight + 1, SSD1306_WHITE);
        }

        // Render the buffer to the physical screen
        display.display();
    }

    void drawOtaProgress(unsigned int progress, unsigned int total) {
        static int lastPercent = -1;
        int percent = (total > 0) ? (progress * 100 / total) : 0;
        if (percent == lastPercent && progress > 0) {
            return;
        }

        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);

        // Header Title
        drawCenteredText("OTA UPDATE", 0, 1);
        display.drawFastHLine(0, 9, 128, SSD1306_WHITE);

        // Main info text
        drawCenteredText("Downloading...", 15, 1);

        // Draw progress bar border (100px wide, centered: x=14 to 114)
        display.drawRect(14, 28, 100, 10, SSD1306_WHITE);
        // Draw progress bar fill
        int fillWidth = percent;
        display.fillRect(14, 28, fillWidth, 10, SSD1306_WHITE);

        // Draw percentage text
        char buf[16];
        sprintf(buf, "%d%% completed", percent);
        drawCenteredText(buf, 44, 1);

        // Refresh screen
        display.display();

        if (percent >= 100) {
            lastPercent = -1;
        } else {
            lastPercent = percent;
        }
    }

    void drawOtaError(const char* errorMsg) {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        drawCenteredText("OTA ERROR", 0, 1);
        display.drawFastHLine(0, 9, 128, SSD1306_WHITE);
        drawCenteredText(errorMsg, 24, 1);
        drawCenteredText("Rebooting...", 44, 1);
        display.display();
    }
}
