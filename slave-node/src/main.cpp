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
unsigned long lastWifiCheckTime = 0;

String localSSID = "";
String localPASS = "";

String localBotToken = "";
String localChatId = "";

const int FLASH_LED_PIN = 4;

// Helper function to control the flash LED blinking a predefined number of times to indicate system status
void blinkFlash(int times, int durationMs) 
{
    pinMode(FLASH_LED_PIN, OUTPUT);

    for (int i = 0; i < times; i++) 
    {
        digitalWrite(FLASH_LED_PIN, HIGH); // Flash ON
        delay(durationMs);
        digitalWrite(FLASH_LED_PIN, LOW);  // Flash OFF

        if (times > 1) 
        {
            delay(durationMs);
        }
    }
}

void connectWiFi(const char* ssid, const char* pass) 
{
    Serial.printf("[INFO] Connecting to WiFi: %s\n", ssid);
    
    if (WiFi.status() == WL_CONNECTED) 
    {
        WiFi.disconnect();
    }
    
    WiFi.begin(ssid, pass);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) 
    {
        delay(500);

        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) 
    {
        Serial.println("\n[INFO] WiFi Connected!");
        Serial.print("[INFO] IP Address: ");
        Serial.println(WiFi.localIP());
        isWifiConnected = true;
        
        // Double-blink the flash LED to indicate a successful connection
        blinkFlash(2, 150); 
    } 
    else 
    {
        Serial.println("\n[ERROR] WiFi connection failed!");
        isWifiConnected = false;
        
        // Flash the LED three times rapidly to indicate a connection error
        blinkFlash(3, 100); 
    }
}

// Function to handle capturing and sending images
void handleCapture(String caption) 
{
    if (millis() - lastCaptureTime < COOLDOWN_TIME) 
    {
        Serial.println("[WARN] In cooldown period (15s). Skipping continuous capture request.");
        
        return;
    }

    Serial.println("[INFO] Starting image capture (No Flash)...");
    
    
    cameraManager.clearBuffer();                    // Discard old frame
    camera_fb_t* fb = cameraManager.capture();  

    if (fb) 
    {
        Serial.println("[INFO] Sending photo to Cloud platforms...");
        
        // Check whether Telegram has been configured before sending
        if (localBotToken.length() > 0 && localChatId.length() > 0) 
        {
            cloudManager.sendTelegramPhoto(fb, caption, localBotToken, localChatId);
        } 
        else 
        {
            Serial.println("[WARN] No Telegram Config. Skip sending to Telegram.");
        }
        
        // Always send to AWS, as it is the system backend
        cloudManager.sendAWS(fb);
        
        cameraManager.freeFrame(fb);
    } 
    else 
    {
        Serial.println("[ERROR] Failed to capture image from camera!");
    }

    lastCaptureTime = millis();
}

void setup() 
{
    // UART0: Serial interface for debug logging to the PC
    Serial.begin(115200);
    
    // UART1: Remap hardware pins (RX=13, TX=12) to match the board layout
    Serial1.begin(115200, SERIAL_8N1, 13, 12);
    
    Serial.println("\n=================================");
    Serial.println("   SLAVE NODE (ESP32-CAM) INITIALIZED");
    Serial.println("=================================");

    if (!cameraManager.begin()) 
    {
        Serial.println("[ERROR] Camera initialization failed. Please check the hardware!");
    }

    blinkFlash(1, 200);

    Serial.println("[INFO] Waiting for WiFi configuration from Master Node...");
}

// Static buffer for UART messages received from the Master
String rxBuf = "";

void loop() 
{
    // Auto-check and reconnect WiFi every 10 seconds to prevent blocking
    if (localSSID.length() > 0 && millis() - lastWifiCheckTime > 10000) 
    {
        lastWifiCheckTime = millis();

        if (WiFi.status() != WL_CONNECTED) 
        {
            Serial.println("[WARN] WiFi connection lost. Reconnecting...");

            WiFi.begin(localSSID.c_str(), localPASS.c_str());
            
            // Wait up to 3 seconds to reconnect to the previous network
            int checkAttempts = 0;
            while (WiFi.status() != WL_CONNECTED && checkAttempts < 6) 
            {
                delay(500);

                checkAttempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) 
            {
                Serial.println("[INFO] Reconnected to WiFi successfully!");

                isWifiConnected = true;
            } 
            else 
            {
                isWifiConnected = false;
            }
        } 
        else 
        {
            isWifiConnected = true;
        }
    }

    // Scan UART port for signals from the Master
    while (Serial1.available() > 0) 
    {
        char c = (char)Serial1.read();

        if (c == '\n') 
        {
            rxBuf.trim();

            if (rxBuf.length() > 0) 
            {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, rxBuf);

                if (!error) 
                {
                    const char* cmd = doc["cmd"] | "";

                    // 1. Receive WiFi and Telegram configuration data pushed from the Master
                    if (strcmp(cmd, "WIFI") == 0) 
                    {
                        const char* ssid = doc["ssid"] | "";
                        const char* pass = doc["pass"] | "";
                        const char* bot  = doc["bot_token"] | "";
                        const char* chat = doc["chat_id"] | "";
                        
                        localSSID     = String(ssid);
                        localPASS     = String(pass);
                        localBotToken = String(bot);
                        localChatId   = String(chat);
                        
                        // Turn on Flash for 1 second to indicate successful command reception
                        blinkFlash(1, 1000);

                        connectWiFi(ssid, pass);
                    }
                    // 2. Process peripheral activation commands and capture images for Cloud upload
                    else 
                    {
                        if (isWifiConnected) 
                        {
                            if (strcmp(cmd, "ALERT") == 0) 
                            {
                                const char* type = doc["type"] | "UNKNOWN";
                                int gas = doc["gas"] | 0;
                                
                                String caption = "🚨 *ALERT DETECTED!* 🚨\n🔥 Type: " + String(type) + "\n💨 Gas concentration: " + String(gas) + " PPM";
                                Serial.println("[ALERT] Received alert command from Master!");
                                handleCapture(caption);
                            }
                            else if (strcmp(cmd, "SNAPSHOT") == 0) 
                            {
                                Serial.println("[INFO] Received manual capture command from Keypad!");
                                handleCapture("📸 *MANUAL CAPTURE FROM KEYPAD*");
                            }
                            else if (strcmp(cmd, "SAFE") == 0) 
                            {
                                // Heartbeat packet for connection keep-alive monitoring
                            }
                        } 
                        else 
                        {
                            Serial.println("[WARN] Received control command but Slave has NO WiFi connection!");
                        }
                    }
                } 
                else 
                {
                    Serial.print("[ERROR] Failed to parse JSON: ");
                    Serial.println(error.f_str());
                }

                rxBuf = ""; 
            }
        } 
        else if (rxBuf.length() < 512) 
        {
            rxBuf += c; 
        }
    }
}