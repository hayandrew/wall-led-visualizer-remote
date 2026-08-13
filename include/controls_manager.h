#ifndef CONTROLS_MANAGER_H
#define CONTROLS_MANAGER_H

#include <Arduino.h>
#include "led_manager.h"

namespace ControlsManager {
    enum ControlState {
        STATE_NAV,  // Moving the menu cursor
        STATE_EDIT  // Editing a selected parameter value
    };

    // Initialize pins and interrupts for the rotary encoder and switch
    void init();

    // Read input states and update menu selection/parameter values
    void update();

    // Fetch the initial state from the remote ESP32-S3 and update local parameters
    void fetchStateFromRemote();

    // Accessors for UI rendering
    ControlState getState();
    int getMenuCursor();
    bool isEditing();
    
    // Preview values during edit mode
    VisualizerMode getVisualizerModePreview();
    SourceMode getSourcePreview();
    uint8_t getBrightnessPreview();
    float getGainPreview();
    bool getAutoCyclePreview();
}

#endif // CONTROLS_MANAGER_H
