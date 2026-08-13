#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>

enum SourceMode {
    SOURCE_SOUND = 0,
    SOURCE_WIFI
};

enum VisualizerMode {
    MODE_WHITE_WAVEFORM = 0,
    MODE_AUDIO_PARTICLES,
    MODE_RAINBOW_WAVE,
    MODE_SOUND_RIPPLES,
    MODE_LAVA_LAMP,
    MODE_DIGITAL_RAIN,
    MODE_SPECTRUM_LINEAR,
    MODE_PULSING_TUNNEL,
    MODE_MARIO_RUN,
    // MODE_DIAGNOSTIC_HEART,
    // MODE_NOISE,
    // MODE_FIRE_PORTAL,
    // MODE_SUBSCRIBE,
    MODE_COUNT // Keeps track of total modes
};

namespace LEDManager {
    // Get/Set current input source (Sound or WiFi)
    SourceMode getSource();
    void setSource(SourceMode source);
    const char* getSourceName(SourceMode source);
    // Initialize the FastLED setup, grid dimensions, and safe brightness level
    void init();

    // Calculate and draw the active animation frame on the LED matrix
    void update();

    // Set a specific animation mode
    void setMode(VisualizerMode mode);

    // Switch to the next available animation mode
    void nextMode();

    // Get the name of the active mode for debug logging and OLED display
    const char* getModeName(VisualizerMode mode);

    // Get the currently active mode
    VisualizerMode getActiveMode();

    // Set global brightness (0-255)
    void setBrightness(uint8_t brightness);

    // Get current global brightness
    uint8_t getBrightness();

    // Set auto-cycle enabled/disabled
    void setAutoCycle(bool enabled);

    // Get auto-cycle status
    bool getAutoCycle();
}

#endif // LED_MANAGER_H
