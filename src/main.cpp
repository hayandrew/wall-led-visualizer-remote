#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "led_manager.h"
#include "controls_manager.h"
#include "display_manager.h"
#include "diyhue_manager.h"

WiFiServer telnetServer(23);
WiFiClient telnetClient;
TelnetLogger telnetLogger;

static bool waitAndCheckBypass(int ms) {
  int steps = ms / 50;
  for (int i = 0; i < steps; i++) {
    if (digitalRead(ENCODER_SW_PIN) == LOW) {
      return true;
    }
    delay(50);
  }
  return false;
}

void setup() {
  // Initialize Serial logging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-C3 LED Visualizer Starting ===");

  // Initialize display first to show "Connecting..." during Wi-Fi setup
  DisplayManager::init();
  DisplayManager::drawBootStatus("Connecting...");

  // Configure static IP to speed up connection and match reference setup
  WiFi.mode(WIFI_STA);
  IPAddress local_IP(192, 168, 68, 50);
  IPAddress gateway(192, 168, 68, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("[WiFi] Static IP configuration failed!");
  }

  // Initialize SW pin for bypass option
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  // Connect to home Wi-Fi using preprocessor macros injected by load_env.py
  #if defined(WIFI_SSID) && defined(WIFI_PASS)
    Serial.printf("[WiFi] Connecting to SSID: %s...\n", WIFI_SSID);
  #else
    #error "WIFI_SSID and WIFI_PASS must be defined in .env!"
  #endif

  bool bypassed = false;
  int retries = 0;
  
  while (WiFi.status() != WL_CONNECTED && !bypassed) {
    if (retries > 0) {
      Serial.println("\n[WiFi] Connection Failed. Retrying...");
      DisplayManager::drawBootStatus("Connection Failed", "Retrying...");
      if (waitAndCheckBypass(2000)) {
        bypassed = true;
        break;
      }
    }
    
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // Wait for connection with a timeout of 10 seconds (5 checks * 2 seconds)
    int check = 0;
    while (WiFi.status() != WL_CONNECTED && check < 5) {
      if (waitAndCheckBypass(2000)) {
        bypassed = true;
        break;
      }
      check++;
      retries++;
      char buf[64];
      sprintf(buf, "Attempt %d (Click Knob to Skip)", retries);
      DisplayManager::drawBootStatus("Connecting...", buf);
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    DisplayManager::drawBootStatus("WiFi Connected", WiFi.localIP().toString().c_str());
    delay(1000);
  } else {
    Serial.println("\n[WiFi] Operating offline.");
    DisplayManager::drawBootStatus("Connection Skipped", "Operating Offline");
    delay(1500);
  }

  // Set up OTA port (default is 3232, but standard is fine)
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("esp32c3-visualizer");

  // Configure ArduinoOTA event callbacks
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("[OTA] Start updating " + type);
    DisplayManager::drawOtaProgress(0, 100);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] End of update. Rebooting...");
    DisplayManager::drawOtaProgress(100, 100);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
    DisplayManager::drawOtaProgress(progress, total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error[%u]: ", error);
    const char* msg = "Unknown Error";
    if (error == OTA_AUTH_ERROR) { Serial.println("Auth Failed"); msg = "Auth Failed"; }
    else if (error == OTA_BEGIN_ERROR) { Serial.println("Begin Failed"); msg = "Begin Failed"; }
    else if (error == OTA_CONNECT_ERROR) { Serial.println("Connect Failed"); msg = "Connect Failed"; }
    else if (error == OTA_RECEIVE_ERROR) { Serial.println("Receive Failed"); msg = "Receive Failed"; }
    else if (error == OTA_END_ERROR) { Serial.println("End Failed"); msg = "End Failed"; }
    DisplayManager::drawOtaError(msg);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] OTA Services Ready.");



  // Initialize LEDs via Manager
  DisplayManager::drawBootStatus("Initializing...", "LED Manager");
  LEDManager::init();

  // Initialize physical controls
  DisplayManager::drawBootStatus("Initializing...", "Controls");
  ControlsManager::init();

  // Synchronize settings with the main LED panel on boot
  if (WiFi.status() == WL_CONNECTED) {
    bool syncSuccess = false;
    
    while (!syncSuccess) {
      unsigned long startTime = millis();
      int attempt = 0;
      
      while (!syncSuccess && (millis() - startTime < 10000)) {
        attempt++;
        char detailBuf[64];
        sprintf(detailBuf, "Attempt %d...", attempt);
        DisplayManager::drawBootStatus("Connecting to Panel", detailBuf);
        
        syncSuccess = ControlsManager::fetchStateFromRemote();
        if (syncSuccess) {
          DisplayManager::drawBootStatus("Panel Synced", "Settings Applied");
          delay(1000);
          break;
        } else {
          Serial.println("[Remote] Panel sync failed. Retrying in 1s...");
          delay(1000);
        }
      }
      
      if (!syncSuccess) {
        DisplayManager::drawBootStatus("Ensure the LED panel", "is on. Click to retry");
        Serial.println("[Remote] Panel sync failed after 10s. Waiting for dial click to retry...");
        
        // Wait for rotary dial switch click
        bool clicked = false;
        while (!clicked) {
          if (digitalRead(ENCODER_SW_PIN) == LOW) {
            clicked = true;
            delay(200); // Debounce
            while (digitalRead(ENCODER_SW_PIN) == LOW) {
              delay(10);
            }
          }
          delay(50);
        }
      }
    }
  }

  // Initialize diyHue Network Client
  DisplayManager::drawBootStatus("Initializing...", "diyHue Server");
  DiyHueManager::init();

  // Start Telnet Server
  telnetServer.begin();
  telnetServer.setNoDelay(true);

  DisplayManager::drawBootStatus("Boot Complete");
  delay(500);

  Serial.println("=== Setup Complete. Entering loop ===\n");
}

void loop() {
  // Check for incoming Telnet connections
  if (telnetServer.hasClient()) {
    if (telnetClient && telnetClient.connected()) {
      telnetClient.stop();
    }
    telnetClient = telnetServer.available();
    telnetClient.println("\n=== Connected to ESP32 OTA Serial Monitor ===");
  }

  // Handle OTA update check
  ArduinoOTA.handle();

  // Update physical controls state and OLED display drawing
  ControlsManager::update();
  DisplayManager::update();

  // Update diyHue client and handle automatic mode switching on command
  DiyHueManager::update();
  if (DiyHueManager::hasNewCommand()) {
      DiyHueManager::clearNewCommand();
      LEDManager::setSource(SOURCE_WIFI);
  }



  // 2. Auto-cycle visualizer modes every 5 seconds (if enabled and source is Sound)
  static unsigned long lastModeSwitch = millis();
  static VisualizerMode lastActiveMode = LEDManager::getActiveMode();
  VisualizerMode currentMode = LEDManager::getActiveMode();

  if (currentMode != lastActiveMode) {
    lastActiveMode = currentMode;
    lastModeSwitch = millis();
  }

  if (LEDManager::getSource() == SOURCE_SOUND && LEDManager::getAutoCycle() && (millis() - lastModeSwitch >= 5000)) {
    lastModeSwitch = millis();
    LEDManager::nextMode();
  }

  // 3. Handle Serial commands to switch modes manually (if source is Sound)
  if (Serial.available()) {
    char c = Serial.read();
    if ((c == 'n' || c == ' ') && LEDManager::getSource() == SOURCE_SOUND) {
      LEDManager::nextMode();
      lastModeSwitch = millis();
    } else if (c >= '0' && c <= '6') {
      LEDManager::setMode((VisualizerMode)(c - '0'));
      lastModeSwitch = millis();
    }
  }

  // 4. Update the visualizer animation frame
  LEDManager::update();

  // Yield to keep the Wi-Fi/IP stack healthy
  delay(10);
}
