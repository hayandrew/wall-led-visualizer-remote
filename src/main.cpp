#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "project_config.h"
#include "led_manager.h"
#include "audio_processor.h"
#include "controls_manager.h"
#include "display_manager.h"
#include "diyhue_manager.h"

void setup() {
  // Initialize Serial logging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-C3 LED Visualizer Starting ===");

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

  // Connect to home Wi-Fi using preprocessor macros injected by load_env.py
  #if defined(WIFI_SSID) && defined(WIFI_PASS)
    Serial.printf("[WiFi] Connecting to SSID: %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  #else
    #error "WIFI_SSID and WIFI_PASS must be defined in .env!"
  #endif

  // Wait for connection with a timeout
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Connection failed! Operating offline.");
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

  // Initialize I2S Audio Processor
  AudioProcessor::init();

  // Initialize LEDs via Manager
  LEDManager::init();

  // Initialize physical controls and display menu
  ControlsManager::init();
  DisplayManager::init();

  // Synchronize remote settings on boot
  if (WiFi.status() == WL_CONNECTED) {
    ControlsManager::fetchStateFromRemote();
  }

  // Initialize diyHue Network Client
  DiyHueManager::init();

  Serial.println("=== Setup Complete. Entering loop ===\n");
}

void loop() {
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

  // 1. Run FFT calculation if background I2S buffer is filled
  if (AudioProcessor::isNewBufferReady()) {
    AudioProcessor::runFFT();
    AudioProcessor::clearNewBufferFlag();
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

  // Print volume statistics to Serial Monitor every 500ms
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    Serial.printf("[Audio] Peak: %.2f | Env: %.2f | Active Mode: %s\n", 
                  AudioProcessor::getPeakAmplitude(), 
                  AudioProcessor::getVolumeEnvelope(),
                  LEDManager::getModeName(LEDManager::getActiveMode()));
  }

  // Yield to keep the Wi-Fi/IP stack healthy
  delay(10);
}
