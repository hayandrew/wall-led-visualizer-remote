#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <Arduino.h>
#include "led_manager.h"

namespace BLEManager {
    // Initialize the BLE client stack and start the scanning/connection background task
    void init();

    // Send updated settings to the Wall Panel over BLE
    void syncSettingsToPanel(SourceMode source, VisualizerMode mode, uint8_t brightness, float gain, bool autoCycle);

    // Return true if the client is connected to the server and has synchronized the initial state
    bool isConnectedAndSynced();
}

#endif // BLE_MANAGER_H
