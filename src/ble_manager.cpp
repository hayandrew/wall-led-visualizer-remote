#include "ble_manager.h"
#include <NimBLEDevice.h>
#include "project_config.h"
#include "led_manager.h"
#include "controls_manager.h"

struct __attribute__((__packed__)) BLEControlPayload {
    uint8_t source;      // 0 = Sound, 1 = WiFi
    uint8_t mode;        // VisualizerMode
    uint8_t brightness;  // 0 - 255
    float gain;          // 0.1 - 5.0
    uint8_t autoCycle;   // 0 = Off, 1 = On
};

namespace BLEManager {
    static NimBLEAdvertisedDevice* advDevice = nullptr;
    static NimBLEClient* pClient = nullptr;
    static NimBLERemoteCharacteristic* pRemoteChar = nullptr;
    
    static volatile bool connected = false;
    static volatile bool synced = false;
    static volatile bool doConnect = false;

    // Apply payload values locally
    static void applyPayloadLocally(const BLEControlPayload& payload) {
        Serial.printf("[BLE Client] Applying remote state - Source: %d, Mode: %d, Brightness: %d, Gain: %.2f, AutoCycle: %d\n",
                      payload.source, payload.mode, payload.brightness, payload.gain, payload.autoCycle);
        
        LEDManager::setSource((SourceMode)payload.source);
        LEDManager::setMode((VisualizerMode)payload.mode);
        LEDManager::setBrightness(payload.brightness);
        ControlsManager::setGain(payload.gain);
        LEDManager::setAutoCycle(payload.autoCycle);
    }

    // Callback when characteristic notification is received
    static void notifyCallback(
        NimBLERemoteCharacteristic* pBLERemoteCharacteristic,
        uint8_t* pData,
        size_t length,
        bool isNotify
    ) {
        if (length == sizeof(BLEControlPayload)) {
            BLEControlPayload payload;
            memcpy(&payload, pData, sizeof(BLEControlPayload));
            applyPayloadLocally(payload);
        } else {
            Serial.printf("[BLE Client] Received invalid notification size: %u\n", (unsigned int)length);
        }
    }

    class ClientCallbacks : public NimBLEClientCallbacks {
        void onConnect(NimBLEClient* pCl) override {
            Serial.println("[BLE Client] Connected to server.");
        }

        void onDisconnect(NimBLEClient* pCl) override {
            connected = false;
            synced = false;
            pRemoteChar = nullptr;
            Serial.println("[BLE Client] Disconnected from server. Scanning will resume.");
        }
    };

    class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
        void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
            if (advertisedDevice->isAdvertisingService(NimBLEUUID(BLE_SERVICE_UUID)) || 
                advertisedDevice->getName() == BLE_DEVICE_NAME) {
                
                Serial.println("[BLE Client] Target panel device found!");
                NimBLEDevice::getScan()->stop();
                advDevice = advertisedDevice;
                doConnect = true;
            }
        }
    };

    // Main connection logic
    bool connectToServer() {
        if (advDevice == nullptr) return false;

        Serial.print("[BLE Client] Forming a connection to ");
        Serial.println(advDevice->getAddress().toString().c_str());

        if (pClient == nullptr) {
            pClient = NimBLEDevice::createClient();
            pClient->setClientCallbacks(new ClientCallbacks(), false);
        }

        if (!pClient->connect(advDevice)) {
            Serial.println("[BLE Client] Failed to connect.");
            return false;
        }

        Serial.println("[BLE Client] Connected to server. Retrieving service...");
        NimBLERemoteService* pRemoteService = pClient->getService(BLE_SERVICE_UUID);
        if (pRemoteService == nullptr) {
            Serial.println("[BLE Client] Failed to find our service UUID.");
            pClient->disconnect();
            return false;
        }

        Serial.println("[BLE Client] Service found. Retrieving characteristic...");
        pRemoteChar = pRemoteService->getCharacteristic(BLE_CHARACTERISTIC_UUID);
        if (pRemoteChar == nullptr) {
            Serial.println("[BLE Client] Failed to find our characteristic UUID.");
            pClient->disconnect();
            return false;
        }

        // Read initial state
        if (pRemoteChar->canRead()) {
            std::string value = pRemoteChar->readValue();
            if (value.length() == sizeof(BLEControlPayload)) {
                BLEControlPayload payload;
                memcpy(&payload, value.data(), sizeof(BLEControlPayload));
                applyPayloadLocally(payload);
                synced = true;
                Serial.println("[BLE Client] Initial state successfully synchronized.");
            } else {
                Serial.println("[BLE Client] Failed to parse read value.");
            }
        }

        // Subscribe to notifications
        if (pRemoteChar->canNotify()) {
            if (pRemoteChar->subscribe(true, notifyCallback)) {
                Serial.println("[BLE Client] Subscribed to notifications.");
            } else {
                Serial.println("[BLE Client] Notification subscription failed.");
            }
        }

        connected = true;
        return true;
    }

    void bleTask(void* parameter) {
        while (true) {
            if (!connected && !doConnect) {
                Serial.println("[BLE Client] Starting scan...");
                NimBLEDevice::getScan()->start(5, false); // scan for 5 seconds
            }

            if (doConnect) {
                doConnect = false;
                connectToServer();
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    void init() {
        Serial.println("[BLE Client] Initializing NimBLE Client...");
        NimBLEDevice::init("");
        
        NimBLEScan* pBLEScan = NimBLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(), false);
        pBLEScan->setInterval(45);
        pBLEScan->setWindow(15);
        pBLEScan->setActiveScan(true);

        xTaskCreatePinnedToCore(
            bleTask,
            "BLEClientTask",
            4096,
            NULL,
            1, // Low priority
            NULL,
            0 // Pin to core 0
        );
    }

    void syncSettingsToPanel(SourceMode source, VisualizerMode mode, uint8_t brightness, float gain, bool autoCycle) {
        if (!connected || pRemoteChar == nullptr) {
            Serial.println("[BLE Client] Cannot sync: Not connected to panel.");
            return;
        }

        BLEControlPayload payload;
        payload.source = (uint8_t)source;
        payload.mode = (uint8_t)mode;
        payload.brightness = brightness;
        payload.gain = gain;
        payload.autoCycle = (uint8_t)autoCycle;

        bool success = pRemoteChar->writeValue((uint8_t*)&payload, sizeof(BLEControlPayload), true); // response = true

        if (success) {
            Serial.println("[BLE Client] Settings update successfully written to panel.");
        } else {
            Serial.println("[BLE Client] Failed to write settings update to panel.");
        }
    }

    bool isConnectedAndSynced() {
        return connected && synced;
    }
}
