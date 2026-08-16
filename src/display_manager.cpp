#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display_manager.h"
#include "controls_manager.h"
#include "project_config.h"
#include "led_manager.h"
#include <cmath>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Share reset pin with ESP32
#define SCREEN_ADDRESS 0x3C // Standard I2C address for SSD1306

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

namespace DisplayManager {

    static volatile bool fatalErrorActive = false;
    static const char* fatalErrorLine1 = nullptr;
    static const char* fatalErrorLine2 = nullptr;

    // Centered text drawing helper
    void drawCenteredText(const char* text, int y, int size) {
        int len = strlen(text);
        int charWidth = (size == 1) ? 6 : 12;
        int x = (128 - (len * charWidth)) / 2;
        if (x < 0) x = 0;
        display.setTextSize(size);
        display.setCursor(x, y + 10);
        display.print(text);
    }

    // Centered text drawing helper with left/right arrowheads flanking it at the margins
    void drawHeaderWithArrows(const char* text, int y, bool drawArrows) {
        int len = strlen(text);
        int charWidth = 6;
        int x_start = (128 - (len * charWidth)) / 2;

        display.setTextSize(1);
        display.setCursor(x_start, y + 10);
        display.print(text);

        if (drawArrows) {
            // Draw left arrow (pointing left) at the left margin
            display.fillTriangle(4, y + 14, 8, y + 11, 8, y + 17, SSD1306_WHITE);

            // Draw right arrow (pointing right) at the right margin
            display.fillTriangle(124, y + 14, 120, y + 11, 120, y + 17, SSD1306_WHITE);
        }
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
        Wire.setClock(400000); // Speed up I2C clock to 400kHz to reduce blocking time during screen updates

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

        // Fetch active settings and controls state
        int cursor = ControlsManager::getMenuCursor();
        bool editing = ControlsManager::isEditing();
        VisualizerMode modePreview = ControlsManager::getVisualizerModePreview();
        SourceMode sourcePreview = ControlsManager::getSourcePreview();
        uint8_t brightnessPreview = ControlsManager::getBrightnessPreview();
        float gainPreview = ControlsManager::getGainPreview();
        bool autoCyclePreview = ControlsManager::getAutoCyclePreview();

        // Check if any state changed since the last redraw
        static int lastCursor = -1;
        static bool lastEditing = false;
        static VisualizerMode lastModePreview = MODE_COUNT;
        static SourceMode lastSourcePreview = (SourceMode)-1;
        static uint8_t lastBrightnessPreview = 0;
        static float lastGainPreview = -1.0f;
        static bool lastAutoCyclePreview = false;
        static bool lastFatalErrorActive = false;
        static const char* lastErrorLine1 = nullptr;
        static const char* lastErrorLine2 = nullptr;

        bool dirty = (cursor != lastCursor) ||
                     (editing != lastEditing) ||
                     (modePreview != lastModePreview) ||
                     (sourcePreview != lastSourcePreview) ||
                     (brightnessPreview != lastBrightnessPreview) ||
                     (gainPreview != lastGainPreview) ||
                     (autoCyclePreview != lastAutoCyclePreview) ||
                     (fatalErrorActive != lastFatalErrorActive) ||
                     (fatalErrorLine1 != lastErrorLine1) ||
                     (fatalErrorLine2 != lastErrorLine2);

        if (!dirty) return;

        // Update the last known state
        lastDraw = millis();
        lastCursor = cursor;
        lastEditing = editing;
        lastModePreview = modePreview;
        lastSourcePreview = sourcePreview;
        lastBrightnessPreview = brightnessPreview;
        lastGainPreview = gainPreview;
        lastAutoCyclePreview = autoCyclePreview;
        lastFatalErrorActive = fatalErrorActive;
        lastErrorLine1 = fatalErrorLine1;
        lastErrorLine2 = fatalErrorLine2;

        // Clear buffers and start drawing
        display.clearDisplay();

        if (fatalErrorActive) {
            display.setTextColor(SSD1306_WHITE);
            drawCenteredText("CONNECTION ERROR", 4, 1);
            display.drawFastHLine(0, 14 + 10, 128, SSD1306_WHITE);
            
            if (fatalErrorLine1 != nullptr) {
                drawCenteredText(fatalErrorLine1, 28, 1);
            }
            if (fatalErrorLine2 != nullptr) {
                drawCenteredText(fatalErrorLine2, 44, 1);
            }
            display.display();
            return;
        }

        // Header Title (Current Menu Item)
        display.setTextColor(SSD1306_WHITE);
        switch (cursor) {
            case 0: drawHeaderWithArrows("VISUALIZER", 5, !editing); break;
            case 1: drawHeaderWithArrows("SOURCE", 5, !editing); break;
            case 2: drawHeaderWithArrows("BRIGHTNESS", 5, !editing); break;
            case 3: drawHeaderWithArrows("MIC GAIN", 5, !editing); break;
            case 4: drawHeaderWithArrows("CYCLE", 5, !editing); break;
        }
        display.drawFastHLine(0, 9 + 20, 128, SSD1306_WHITE);

        // Render Focused Parameter Value or Progress Bar
        if (cursor == 0) {
            drawCenteredText(getShortModeName(ControlsManager::getVisualizerModePreview()), 31, 2);
            if (editing) {
                display.fillTriangle(4, 29 + 15, 4, 39 + 15, 10, 34 + 15, SSD1306_WHITE);
                display.fillTriangle(124, 29 + 15, 124, 39 + 15, 118, 34 + 15, SSD1306_WHITE);
            }
        } else if (cursor == 1) {
            drawCenteredText(LEDManager::getSourceName(ControlsManager::getSourcePreview()), 31, 2);
            if (editing) {
                display.fillTriangle(4, 29 + 15, 4, 39 + 15, 10, 34 + 15, SSD1306_WHITE);
                display.fillTriangle(124, 29 + 15, 124, 39 + 15, 118, 34 + 15, SSD1306_WHITE);
            }
        } else if (cursor == 2) {
            int pct = (int)round((ControlsManager::getBrightnessPreview() * 100.0) / 255.0);
            if (editing) {
                // Draw progress bar on the left (80px wide, x=8 to 88)
                int fillWidth = (pct * 80) / 100;
                if (fillWidth > 80) fillWidth = 80;
                if (fillWidth < 0) fillWidth = 0;
                
                display.drawRect(8, 28 + 15, 80, 12, SSD1306_WHITE);
                display.fillRect(8, 28 + 15, fillWidth, 12, SSD1306_WHITE);
                
                char buf[8];
                sprintf(buf, "%d%%", pct);
                int tx = 112 - (strlen(buf) * 3); // Center of right-hand column (x=96 to 127)
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                display.setCursor(tx, 30 + 15);
                display.print(buf);
            } else {
                char buf[8];
                sprintf(buf, "%d%%", pct);
                drawCenteredText(buf, 31, 2);
            }
        } else if (cursor == 3) {
            float g = ControlsManager::getGainPreview();
            if (editing) {
                // Draw progress bar for gain on the left (0.2x to 5.0x mapped to 0-80px)
                float gainPct = ((g - 0.2f) / (5.0f - 0.2f)) * 100.0f;
                int fillWidth = (int)((gainPct * 80.0f) / 100.0f);
                if (fillWidth > 80) fillWidth = 80;
                if (fillWidth < 0) fillWidth = 0;
                
                display.drawRect(8, 28 + 15, 80, 12, SSD1306_WHITE);
                display.fillRect(8, 28 + 15, fillWidth, 12, SSD1306_WHITE);
                
                char buf[8];
                sprintf(buf, "%.1fx", g);
                int tx = 112 - (strlen(buf) * 3); // Center of right-hand column (x=96 to 127)
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                display.setCursor(tx, 30 + 15);
                display.print(buf);
            } else {
                char buf[8];
                sprintf(buf, "%.1fx", g);
                drawCenteredText(buf, 31, 2);
            }
        } else if (cursor == 4) {
            bool ac = ControlsManager::getAutoCyclePreview();
            drawCenteredText(ac ? "ON" : "OFF", 31, 2);
            if (editing) {
                display.fillTriangle(4, 29 + 15, 4, 39 + 15, 10, 34 + 15, SSD1306_WHITE);
                display.fillTriangle(124, 29 + 15, 124, 39 + 15, 118, 34 + 15, SSD1306_WHITE);
            }
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
        display.drawFastHLine(0, 9 + 10, 128, SSD1306_WHITE);

        // Main info text
        drawCenteredText("Downloading...", 15, 1);

        // Draw progress bar border (100px wide, centered: x=14 to 114)
        display.drawRect(14, 28 + 10, 100, 10, SSD1306_WHITE);
        // Draw progress bar fill
        int fillWidth = percent;
        display.fillRect(14, 28 + 10, fillWidth, 10, SSD1306_WHITE);

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
        display.drawFastHLine(0, 9 + 10, 128, SSD1306_WHITE);
        drawCenteredText(errorMsg, 24, 1);
        drawCenteredText("Rebooting...", 44, 1);
        display.display();
    }

    void drawBootStatus(const char* status, const char* details) {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        drawCenteredText("SYSTEM BOOT", 4, 1);
        display.drawFastHLine(0, 14 + 10, 128, SSD1306_WHITE);
        
        drawCenteredText(status, 28, 1);
        if (details != nullptr) {
            drawCenteredText(details, 44, 1);
        }
        display.display();
    }

    void setFatalError(const char* line1, const char* line2) {
        fatalErrorLine1 = line1;
        fatalErrorLine2 = line2;
        fatalErrorActive = true;
    }

    void clearFatalError() {
        fatalErrorActive = false;
    }

    bool isFatalErrorActive() {
        return fatalErrorActive;
    }
}
