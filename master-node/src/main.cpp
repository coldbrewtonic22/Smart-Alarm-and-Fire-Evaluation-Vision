#define BLYNK_PRINT Serial

#include "Config.h"

#include <WiFi.h>
#include <EEPROM.h>
#include <Arduino.h>
#include <BlynkSimpleEsp32.h>

#include "UIManager.h"
#include "SensorManager.h"
#include "ActuatorManager.h"
#include "UartCommManager.h"
#include "WifiConfigManager.h"

// --- GLOBAL INSTANCES & VARIABLES ---

UIManager uiManager;
SensorManager sensorManager;
UartCommManager uartManager;
ActuatorManager actuatorManager;
WifiConfigManager wifiConfigManager;

// System States

SystemMode currentMode = MODE_AUTO;
int gasThreshold = DEFAULT_GAS_THRESH;

int relayState    = 0;         // 0: OFF, 1: Fan, 2: Pump, 3: Both
bool gasDetected  = false;
bool fireDetected = false;
bool doorState    = false;

// Buzzer State & UI

bool buzzerActive       = false;
bool userSilencedBuzzer = false;
unsigned long deviceOffStartTime = 0;

// Network

String wifiSsid, wifiPass, blynkToken, teleBot, teleChat;
bool isApMode       = false;
bool blynkConnected = false;

// UI State Machine

enum UIState { UI_IDLE, UI_INPUT_PIN, UI_INPUT_THRESH };
UIState currentUIState = UI_IDLE;
String inputBuffer = "";

// FreeRTOS Task Handles

TaskHandle_t TaskSafety_Handle  = NULL;
TaskHandle_t TaskUI_Handle      = NULL;
TaskHandle_t TaskNetwork_Handle = NULL;

// Define helper functions

void handleAlerts();
void initHardware();
void loadEEPROMData();
void startFreeRTOS();
void checkSwitchToAuto();
void connectNetworkOrAP();
void saveThreshold(int thresh);

// --- BLYNK CALLBACKS ---

// V1
BLYNK_WRITE(RELAY_PIN) { 
    if (currentMode == MODE_MANUAL) {
        relayState = param.asInt();
        bool fanOn = (relayState == 1 || relayState == 3);
        bool pumpOn = (relayState == 2 || relayState == 3);
        actuatorManager.controlRelays(fanOn, pumpOn);
    }
}

// V2
BLYNK_WRITE(SERVO_PIN) { 
    if (currentMode == MODE_MANUAL) {
        doorState = param.asInt();
        actuatorManager.controlDoor(doorState);
    }
}

// V3
BLYNK_WRITE(THRESHOLD_PIN) { 
    gasThreshold = param.asInt();
    saveThreshold(gasThreshold);
}

// V4
BLYNK_WRITE(MODE_PIN) { 
    currentMode = param.asInt() ? MODE_AUTO : MODE_MANUAL;

    EEPROM.write(ADDR_MODE, currentMode);
    EEPROM.commit();
}

// --- FREERTOS TASKS ---

// TASK 1: Safety Management (Core 1 - Highest Priority)
void Task_SafetyMonitor(void* pvParameters) {
    unsigned long lastHeartbeat = 0;
    bool lastAlertState = false;

    while (true) {
        // --- Read sensors ---

        sensorManager.readSensors();
        int currentGas = sensorManager.getGasValue();
        bool currentFire = sensorManager.isFireDetected();

        // Handle hysteresis for gas
        if (currentGas > gasThreshold) {
            gasDetected = true;
        } 
        else if (currentGas < gasThreshold - GAS_HYSTERESIS) {
            gasDetected = false;
        }

        fireDetected = currentFire;

        // --- Handle Logic (AUTO / MANUAL) ---

        if (currentMode == MODE_AUTO) {
            handleAlerts();
        } 
        else {
            checkSwitchToAuto();
        }

        // --- Handle Buzzer (Non-blocking) ---

        actuatorManager.handleBuzzer(buzzerActive);

        // --- UART communication with Slave ESP32-S3-CAM ---

        bool currentAlertState = (gasDetected || fireDetected);
        
        // Send immediately if the alarm status changes
        if (currentAlertState != lastAlertState) {
            if (currentAlertState) {
                if      (gasDetected && fireDetected) uartManager.sendStatus("ALERT", "GAS_AND_FIRE", currentGas);
                else if (gasDetected)                 uartManager.sendStatus("ALERT", "GAS", currentGas);
                else if (fireDetected)                uartManager.sendStatus("ALERT", "FIRE", currentGas);
            } 
            else {
                uartManager.sendStatus("SAFE", "", currentGas);
            }

            lastAlertState = currentAlertState;
            lastHeartbeat = millis(); // Reset heartbeat
        }
        
        // Heartbeat every 10 seconds when safe
        if (!currentAlertState && (millis() - lastHeartbeat >= 10000)) {
            uartManager.sendStatus("SAFE", "", currentGas);
            lastHeartbeat = millis();
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);    // Scan every 50 ms
    }
}

// TASK 2: User Interface & Keypad Management (Core 1 - Medium Priority)
void Task_UI_Keypad(void* pvParameters) {
    while (true) {
        char key = uiManager.getPressedKey();
        
        // --- XỬ LÝ NHẬP LIỆU KEYPAD NON-BLOCKING ---
        if (key) {
            if (key == '*') {
                currentUIState = UI_IDLE;   // Cancel
                inputBuffer = "";
                uiManager.updateMainScreen(sensorManager.getGasValue(), fireDetected, relayState, doorState, blynkConnected, currentMode);
            } 
            else if (currentUIState == UI_IDLE) {
                if (key == 'A') {
                    currentMode = (currentMode == MODE_AUTO) ? MODE_MANUAL : MODE_AUTO;
                    EEPROM.write(ADDR_MODE, currentMode); EEPROM.commit();
                    if(blynkConnected) Blynk.virtualWrite(MODE_PIN, currentMode);
                }
                else if (key == 'B') {
                    currentUIState = UI_INPUT_THRESH;
                    inputBuffer = "";
                    uiManager.showMessage(0, 3, "New Threshold:      ", false);
                }
                else if (key == 'C') {
                    uartManager.sendSnapshotRequest();
                    uiManager.showMessage(0, 3, ">   Snapshot Sent!  ", false);
                    vTaskDelay(1500 / portTICK_PERIOD_MS);
                }
                else if (key == 'D' && buzzerActive) {
                    currentUIState = UI_INPUT_PIN;
                    inputBuffer = "";
                    uiManager.showMessage(0, 3, "Enter PIN:          ", false);
                }
            } 
            else if (currentUIState == UI_INPUT_PIN) {
                if (key >= '0' && key <= '9' && inputBuffer.length() < 4) {
                    inputBuffer += key;
                    String hiddenPin = "Enter PIN: ";
                    for (int i=0; i<inputBuffer.length(); i++) hiddenPin += "*";
                    uiManager.showMessage(0, 3, hiddenPin + "   ", false);
                    
                    // Automatically validate when 4 digits are entered
                    if (inputBuffer.length() == 4) {
                        if (inputBuffer == DEFAULT_PIN_CODE) {
                            userSilencedBuzzer = true;
                            buzzerActive = false;
                            uiManager.showMessage(0, 3, "> CORRECT! Silenced ", false);
                        } 
                        else {
                            uiManager.showMessage(0, 3, ">     WRONG PIN!    ", false);
                        }


                        vTaskDelay(1500 / portTICK_PERIOD_MS);
                        currentUIState = UI_IDLE;
                    }
                }
            }
            else if (currentUIState == UI_INPUT_THRESH) {
                if (key >= '0' && key <= '9' && inputBuffer.length() < 4) {
                    inputBuffer += key;
                    uiManager.showMessage(0, 3, "New Thresh: " + inputBuffer + "  ", false);
                }
                else if (key == '#') {
                    if (inputBuffer.length() > 0) {
                        gasThreshold = inputBuffer.toInt();
                        saveThreshold(gasThreshold);
                        if(blynkConnected) Blynk.virtualWrite(THRESHOLD_PIN, gasThreshold);
                        uiManager.showMessage(0, 3, "> Saved!            ", false);
                        vTaskDelay(1500 / portTICK_PERIOD_MS);
                    }

                    currentUIState = UI_IDLE;
                }
            }
        }

        // --- UPDATE MAIN DISPLAY (If not in input mode) ---
        if (currentUIState == UI_IDLE) {
            uiManager.updateMainScreen(sensorManager.getGasValue(), fireDetected, relayState, doorState, blynkConnected, currentMode);
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);   // Scan keys every 100 ms
    }
}

// TASK 3: Handle Network & Failsafe (Core 0 - Low Priority)
void Task_Network(void* pvParameters) {
    unsigned long lastBlynkUpdate = 0;
    
    while (true) {
        if (isApMode) {
            wifiConfigManager.loop();
        } 
        else {
            // Run Blynk (Failsafe: If the network connection is lost and this task hangs, Core 1 tasks will continue running normally)
            if (WiFi.status() == WL_CONNECTED && blynkConnected) {
                Blynk.run();
                
                // Update the dashboard every 2 seconds
                if (millis() - lastBlynkUpdate >= 2000) {
                    Blynk.virtualWrite(GAS_PIN, sensorManager.getGasValue());
                    lastBlynkUpdate = millis();
                }
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// --- MAIN SETUP & UTILITY FUNCTIONS ---

void setup() {
    initHardware();
    loadEEPROMData();
    connectNetworkOrAP();
    
    uiManager.updateMainScreen(0, false, 0, false, blynkConnected, currentMode);
    
    startFreeRTOS();
}

void loop() {
    vTaskDelete(NULL);
}

void startFreeRTOS() {
    xTaskCreatePinnedToCore(Task_Network,       "TaskNetwork", 8192, NULL, 1, &TaskNetwork_Handle, 0);  // WebServer/Blynk chạy vô thời hạn ở Core 0
    xTaskCreatePinnedToCore(Task_SafetyMonitor, "TaskSafety",  4096, NULL, 5, &TaskSafety_Handle,  1);  // Bảo vệ cháy nổ chạy ở Core 1
    xTaskCreatePinnedToCore(Task_UI_Keypad,     "TaskUI",      4096, NULL, 3, &TaskUI_Handle,      1);  // Giao diện người dùng chạy ở Core 1
}

void initHardware() {
    Serial.begin(SERIAL_DEBUG_BAUD);
    
    Serial.println("\n=================================");
    Serial.println("[SYSTEM] Master Node Initialized.");
    Serial.println("=================================");

    EEPROM.begin(EEPROM_SIZE);
    
    actuatorManager.begin();
    sensorManager.begin();
    uiManager.begin();
    uartManager.begin();

    uiManager.showStartupScreen();
    delay(2000);
}

void connectNetworkOrAP() {
    if (wifiSsid.length() > 0 && blynkToken.length() == 32) {
        uiManager.showMessage(0, 0, "   SYSTEM BOOTING   ", true);
        uiManager.showMessage(0, 1, " WiFi Connecting... ", false);

        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

        int retry = 0;
        String dotConnect = "";
        while (WiFi.status() != WL_CONNECTED && retry < 20) {
            delay(500);
            dotConnect += ".";
            if(dotConnect.length() > 20) dotConnect = "";
            uiManager.showMessage(0, 2, dotConnect, false);
            retry++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("[INFO] WiFi Connected!");
            uiManager.showMessage(0, 0, "   WiFi Connected!  ", true);
            uiManager.showMessage(0, 1, wifiSsid, false);
            
            uartManager.sendWiFiConfig(wifiSsid, wifiPass);
            
            Blynk.config(blynkToken.c_str());
            blynkConnected = Blynk.connect();

            delay(2000); 
            return;
        } 
        else {
            Serial.println("[WARN] WiFi Connection Failed.");
            uiManager.showMessage(0, 0, "  Connect Failed!   ", true);
            uiManager.showMessage(0, 1, "Starting AP Mode... ", false);

            delay(2000);
        }
    } 
    else {
        Serial.println("[INFO] No Config found. Entering AP Mode.");
        uiManager.showMessage(0, 0, "   SETUP REQUIRED   ", true);
        uiManager.showMessage(0, 1, "No WiFi Config Found", false);

        delay(2000);
    }

    isApMode = true;
    wifiConfigManager.beginAP();
    
    uiManager.showMessage(0, 0, "   SETUP REQUIRED   ", true);
    uiManager.showMessage(0, 1, "WiFi:ESP32_SmartHome", false);
    uiManager.showMessage(0, 2, "  IP: 192.168.4.1   ", false);
    uiManager.showMessage(0, 3, "Connect to config...", false);
    
    unsigned long waitStartTime = millis();
    while (millis() - waitStartTime < 10000) {
        wifiConfigManager.loop();   
        delay(10);
    } 
}

// Alarm scenario handling function
void handleAlerts() {
    static bool lastAlertState = false;
    bool currentAlertState = (gasDetected || fireDetected);
    
    // If an alarm has just been triggered, reset the Silence flag to enable the Buzzer
    if (currentAlertState && !lastAlertState) {
        userSilencedBuzzer = false;
        buzzerActive = true;
    }

    lastAlertState = currentAlertState;

    if (gasDetected && fireDetected) {
        if (!userSilencedBuzzer) buzzerActive = true;

        actuatorManager.setLED(true);
        doorState = true;  actuatorManager.controlDoor(doorState);
        relayState = 3;    actuatorManager.controlRelays(true, true);
    } 
    else if (gasDetected && !fireDetected) {
        if (!userSilencedBuzzer) buzzerActive = true;

        actuatorManager.setLED(true);
        doorState = true;  actuatorManager.controlDoor(doorState);
        relayState = 1;    actuatorManager.controlRelays(true, false);
    } 
    else if (!gasDetected && fireDetected) {
        if (!userSilencedBuzzer) buzzerActive = true;

        actuatorManager.setLED(true);
        doorState = true;  actuatorManager.controlDoor(doorState);
        relayState = 2;    actuatorManager.controlRelays(false, true);
    } 
    else {
        buzzerActive = false;
        userSilencedBuzzer = false;
        actuatorManager.setLED(false);
        doorState = false;  actuatorManager.controlDoor(doorState);
        relayState = 0;     actuatorManager.controlRelays(false, false);
    }

    // Sync back to the Blynk app
    if (blynkConnected) {
        Blynk.virtualWrite(RELAY_PIN, relayState);
        Blynk.virtualWrite(SERVO_PIN, doorState ? 1 : 0);
    }
}

// Logic to return to AUTO mode if MANUAL mode is inactive
void checkSwitchToAuto() {
    if (currentMode == MODE_MANUAL) {
        if (relayState == 0 && doorState == false) {
            if (deviceOffStartTime == 0) {
                deviceOffStartTime = millis();
            } 
            else if (millis() - deviceOffStartTime >= 5000) {
                currentMode = MODE_AUTO;
                EEPROM.write(ADDR_MODE, currentMode); EEPROM.commit();

                if (blynkConnected) Blynk.virtualWrite(MODE_PIN, currentMode);

                deviceOffStartTime = 0;
            }
        } 
        else {
            deviceOffStartTime = 0;
        }
    }
}

// Load EEPROM
void loadEEPROMData() {
    wifiConfigManager.loadConfigurations(wifiSsid, wifiPass, blynkToken, teleBot, teleChat);
    
    currentMode = (SystemMode)EEPROM.read(ADDR_MODE);
    if (currentMode > MODE_AUTO) currentMode = MODE_AUTO;
    
    int tH = EEPROM.read(ADDR_THRESH_H);
    int tL = EEPROM.read(ADDR_THRESH_L);

    if(tH != 255 && tL != 255) {
        gasThreshold = (tH * 100) + tL;

        if (gasThreshold > 9999 || gasThreshold < 0) gasThreshold = DEFAULT_GAS_THRESH;
    }
}

void saveThreshold(int thresh) {
    EEPROM.write(ADDR_THRESH_H, thresh / 100);
    EEPROM.write(ADDR_THRESH_L, thresh % 100);
    EEPROM.commit();
}