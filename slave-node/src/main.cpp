#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "CameraManager.h"
#include "CloudManager.h"

// Initialize Managers
CameraManager cameraManager;
CloudManager cloudManager;

bool isWifiConnected = false;
unsigned long lastCaptureTime = 0;

void connectWiFi(const char* ssid, const char* pass) {
    Serial.printf("[INFO] Connecting to WiFi: %s\n", ssid);
    WiFi.begin(ssid, pass);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[INFO] WiFi Connected!");
        Serial.print("[INFO] IP Address: ");
        Serial.println(WiFi.localIP());
        isWifiConnected = true;
    } 
    else {
        Serial.println("\n[ERROR] WiFi connection failed!");
    }
}

// Function to handle capturing and sending images
void handleCapture(String caption) {
    if (millis() - lastCaptureTime < COOLDOWN_TIME) {
        Serial.println("[WARN] In cooldown period (15s). Skipping continuous capture request.");
        return;
    }

    Serial.println("[INFO] Starting image capture...");
    
    cameraManager.setFlash(true);
    delay(500);     // Wait 0.5s for the flash to evenly illuminate the scene

    cameraManager.clearBuffer();            // Discard old frame
    camera_fb_t* fb = esp_camera_fb_get();  // Capture new frame

    if (fb) {
        // Send to cloud APIs
        cloudManager.sendTelegramPhoto(fb, caption);
        cloudManager.sendAWS(fb);
        
        esp_camera_fb_return(fb);   // Free up RAM
    } 
    else {
        Serial.println("[ERROR] Failed to capture image from camera!");
    }

    cameraManager.setFlash(false);
    lastCaptureTime = millis();
}

void setup() {
    // UART0: Communication with the computer
    Serial.begin(115200);
    
    // UART1: Communication with Master Node
    Serial1.begin(115200, SERIAL_8N1, PIN_UART1_RX, PIN_UART1_TX);
    
    Serial.println("\n=================================");
    Serial.println("   SLAVE NODE (S3-CAM) INITIALIZED");
    Serial.println("=================================");

    if (!cameraManager.begin()) {
        Serial.println("[ERROR] Camera initialization failed. Please check the hardware!");
    }

    Serial.println("[INFO] Waiting for WiFi configuration from Master Node...");
}

void loop() {
    // Read data from Master Node
    if (Serial1.available() > 0) {
        String data = Serial1.readStringUntil('\n');
        data.trim();

        if (data.length() > 0) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, data);

            if (!error) {
                const char* cmd = doc["cmd"] | "";

                // 1. Command to receive WiFi credentials from Master
                if (strcmp(cmd, "WIFI") == 0) {
                    const char* ssid = doc["ssid"] | "";
                    const char* pass = doc["pass"] | "";

                    if (!isWifiConnected) {
                        connectWiFi(ssid, pass);
                    }
                }
                
                // 2. Cloud interaction commands (only run when WiFi is available)
                else if (isWifiConnected) {
                    if (strcmp(cmd, "ALERT") == 0) {
                        const char* type = doc["type"] | "UNKNOWN";
                        int gas = doc["gas"] | 0;
                        
                        String caption = "🚨 *ALERT DETECTED!* 🚨\n🔥 Type: " + String(type) + "\n💨 Gas concentration: " + String(gas) + " PPM";
                        Serial.println("[ALERT] Received alert command from Master!");
                        handleCapture(caption);
                    }
                    else if (strcmp(cmd, "SNAPSHOT") == 0) {
                        Serial.println("[INFO] Received manual capture command from Keypad!");
                        handleCapture("📸 *MANUAL CAPTURE FROM KEYPAD*");
                    }
                    else if (strcmp(cmd, "SAFE") == 0) {
                        // Receive heartbeat from Master, can be printed or ignored to reduce Serial clutter
                    }
                } 
                else {
                    Serial.println("[WARN] Received control command but the device has no WiFi connection!");
                }
            } 
            else {
                Serial.print("[ERROR] Failed to parse JSON from Master: ");
                Serial.println(error.f_str());
            }
        }
    }
}