#include "controls_manager.h"
#include "project_config.h"
#include "led_manager.h"
#include "display_manager.h"
#include <cmath>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace ControlsManager {
    ControlState currentState = STATE_NAV;
    int menuCursor = 0;
    // Quadrature state variables
    volatile int rawEncoderDelta = 0;
    static volatile uint8_t lastEncoderState = 0;
    
    // State variables for non-blocking HTTP sync
    static bool remoteUpdatePending = false;
    static uint32_t lastInteractionTime = 0;

    static volatile bool connectionFailed = false;
    static volatile bool retryRequested = false;

    // Active gain setting stored locally to preserve HTTP payload structure
    static float activeGain = 1.0f;

    // Preview variables to defer selection changes until confirmed
    static VisualizerMode selectedModePreview = MODE_WHITE_WAVEFORM;
    static SourceMode selectedSourcePreview = SOURCE_SOUND;
    static uint8_t selectedBrightnessPreview = 0;
    static float selectedGainPreview = 1.0f;
    static bool selectedAutoCyclePreview = false;
    static bool systemOn = true;
    static bool selectedSystemOnPreview = true;

    // Struct to hold sync payload
    struct SyncSettings {
        SourceMode source;
        VisualizerMode mode;
        uint8_t brightness;
        float gain;
        bool autoCycle;
        bool systemOn;
    };

    static QueueHandle_t syncQueue = NULL;
    static TaskHandle_t syncTaskHandle = NULL;

    void syncTask(void* parameter) {
        SyncSettings settings;
        while (true) {
            if (xQueueReceive(syncQueue, &settings, portMAX_DELAY) == pdTRUE) {
                bool verified = false;
                
                while (!verified) {
                    if (WiFi.status() == WL_CONNECTED) {
                        HTTPClient http;
                        http.begin("http://192.168.68.55/api/settings");
                        http.addHeader("Content-Type", "application/json");
                        http.setTimeout(2000); // 2-second timeout in background

                        StaticJsonDocument<256> doc;
                        doc["source"] = LEDManager::getSourceName(settings.source);
                        doc["mode"] = (int)settings.mode;
                        doc["brightness"] = settings.brightness;
                        doc["gain"] = settings.gain;
                        doc["autoCycle"] = settings.autoCycle;
                        doc["on"] = settings.systemOn;

                        String payload;
                        serializeJson(doc, payload);

                        Serial.printf("[Remote Task] Syncing to wall visualizer: %s\n", payload.c_str());

                        int httpCode = http.POST(payload);
                        if (httpCode == HTTP_CODE_OK) {
                            Serial.printf("[Remote Task] HTTP POST response: %d (verified)\n", httpCode);
                            verified = true;
                        } else {
                            Serial.printf("[Remote Task] HTTP POST failed: %d\n", httpCode);
                        }
                        http.end();
                    } else {
                        Serial.println("[Remote Task] WiFi not connected on initial sync attempt.");
                    }

                    if (verified) {
                        break;
                    }

                    // Loop for 10 seconds, retrying every second
                    Serial.println("[Remote Task] Connection lost or not verified. Retrying every second for 10 seconds...");
                    for (int attempt = 1; attempt <= 10; attempt++) {
                        delay(1000);
                        
                        if (WiFi.status() != WL_CONNECTED) {
                            Serial.printf("[Remote Task] Reconnect attempt %d: WiFi not connected. Calling WiFi.begin...\n", attempt);
                            WiFi.begin(WIFI_SSID, WIFI_PASS);
                        }
                        
                        if (WiFi.status() == WL_CONNECTED) {
                            Serial.printf("[Remote Task] Reconnect attempt %d: WiFi connected. Sending package...\n", attempt);
                            HTTPClient http;
                            http.begin("http://192.168.68.55/api/settings");
                            http.addHeader("Content-Type", "application/json");
                            http.setTimeout(2000);

                            StaticJsonDocument<256> doc;
                            doc["source"] = LEDManager::getSourceName(settings.source);
                            doc["mode"] = (int)settings.mode;
                            doc["brightness"] = settings.brightness;
                            doc["gain"] = settings.gain;
                            doc["autoCycle"] = settings.autoCycle;
                            doc["on"] = settings.systemOn;

                            String payload;
                            serializeJson(doc, payload);

                            int httpCode = http.POST(payload);
                            if (httpCode == HTTP_CODE_OK) {
                                Serial.printf("[Remote Task] Package sent and verified on attempt %d.\n", attempt);
                                verified = true;
                                http.end();
                                break;
                            } else {
                                Serial.printf("[Remote Task] Send failed on attempt %d with code %d.\n", attempt, httpCode);
                            }
                            http.end();
                        }
                    }

                    if (!verified) {
                        Serial.println("[Remote Task] Failed to reconnect and verify within 10 seconds. Waiting for click to retry...");
                        DisplayManager::setFatalError("No connection.", "Click to retry.");
                        connectionFailed = true;
                        retryRequested = false;

                        while (!retryRequested) {
                            delay(100);
                        }

                        connectionFailed = false;
                        retryRequested = false;
                        DisplayManager::clearFatalError();
                        Serial.println("[Remote Task] Retry requested by user. Retrying...");
                    }
                }
            }
        }
    }

    void syncSettingsToRemote() {
        if (syncQueue == NULL) {
            Serial.println("[Remote] syncQueue is NULL, cannot sync settings.");
            return;
        }

        SyncSettings settings;
        settings.source = LEDManager::getSource();
        settings.mode = LEDManager::getActiveMode();
        settings.brightness = LEDManager::getBrightness();
        settings.gain = activeGain;
        settings.autoCycle = LEDManager::getAutoCycle();
        settings.systemOn = systemOn;

        if (xQueueOverwrite(syncQueue, &settings) != pdTRUE) {
            Serial.println("[Remote] Failed to write settings to syncQueue.");
        } else {
            Serial.println("[Remote] Queued async settings sync.");
        }
    }

    void IRAM_ATTR handleEncoderISR() {
        uint8_t clk = digitalRead(ENCODER_CLK_PIN);
        uint8_t dt = digitalRead(ENCODER_DT_PIN);
        uint8_t newState = (clk << 1) | dt;
        
        // Full 2-bit quadrature transition table
        // Index is (lastEncoderState << 2) | newState
        static const int8_t quad_states[16] = {
            0,  1, -1,  0,  // 00 -> 00, 01, 10, 11
           -1,  0,  0,  1,  // 01 -> 00, 01, 10, 11
            1,  0,  0, -1,  // 10 -> 00, 01, 10, 11
            0, -1,  1,  0   // 11 -> 00, 01, 10, 11
        };

        int8_t change = quad_states[(lastEncoderState << 2) | newState];
        if (change != 0) {
            rawEncoderDelta += change;
            lastEncoderState = newState;
        }
    }

    void init() {
        // Configure encoder pins with internal pullups
        pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
        pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
        pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

        // Initialize starting state to current physical state
        lastEncoderState = (digitalRead(ENCODER_CLK_PIN) << 1) | digitalRead(ENCODER_DT_PIN);
        rawEncoderDelta = 0;

        // Create FreeRTOS Queue for settings sync
        syncQueue = xQueueCreate(1, sizeof(SyncSettings));
        if (syncQueue != NULL) {
            // Create background task for syncing settings asynchronously
            xTaskCreatePinnedToCore(
                syncTask,
                "SyncSettingsTask",
                4096,
                NULL,
                1,              // Low priority so it doesn't interfere with UI or Audio
                &syncTaskHandle,
                0               // Pinned to core 0
            );
        } else {
            Serial.println("[Remote] Error creating sync settings queue!");
        }

        // Attach CHANGE interrupts to BOTH CLK and DT for full quadrature tracking
        attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), handleEncoderISR, CHANGE);
        attachInterrupt(digitalPinToInterrupt(ENCODER_DT_PIN), handleEncoderISR, CHANGE);
    }

    void update() {
        if (connectionFailed) {
            // Read SW switch press with debouncing
            static bool lastSwState = HIGH;
            static uint32_t lastSwDebounceTime = 0;
            bool currentSwState = digitalRead(ENCODER_SW_PIN);
            bool swClicked = false;

            if (currentSwState != lastSwState) {
                if (millis() - lastSwDebounceTime > 50) {
                    if (currentSwState == LOW) {
                        swClicked = true;
                        Serial.println("[Controls] Encoder switch clicked during connection error.");
                    }
                    lastSwState = currentSwState;
                    lastSwDebounceTime = millis();
                }
            }

            if (swClicked) {
                retryRequested = true;
            }
            return;
        }

        // 1. Read and clear encoder raw delta
        int delta = 0;
        int rawDelta = 0;
        static int lastProcessedRawDelta = 0;
        bool rawDeltaChanged = false;

        noInterrupts();
        rawDelta = rawEncoderDelta;
        if (rawDelta != lastProcessedRawDelta) {
            rawDeltaChanged = true;
            rawEncoderDelta = rawDelta % 4;
            lastProcessedRawDelta = rawEncoderDelta;
        }
        interrupts();

        if (rawDeltaChanged) {
            delta = rawDelta / 4;
            
            // Check if the physical encoder is at the detent rest position (both pins HIGH)
            bool isAtDetent = (digitalRead(ENCODER_CLK_PIN) == HIGH && digitalRead(ENCODER_DT_PIN) == HIGH);
            if (isAtDetent) {
                int remainder = rawDelta % 4;
                // If we returned to detent with a significant partial turn (3 steps),
                // round it up/down to register the click.
                if (remainder >= 3) {
                    delta += 1;
                } else if (remainder <= -3) {
                    delta -= 1;
                }
            }

            if (delta != 0) {
                // Clear any leftover remainder since a step was registered
                noInterrupts();
                rawEncoderDelta = 0;
                lastProcessedRawDelta = 0;
                interrupts();
            }

            if (rawDelta != 0) {
                Serial.printf("[Controls] Encoder activity: rawDelta = %d, delta = %d, pins = %d%d\n", 
                              rawDelta, delta, digitalRead(ENCODER_CLK_PIN), digitalRead(ENCODER_DT_PIN));
            }
        }

        // Ben Buxton's state machine naturally self-recovers and resets to R_START 
        // when both pins return to detent (HIGH/HIGH). Polling and resetting here 
        // can prematurely clear transitions and cause missed steps.

        // 2. Read SW switch press with debouncing
        static bool lastSwState = HIGH;
        static uint32_t lastSwDebounceTime = 0;
        bool currentSwState = digitalRead(ENCODER_SW_PIN);
        bool swClicked = false;

        if (currentSwState != lastSwState) {
            if (millis() - lastSwDebounceTime > 50) {
                if (currentSwState == LOW) {
                    swClicked = true;
                    Serial.println("[Controls] Encoder switch clicked.");
                }
                lastSwState = currentSwState;
                lastSwDebounceTime = millis();
            }
        }

        // Determine number of items based on source
        int numItems = 6;
        if (LEDManager::getSource() == SOURCE_WIFI) {
            numItems = 6;
            if (menuCursor != 4 && menuCursor != 5) {
                menuCursor = 5; // default to Power on WiFi mode
            }
        }
        if (menuCursor >= numItems) {
            menuCursor = 0;
        }

        // 3. Process inputs based on state machine
        if (currentState == STATE_NAV) {
            // Handle menu navigation
            if (delta != 0) {
                if (LEDManager::getSource() != SOURCE_WIFI) {
                    menuCursor += delta;
                    if (menuCursor < 0) menuCursor = numItems - 1;
                    if (menuCursor >= numItems) menuCursor = 0;
                } else {
                    // In WiFi mode, only allow toggling between 4 (Source) and 5 (Power)
                    menuCursor += delta;
                    if (menuCursor < 4) menuCursor = 5;
                    if (menuCursor > 5) menuCursor = 4;
                }
                Serial.printf("[Controls] Menu cursor: %d\n", menuCursor);
            }

            if (swClicked) {
                currentState = STATE_EDIT;
                Serial.printf("[Controls] Entered EDIT mode for item %d\n", menuCursor);
                // Initialize preview values when entering edit mode
                switch (menuCursor) {
                    case 0: selectedModePreview = LEDManager::getActiveMode(); break;
                    case 1: selectedAutoCyclePreview = LEDManager::getAutoCycle(); break;
                    case 2: selectedGainPreview = activeGain; break;
                    case 3: selectedBrightnessPreview = LEDManager::getBrightness(); break;
                    case 4: selectedSourcePreview = LEDManager::getSource(); break;
                    case 5: selectedSystemOnPreview = systemOn; break;
                }
            }
        } else if (currentState == STATE_EDIT) {
            // Handle editing values
            if (delta != 0) {
                switch (menuCursor) {
                    case 0: { // Mode Selection
                        int currentModeInt = (int)selectedModePreview;
                        currentModeInt += delta;
                        if (currentModeInt < 0) currentModeInt = (int)MODE_COUNT - 1;
                        if (currentModeInt >= (int)MODE_COUNT) currentModeInt = 0;
                        
                        selectedModePreview = (VisualizerMode)currentModeInt;
                        Serial.printf("[Controls] Mode preview changed to: %s\n", LEDManager::getModeName(selectedModePreview));
                        break;
                    }
                    case 1: { // Auto-Cycle (Toggle)
                        selectedAutoCyclePreview = !selectedAutoCyclePreview;
                        Serial.printf("[Controls] Auto-Cycle preview changed to: %s\n", selectedAutoCyclePreview ? "ON" : "OFF");
                        break;
                    }
                    case 2: { // Gain (Step by 0.1, 0.2 to 2.0)
                        float g = selectedGainPreview;
                        g -= delta * 0.1f;
                        if (g < 0.2f) g = 0.2f;
                        if (g > 2.0f) g = 2.0f;
                        
                        selectedGainPreview = g;
                        Serial.printf("[Controls] Gain preview changed to: %.1f\n", g);
                        break;
                    }
                    case 3: { // Brightness (Step by 5%, 0% to 100%)
                        int currentPct = (int)round((selectedBrightnessPreview * 100.0) / 255.0);
                        int nextPct = currentPct - delta * 5;
                        if (nextPct < 0) nextPct = 0;
                        if (nextPct > 100) nextPct = 100;
                        
                        int b = (nextPct * 255) / 100;
                        selectedBrightnessPreview = (uint8_t)b;
                        Serial.printf("[Controls] Brightness preview changed to: %d%% (%d/255)\n", nextPct, b);
                        break;
                    }
                    case 4: { // Source Selection
                        SourceMode currentSrc = selectedSourcePreview;
                        SourceMode nextSrc = (currentSrc == SOURCE_SOUND) ? SOURCE_WIFI : SOURCE_SOUND;
                        selectedSourcePreview = nextSrc;
                        Serial.printf("[Controls] Source preview changed to: %s\n", LEDManager::getSourceName(nextSrc));
                        break;
                    }
                    case 5: { // Power Selection (Toggle)
                        selectedSystemOnPreview = !selectedSystemOnPreview;
                        Serial.printf("[Controls] Power preview changed to: %s\n", selectedSystemOnPreview ? "ON" : "OFF");
                        break;
                    }
                }
            }

            if (swClicked) {
                currentState = STATE_NAV;
                Serial.println("[Controls] Returned to NAV mode.");
                
                // Commit preview values to active settings
                switch (menuCursor) {
                    case 0:
                        LEDManager::setMode(selectedModePreview);
                        break;
                    case 1:
                        LEDManager::setAutoCycle(selectedAutoCyclePreview);
                        break;
                    case 2:
                        activeGain = selectedGainPreview;
                        break;
                    case 3:
                        LEDManager::setBrightness(selectedBrightnessPreview);
                        break;
                    case 4:
                        LEDManager::setSource(selectedSourcePreview);
                        break;
                    case 5:
                        systemOn = selectedSystemOnPreview;
                        break;
                }
                
                // Immediately sync to the remote wall visualizer on confirmation
                syncSettingsToRemote();
                remoteUpdatePending = false;
            }
        }

        // Handle deferred non-blocking sync after 300ms of inactivity
        if (remoteUpdatePending && (millis() - lastInteractionTime > 300)) {
            syncSettingsToRemote();
            remoteUpdatePending = false;
        }
    }

    ControlState getState() {
        return currentState;
    }

    int getMenuCursor() {
        return menuCursor;
    }

    bool isEditing() {
        return (currentState == STATE_EDIT);
    }

    VisualizerMode getVisualizerModePreview() {
        if (currentState == STATE_EDIT && menuCursor == 0) {
            return selectedModePreview;
        }
        return LEDManager::getActiveMode();
    }

    SourceMode getSourcePreview() {
        if (currentState == STATE_EDIT && menuCursor == 4) {
            return selectedSourcePreview;
        }
        return LEDManager::getSource();
    }

    uint8_t getBrightnessPreview() {
        if (currentState == STATE_EDIT && menuCursor == 3) {
            return selectedBrightnessPreview;
        }
        return LEDManager::getBrightness();
    }

    float getGainPreview() {
        if (currentState == STATE_EDIT && menuCursor == 2) {
            return selectedGainPreview;
        }
        return activeGain;
    }

    bool getAutoCyclePreview() {
        if (currentState == STATE_EDIT && menuCursor == 1) {
            return selectedAutoCyclePreview;
        }
        return LEDManager::getAutoCycle();
    }

    bool getSystemOnPreview() {
        if (currentState == STATE_EDIT && menuCursor == 5) {
            return selectedSystemOnPreview;
        }
        return systemOn;
    }

    bool fetchStateFromRemote() {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Remote] WiFi not connected, cannot fetch state.");
            return false;
        }

        Serial.println("[Remote] Fetching initial state from wall visualizer...");
        HTTPClient http;
        http.begin("http://192.168.68.55/api/status");
        http.setTimeout(2000); // 2-second timeout

        bool success = false;
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.printf("[Remote] Status payload: %s\n", payload.c_str());

            StaticJsonDocument<1024> doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                if (doc.containsKey("source")) {
                    String src = doc["source"];
                    if (src == "Sound") {
                        LEDManager::setSource(SOURCE_SOUND);
                    } else if (src == "WiFi") {
                        LEDManager::setSource(SOURCE_WIFI);
                    }
                }
                if (doc.containsKey("mode")) {
                    int m = doc["mode"];
                    LEDManager::setMode((VisualizerMode)m);
                }
                if (doc.containsKey("brightness")) {
                    uint8_t b = doc["brightness"];
                    LEDManager::setBrightness(b);
                }
                if (doc.containsKey("gain")) {
                    activeGain = doc["gain"];
                }
                if (doc.containsKey("autoCycle")) {
                    bool ac = doc["autoCycle"];
                    LEDManager::setAutoCycle(ac);
                }
                if (doc.containsKey("on")) {
                    systemOn = doc["on"];
                }
                Serial.println("[Remote] Initial state fetched and applied successfully.");
                success = true;
            } else {
                Serial.printf("[Remote] Failed to parse status JSON: %s\n", error.c_str());
            }
        } else {
            Serial.printf("[Remote] Failed to fetch status, HTTP code: %d\n", httpCode);
        }
        http.end();
        return success;
    }

    void setGain(float gain) {
        activeGain = gain;
    }
}
