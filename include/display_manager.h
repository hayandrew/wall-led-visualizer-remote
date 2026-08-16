#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

namespace DisplayManager {
    // Initialize the SSD1306 OLED display using custom I2C pins
    void init();

    // Redraw the screen contents (menu & real-time volume diagnostics) at non-blocking intervals
    void update();

    // Draw OTA progress bar and percentage
    void drawOtaProgress(unsigned int progress, unsigned int total);

    // Draw OTA error message
    void drawOtaError(const char* errorMsg);

    // Draw boot status screen
    void drawBootStatus(const char* status, const char* details = nullptr);

    // Set fatal connection error state
    void setFatalError(const char* line1, const char* line2 = nullptr);

    // Clear fatal connection error state
    void clearFatalError();

    // Check if fatal connection error state is active
    bool isFatalErrorActive();
}

#endif // DISPLAY_MANAGER_H
