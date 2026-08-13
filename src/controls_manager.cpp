#include "controls_manager.h"
#include "project_config.h"
#include "led_manager.h"
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
    // State machine states
    #define R_START 0x0
    #define R_CW_FINAL 0x1
    #define R_CW_BEGIN 0x2
    #define R_CW_NEXT 0x3
    #define R_CCW_BEGIN 0x4
    #define R_CCW_FINAL 0x5
    #define R_CCW_NEXT 0x6

    #define DIR_CW 0x10
    #define DIR_CCW 0x20

    // Transition table for full-step encoder
    static const uint8_t ttable[7][4] = {
        // R_START
        {R_START,    R_CW_BEGIN,  R_CCW_BEGIN, R_START},
        // R_CW_FINAL
        {R_CW_NEXT,  R_START,     R_CW_FINAL,  R_START | DIR_CW},
        // R_CW_BEGIN
        {R_CW_NEXT,  R_CW_BEGIN,  R_START,     R_START},
        // R_CW_NEXT
        {R_CW_NEXT,  R_CW_BEGIN,  R_CW_FINAL,  R_START},
        // R_CCW_BEGIN
        {R_CCW_NEXT, R_START,     R_CCW_BEGIN, R_START},
        // R_CCW_FINAL
        {R_CCW_NEXT, R_CCW_FINAL, R_START,     R_START | DIR_CCW},
        // R_CCW_NEXT
        {R_CCW_NEXT, R_CCW_FINAL, R_CCW_BEGIN, R_START},
    };

    // Interrupt state variables
    volatile int rawEncoderDelta = 0;
    volatile uint8_t encoderState = R_START;
    
    // State variables for non-blocking HTTP sync
    static bool remoteUpdatePending = false;
    static uint32_t lastInteractionTime = 0;

    // Active gain setting stored locally to preserve HTTP payload structure
    static float activeGain = 1.0f;

    // Preview variables to defer selection changes until confirmed
    static VisualizerMode selectedModePreview = MODE_WHITE_WAVEFORM;
    static SourceMode selectedSourcePreview = SOURCE_SOUND;
    static uint8_t selectedBrightnessPreview = 0;
    static float selectedGainPreview = 1.0f;
    static bool selectedAutoCyclePreview = false;

    // Struct to hold sync payload
    struct SyncSettings {
        SourceMode source;
        VisualizerMode mode;
        uint8_t brightness;
        float gain;
        bool autoCycle;
    };

    static QueueHandle_t syncQueue = NULL;
    static TaskHandle_t syncTaskHandle = NULL;

    void syncTask(void* parameter) {
        SyncSettings settings;
        while (true) {
            if (xQueueReceive(syncQueue, &settings, portMAX_DELAY) == pdTRUE) {
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

                    String payload;
                    serializeJson(doc, payload);

                    Serial.printf("[Remote Task] Syncing to wall visualizer: %s\n", payload.c_str());

                    int httpCode = http.POST(payload);
                    if (httpCode > 0) {
                        Serial.printf("[Remote Task] HTTP POST response: %d\n", httpCode);
                    } else {
                        Serial.printf("[Remote Task] HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
                    }
                    http.end();
                } else {
                    Serial.println("[Remote Task] WiFi not connected, skipping sync.");
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

        if (xQueueOverwrite(syncQueue, &settings) != pdTRUE) {
            Serial.println("[Remote] Failed to write settings to syncQueue.");
        } else {
            Serial.println("[Remote] Queued async settings sync.");
        }
    }

    void IRAM_ATTR handleEncoderISR() {
        // Read current state of CLK and DT
        uint8_t pinState = (digitalRead(ENCODER_CLK_PIN) << 1) | digitalRead(ENCODER_DT_PIN);
        
        // Lookup next state
        encoderState = ttable[encoderState & 0x0f][pinState];
        
        // Check if we completed a rotation
        uint8_t result = encoderState & 0x30;
        if (result == DIR_CW) {
            rawEncoderDelta++;
        } else if (result == DIR_CCW) {
            rawEncoderDelta--;
        }
    }

    void init() {
        // Configure encoder pins with internal pullups
        pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
        pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
        pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

        // Initialize starting state
        encoderState = R_START;

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
        // 1. Read and clear encoder raw delta (each tick is exactly one detent click)
        int delta = 0;
        noInterrupts();
        delta = rawEncoderDelta;
        rawEncoderDelta = 0;
        interrupts();

        // Recover from missed interrupts (e.g., during FastLED.show() disabling interrupts)
        // by resetting the state machine to R_START when physical controls are at detent (both HIGH)
        if (digitalRead(ENCODER_CLK_PIN) == HIGH && digitalRead(ENCODER_DT_PIN) == HIGH) {
            noInterrupts();
            encoderState = R_START;
            interrupts();
        }

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
        int numItems = 5;
        if (LEDManager::getSource() == SOURCE_WIFI) {
            menuCursor = 1; // Force to Source Select (second item) when in WiFi mode
            numItems = 2; // Allow safe bounds containing index 1
        }
        if (menuCursor >= numItems) {
            menuCursor = 0;
        }

        // 3. Process inputs based on state machine
        if (currentState == STATE_NAV) {
            // Handle menu navigation
            if (delta != 0) {
                menuCursor += delta;
                // Clamp menu cursor between 0 and numItems - 1
                if (menuCursor < 0) menuCursor = numItems - 1;
                if (menuCursor >= numItems) menuCursor = 0;
                
                Serial.printf("[Controls] Menu cursor: %d\n", menuCursor);
            }

            if (swClicked) {
                currentState = STATE_EDIT;
                Serial.printf("[Controls] Entered EDIT mode for item %d\n", menuCursor);
                // Initialize preview values when entering edit mode
                switch (menuCursor) {
                    case 0: selectedModePreview = LEDManager::getActiveMode(); break;
                    case 1: selectedSourcePreview = LEDManager::getSource(); break;
                    case 2: selectedBrightnessPreview = LEDManager::getBrightness(); break;
                    case 3: selectedGainPreview = activeGain; break;
                    case 4: selectedAutoCyclePreview = LEDManager::getAutoCycle(); break;
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
                    case 1: { // Source Selection
                        SourceMode currentSrc = selectedSourcePreview;
                        SourceMode nextSrc = (currentSrc == SOURCE_SOUND) ? SOURCE_WIFI : SOURCE_SOUND;
                        selectedSourcePreview = nextSrc;
                        Serial.printf("[Controls] Source preview changed to: %s\n", LEDManager::getSourceName(nextSrc));
                        break;
                    }
                    case 2: { // Brightness (Step by 5%, 0% to 100%)
                        int currentPct = (int)round((selectedBrightnessPreview * 100.0) / 255.0);
                        int nextPct = currentPct + delta * 5;
                        if (nextPct < 0) nextPct = 0;
                        if (nextPct > 100) nextPct = 100;
                        
                        int b = (nextPct * 255) / 100;
                        selectedBrightnessPreview = (uint8_t)b;
                        Serial.printf("[Controls] Brightness preview changed to: %d%% (%d/255)\n", nextPct, b);
                        break;
                    }
                    case 3: { // Gain (Step by 0.2, 0.2 to 5.0)
                        float g = selectedGainPreview;
                        g += delta * 0.2f;
                        if (g < 0.2f) g = 0.2f;
                        if (g > 5.0f) g = 5.0f;
                        
                        selectedGainPreview = g;
                        Serial.printf("[Controls] Gain preview changed to: %.1f\n", g);
                        break;
                    }
                    case 4: { // Auto-Cycle (Toggle)
                        selectedAutoCyclePreview = !selectedAutoCyclePreview;
                        Serial.printf("[Controls] Auto-Cycle preview changed to: %s\n", selectedAutoCyclePreview ? "ON" : "OFF");
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
                        LEDManager::setSource(selectedSourcePreview);
                        break;
                    case 2:
                        LEDManager::setBrightness(selectedBrightnessPreview);
                        break;
                    case 3:
                        activeGain = selectedGainPreview;
                        break;
                    case 4:
                        LEDManager::setAutoCycle(selectedAutoCyclePreview);
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
        if (currentState == STATE_EDIT && menuCursor == 1) {
            return selectedSourcePreview;
        }
        return LEDManager::getSource();
    }

    uint8_t getBrightnessPreview() {
        if (currentState == STATE_EDIT && menuCursor == 2) {
            return selectedBrightnessPreview;
        }
        return LEDManager::getBrightness();
    }

    float getGainPreview() {
        if (currentState == STATE_EDIT && menuCursor == 3) {
            return selectedGainPreview;
        }
        return activeGain;
    }

    bool getAutoCyclePreview() {
        if (currentState == STATE_EDIT && menuCursor == 4) {
            return selectedAutoCyclePreview;
        }
        return LEDManager::getAutoCycle();
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
