#include "diyhue_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "led_manager.h"
#include "controls_manager.h"

namespace DiyHueManager {

struct LightState {
    bool on = true;
    uint8_t bri = 255;
    float x = 0.458f;
    float y = 0.410f;
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
};

static LightState lightState;
static WebServer server(80);
static bool newCommandReceived = false;

// Color space conversion helper functions
static void convertXyToRgb(float x, float y, uint8_t bri, uint8_t& r_out, uint8_t& g_out, uint8_t& b_out) {
    if (bri < 5) bri = 5;
    
    // CIE xyY to XYZ
    float Y = (float)bri / 255.0f;
    float X = 0.0f;
    float Z = 0.0f;
    
    if (y > 0.0f) {
        X = (Y / y) * x;
        Z = (Y / y) * (1.0f - x - y);
    }
    
    // sRGB D65 conversion matrix
    float r = X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    float g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    float b = X * 0.0557f - Y * 0.2040f + Z * 1.0570f;
    
    // Clip values to 0.0 - 1.0 range
    if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f; else if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f; else if (b > 1.0f) b = 1.0f;
    
    // Gamma correction
    r = (r <= 0.0031308f) ? 12.92f * r : 1.055f * pow(r, 1.0f / 2.4f) - 0.055f;
    g = (g <= 0.0031308f) ? 12.92f * g : 1.055f * pow(g, 1.0f / 2.4f) - 0.055f;
    b = (b <= 0.0031308f) ? 12.92f * b : 1.055f * pow(b, 1.0f / 2.4f) - 0.055f;
    
    r_out = (uint8_t)(r * 255.0f);
    g_out = (uint8_t)(g * 255.0f);
    b_out = (uint8_t)(b * 255.0f);
}

static CRGB convertCtToRgb(uint16_t mireds) {
    // CT is Mireds (153 to 500)
    // Scale linearly from warm candle/incandescent to cool daylight
    float factor = (float)(mireds - 153) / (500 - 153);
    if (factor < 0.0f) factor = 0.0f;
    if (factor > 1.0f) factor = 1.0f;
    
    // Cool white (high kelvin, more blue) to warm white (low kelvin, more red/yellow)
    uint8_t r = 255;
    uint8_t g = 255 - (uint8_t)(factor * 75);
    uint8_t b = 255 - (uint8_t)(factor * 150);
    return CRGB(r, g, b);
}

void init() {
    Serial.println("[diyHue] Initializing WebServer on port 80...");
    
    // Discovery Endpoint
    server.on("/detect", HTTP_GET, []() {
        DynamicJsonDocument doc(512);
        doc["name"] = "ESP32-C3 LED Matrix";
        doc["protocol"] = "native_single";
        doc["modelid"] = "LST002"; // Emulates a Hue Lightstrip Plus
        doc["type"] = "strip";
        doc["mac"] = WiFi.macAddress();
        doc["version"] = 1.0f;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
        Serial.println("[diyHue] Discovery scan received.");
    });
    
    // State Query Endpoint
    server.on("/state", HTTP_GET, []() {
        DynamicJsonDocument doc(512);
        doc["on"] = lightState.on;
        doc["bri"] = lightState.bri;
        doc["colormode"] = "xy";
        JsonArray xy = doc.createNestedArray("xy");
        xy.add(lightState.x);
        xy.add(lightState.y);
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // State Control Handler
    auto handlePutState = []() {
        String body = server.arg("plain");
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            server.send(400, "text/plain", "Invalid JSON body");
            Serial.printf("[diyHue] JSON parse error: %s\n", error.c_str());
            return;
        }
        
        Serial.printf("[diyHue] State update: %s\n", body.c_str());
        
        if (doc.containsKey("on")) {
            lightState.on = doc["on"];
        }
        if (doc.containsKey("bri")) {
            lightState.bri = doc["bri"];
        }
        
        if (doc.containsKey("xy")) {
            lightState.x = doc["xy"][0];
            lightState.y = doc["xy"][1];
            convertXyToRgb(lightState.x, lightState.y, lightState.bri, lightState.r, lightState.g, lightState.b);
        } else if (doc.containsKey("hue") && doc.containsKey("sat")) {
            uint16_t h = doc["hue"];
            uint8_t s = doc["sat"];
            uint8_t fastled_hue = (uint32_t)h * 255 / 65535;
            CHSV hsv(fastled_hue, s, lightState.bri);
            CRGB rgb;
            hsv2rgb_rainbow(hsv, rgb);
            lightState.r = rgb.r;
            lightState.g = rgb.g;
            lightState.b = rgb.b;
        } else if (doc.containsKey("ct")) {
            uint16_t ct = doc["ct"];
            CRGB rgb = convertCtToRgb(ct);
            lightState.r = (rgb.r * lightState.bri) / 255;
            lightState.g = (rgb.g * lightState.bri) / 255;
            lightState.b = (rgb.b * lightState.bri) / 255;
        } else {
            // If only 'on' or 'bri' was updated, recalculate with previous color coordinates
            convertXyToRgb(lightState.x, lightState.y, lightState.bri, lightState.r, lightState.g, lightState.b);
        }
        
        Serial.printf("[diyHue] Parsed RGB color: R=%d, G=%d, B=%d\n", lightState.r, lightState.g, lightState.b);
        
        newCommandReceived = true;
        server.send(200, "application/json", "{\"status\":\"success\"}");
    };
    
    server.on("/state", HTTP_PUT, handlePutState);
    server.on("/state", HTTP_POST, handlePutState);


    
    server.onNotFound([]() {
        server.send(404, "text/plain", "Not Found");
    });
    
    server.begin();
    Serial.println("[diyHue] Server started successfully.");
}

void update() {
    server.handleClient();
}

bool isOn() {
    return lightState.on;
}

void getRgbColor(uint8_t& r, uint8_t& g, uint8_t& b) {
    r = lightState.r;
    g = lightState.g;
    b = lightState.b;
}

uint8_t getBrightness() {
    return lightState.bri;
}

bool hasNewCommand() {
    return newCommandReceived;
}

void clearNewCommand() {
    newCommandReceived = false;
}

} // namespace DiyHueManager
