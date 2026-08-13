#ifndef DIYHUE_MANAGER_H
#define DIYHUE_MANAGER_H

#include <Arduino.h>

namespace DiyHueManager {
    // Initialize the WebServer and register endpoints
    void init();

    // Call in the main loop to process HTTP requests
    void update();

    // Check if the light is on
    bool isOn();

    // Get the RGB values representing the light's current state
    void getRgbColor(uint8_t& r, uint8_t& g, uint8_t& b);

    // Get the brightness level (0-255)
    uint8_t getBrightness();

    // Check if a state change was received (to auto-switch to diyHue mode)
    bool hasNewCommand();

    // Reset the new command flag
    void clearNewCommand();
}

#endif // DIYHUE_MANAGER_H
