#include "led_manager.h"
#include "project_config.h"

static SourceMode currentSource = SOURCE_SOUND;
static VisualizerMode currentMode = MODE_RAINBOW_WAVE;
static bool autoCycleEnabled = false;
static uint8_t currentBrightness = 102; // local cache to preserve state

namespace LEDManager {

void init() {
    Serial.println("[LED] State Manager Initialized.");
}

void update() {
    // No-op: Remote does not drive physical LEDs
}

void setMode(VisualizerMode mode) {
    if (mode < MODE_COUNT) {
        currentMode = mode;
        Serial.printf("[LED] Mode changed to: %s\n", getModeName(currentMode));
    }
}

SourceMode getSource() {
    return currentSource;
}

void setSource(SourceMode source) {
    currentSource = source;
    Serial.printf("[LED] Source changed to: %s\n", getSourceName(currentSource));
}

const char* getSourceName(SourceMode source) {
    switch (source) {
        case SOURCE_SOUND: return "Sound";
        case SOURCE_WIFI:  return "WiFi";
        default:           return "Unknown";
    }
}

void nextMode() {
    uint8_t next = (uint8_t)currentMode + 1;
    if (next >= MODE_COUNT) {
        next = 0;
    }
    setMode((VisualizerMode)next);
}

const char* getModeName(VisualizerMode mode) {
    switch (mode) {
        case MODE_WHITE_WAVEFORM:     return "White Waveform";
        case MODE_AUDIO_PARTICLES:    return "Particle";
        case MODE_RAINBOW_WAVE:       return "Rainbow Wave";
        case MODE_SOUND_RIPPLES:      return "Sound Ripples";
        case MODE_LAVA_LAMP:          return "Lava Lamp";
        case MODE_DIGITAL_RAIN:       return "Digital Rain";
        case MODE_SPECTRUM_LINEAR:    return "Linear Spectrum";
        case MODE_PULSING_TUNNEL:     return "Pulsing Tunnel";
        case MODE_MARIO_RUN:          return "Super Mario Run";
        default:                      return "Unknown";
    }
}

VisualizerMode getActiveMode() {
    return currentMode;
}

void setBrightness(uint8_t brightness) {
    currentBrightness = brightness;
}

uint8_t getBrightness() {
    return currentBrightness;
}

void setAutoCycle(bool enabled) {
    autoCycleEnabled = enabled;
}

bool getAutoCycle() {
    return autoCycleEnabled;
}

} // namespace LEDManager
