#include "Config.h"

#include <WiFi.h>
#include <EEPROM.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h>

#include "UIManager.h"
#include "SensorManager.h"
#include "ActuatorManager.h"
#include "UartCommManager.h"
#include "WifiConfigManager.h"

// --- GLOBAL INSTANCES & VARIABLES ---

UIManager ui;
UartCommManager uart;
SensorManager sensors;
ActuatorManager actuators;
WifiConfigManager wifiMgr;

// --- SHARED STATE ---

volatile int           g_gasValue        = 0;
volatile bool          g_fireDetected    = false;
volatile AlertState    g_alertState      = STATE_SAFE;
 
volatile int           g_relayState      = 0;                      // 0 = off, 1 = fan, 2 = pump, 3 = both
volatile bool          g_doorOpen        = false;
volatile SystemMode    g_systemMode      = MODE_AUTO;
volatile int           g_gasThreshold    = DEFAULT_GAS_THRESH;
 
volatile bool          g_buzzerActive    = false;
volatile bool          g_userSilenced    = false;   // Keypad PIN → OFF Buzzer
volatile bool          g_sosActive       = false;   // S.O.S (Panic Button)
 
volatile bool          g_blynkConnected  = false;
volatile bool          g_wifiConnected   = false;
volatile bool          g_startupComplete = false;   // Finish 60-seconds Warm-up
 
// Blynk dirty-flag (avoid flood)
volatile bool          g_dirty_gas         = false;
volatile bool          g_dirty_fire        = false;
volatile bool          g_dirty_relay       = false;
volatile bool          g_dirty_door        = false;
volatile bool          g_dirty_mode        = false;
volatile bool          g_dirty_threshold   = false;
volatile bool          g_dirty_notify      = false;         // logEvent 1 time / event
volatile bool          g_dirty_wifi_sync   = false;         // Data synchronization flag for Slave
volatile AlertState    g_lastNotified      = STATE_SAFE;
volatile unsigned long g_deviceOffSince    = 0;             // Auto switch back to AUTO

// --- LCD OVERRIDE ---

SemaphoreHandle_t g_lcdMutex         = NULL;
String            g_lcdLine0         = "";
String            g_lcdLine1         = "";
String            g_lcdLine2         = "";
String            g_lcdLine3         = "";
String            g_pinDisplay       = "";
bool              g_lcdOverrideFull  = false;
unsigned long     g_lcdOverrideUntil = 0;

String centerText(const String& text);

// --- EEPROM's Credentials ---

String g_ssid       = "";
String g_password   = "";
String g_blynkToken = "";
String g_botToken   = "";
String g_chatId     = "";
String g_currentPin = "";

// --- TASK HANDLES ---

TaskHandle_t hTask_Safety    = NULL;
TaskHandle_t hTask_Buzzer    = NULL;
TaskHandle_t hTask_Keypad    = NULL;
TaskHandle_t hTask_LCD       = NULL;
TaskHandle_t hTask_UART_CAM  = NULL;
TaskHandle_t hTask_Blynk     = NULL;
TaskHandle_t hTask_WebServer = NULL;

// --- FUNCTION PROTOTYPES ---

void Task_LCD      (void* pv);
void Task_Blynk    (void* pv);
void Task_Safety   (void* pv);
void Task_Buzzer   (void* pv);
void Task_Keypad   (void* pv);
void Task_UART_CAM (void* pv);
void Task_WebServer(void* pv);

void runHardwareTest();
void connectToWiFiAndBlynk();
void updatePinDisplay(String stars);
void applyAlertToActuators(AlertState state);
void setLCDOverrideRow3(const String& msg, unsigned long durationMs);
void setLCDMenu(const String& l0, const String& l1, const String& l2, const String& l3, unsigned long durationMs);

// --- BLYNK CALLBACKS ---

// V1 - Relay control (Only MANUAL)
BLYNK_WRITE(RELAY_PIN) 
{
    if (g_systemMode != MODE_MANUAL)
    {
        return;
    }
 
    int state = param.asInt();

    g_relayState = state;
    Serial.printf("[BLYNK] Manual Relay control: %d\n", state);

    actuators.controlRelays(state == 1 || state == 3, state == 2 || state == 3);

    g_dirty_relay = false;
}

// V2 - Servo control (Only MANUAL)
BLYNK_WRITE(SERVO_PIN) 
{
    if (g_systemMode != MODE_MANUAL)
    {
        return;
    }
 
    g_doorOpen = (bool)param.asInt();
    actuators.controlDoor(g_doorOpen);
    
    g_dirty_door = false;
    Serial.printf("[BLYNK] Manual Door control: %s\n", g_doorOpen ? "OPEN" : "CLOSE");
}

// V3 - Threshold
BLYNK_WRITE(THRESHOLD_PIN)
{
    int value = param.asInt();

    if (value < 200 || value > 9999)
    {
        return;
    }

    g_gasThreshold = value;
    
    EEPROM.write(ADDR_THRESH_H, value / 100);
    EEPROM.write(ADDR_THRESH_L, value % 100);
    EEPROM.commit();

    Serial.printf("[BLYNK] Gas Threshold changed to: %d PPM\n", value);
    setLCDOverrideRow3("Saved: " + String(value) + " PPM", 2000);

    g_dirty_threshold = false;
}

// V4 - AUTO/MANUAL
BLYNK_WRITE(MODE_PIN)
{
    g_systemMode = (param.asInt() == 1) ? MODE_AUTO : MODE_MANUAL;
    
    EEPROM.write(ADDR_MODE, (uint8_t)g_systemMode);
    EEPROM.commit();

    Serial.printf("[BLYNK] System Mode changed to: %s\n", g_systemMode == MODE_AUTO ? "AUTO" : "MANUAL");
    
    g_deviceOffSince = 0;
    g_dirty_mode = false;
}

String centerText(const String& text) 
{
    if (text.length() >= LCD_COLS)
    {
        return text.substring(0, LCD_COLS);
    } 

    int padding = (LCD_COLS - text.length()) / 2;

    String result = "";

    for (int i = 0; i < padding; i++) 
    {
        result += " ";
    }

    result += text;

    while (result.length() < LCD_COLS) 
    {
        result += " ";
    }

    return result;
}

void updatePinDisplay(String stars) 
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
    {
        g_pinDisplay = stars;
        xSemaphoreGive(g_lcdMutex);
    }
}

void setup()
{
    Serial.begin(SERIAL_DEBUG_BAUD);
    EEPROM.begin(EEPROM_SIZE);
 
    // Create mutex for LCD override
    g_lcdMutex = xSemaphoreCreateMutex();
 
    // Hardware initialization through Managers
    ui.begin();
    uart.begin();
    sensors.begin();
    actuators.begin();
 
    ui.showStartupScreen();
    delay(2000);
 
    // Read credentials from EEPROM
    wifiMgr.loadConfigurations(g_ssid, g_password, g_blynkToken, g_botToken, g_chatId);
 
    int threshVal = EEPROM.read(ADDR_THRESH_H) * 100 + EEPROM.read(ADDR_THRESH_L);
    g_gasThreshold = (threshVal >= 200 && threshVal <= 9999) ? threshVal : DEFAULT_GAS_THRESH;
 
    uint8_t modeVal = EEPROM.read(ADDR_MODE);
    g_systemMode    = (modeVal == 0) ? MODE_MANUAL : MODE_AUTO;

    // Initialize the PIN code from EEPROM
    g_currentPin = "";

    for (int i = 0; i < 4; i++) 
    {
        char c = char(EEPROM.read(ADDR_PIN + i));
        if (c >= '0' && c <= '9')
        {
            g_currentPin += c;
        } 
    }

    // If the EEPROM is empty (PIN has never been set), automatically load the default PIN
    if (g_currentPin.length() != 4) 
    {
        g_currentPin = DEFAULT_PIN_CODE;

        for (int i = 0; i < 4; i++) 
        {
            EEPROM.write(ADDR_PIN + i, g_currentPin[i]);
        }

        EEPROM.commit();
    }
 
    connectToWiFiAndBlynk();    // Save before create Tasks

    // Send initial configuration to the Slave node immediately after the Master finishes booting
    Serial.println("\n[SYSTEM] Activating configuration sync (WiFi & Telegram) to ESP32-CAM...");
    uart.sendWiFiConfig(g_ssid, g_password, g_botToken, g_chatId);
 
    // Create Tasks
    //                      Function        Name         Stack  Param  Prio Handle            Core
    xTaskCreatePinnedToCore(Task_Safety,    "Safety",    8192,  NULL,  5,   &hTask_Safety,    1);
    xTaskCreatePinnedToCore(Task_Buzzer,    "Buzzer",    4096,  NULL,  4,   &hTask_Buzzer,    1);
    xTaskCreatePinnedToCore(Task_Keypad,    "Keypad",    8192,  NULL,  3,   &hTask_Keypad,    1);
    xTaskCreatePinnedToCore(Task_LCD,       "LCD",       8192,  NULL,  2,   &hTask_LCD,       1);
    xTaskCreatePinnedToCore(Task_UART_CAM,  "UART_CAM",  8192,  NULL,  4,   &hTask_UART_CAM,  0);
    xTaskCreatePinnedToCore(Task_Blynk,     "Blynk",     8192,  NULL,  2,   &hTask_Blynk,     0);
    xTaskCreatePinnedToCore(Task_WebServer, "WebServer", 8192,  NULL,  2,   &hTask_WebServer, 0);
}

void loop() 
{
    vTaskDelete(NULL);
}

void connectToWiFiAndBlynk() 
{
    // Always Start AP to config 24/7 (even when STA connected)
    // 1. Start AP + Captive Portal
    wifiMgr.beginAP();
 
    // 2. If has credentials → Upgrade AP_STA and connect STA
    if (g_ssid.length() == 0) 
    {
        ui.showMessage(0, 2, "  No credentials!  ", false);
        ui.showMessage(0, 3, "  IP: 192.168.4.1  ", false);

        delay(5000);
        return;
    }
 
    // Upgrade WiFi mode from AP → AP_STA (AP still running)
    WiFi.mode(WIFI_AP_STA);
    delay(100);
 
    ui.showMessage(0, 2, "WiFi connecting... ", false);
    Serial.printf("\n[WIFI] Connecting to SSID: %s\n", g_ssid.c_str());
    WiFi.begin(g_ssid.c_str(), g_password.c_str());
 
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) 
    {
        delay(500);
        tries++;
    }
 
    if (WiFi.status() != WL_CONNECTED) 
    {
        g_wifiConnected = false;

        ui.showMessage(0, 2, "    WiFi: FAILED    ", false);
        ui.showMessage(0, 3, "  AP Still Running  ", false);

        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);

        delay(5000);
        return;
    }

    g_wifiConnected = true;
    Serial.print("[WIFI] Connected! IP Address: "); 
    Serial.println(WiFi.localIP());
 
    char wifiBuf[21];
    snprintf(wifiBuf, sizeof(wifiBuf), "WiFi OK: %-11s", g_ssid.substring(0, 11).c_str());
    ui.showMessage(0, 2, String(wifiBuf), false);

    delay(1000);
 
    if (g_blynkToken.length() != 32) 
    {
        ui.showMessage(0, 3, "Blynk: Invalid Token", false);

        delay(1500);
        return;
    }
 
    Blynk.config(g_blynkToken.c_str());
    bool ok = Blynk.connect(5000);   // 5s timeout
 
    if (ok) 
    {
        g_blynkConnected = true;
        Serial.println("[BLYNK] Connected to Blynk Cloud successfully!");
        ui.showMessage(0, 3, "  Blynk: Connected  ", false);
    } 
    else 
    {
        Serial.println("[BLYNK] Connection to Blynk Cloud FAILED!");
        ui.showMessage(0, 3, "   Blynk: FAILED!   ", false);
    }

    delay(1500);
}

void setLCDMenu(const String& l0, const String& l1, const String& l2, const String& l3, unsigned long durationMs)
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_lcdLine0 = l0; 
        g_lcdLine1 = l1; 
        g_lcdLine2 = l2; 
        g_lcdLine3 = l3;

        g_lcdOverrideFull = true;
        g_lcdOverrideUntil = millis() + durationMs;

        xSemaphoreGive(g_lcdMutex);
    }
}

void setLCDOverrideRow3(const String& msg, unsigned long durationMs) 
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_lcdLine3 = msg;
        g_lcdOverrideFull = false;
        g_lcdOverrideUntil = millis() + durationMs;
        xSemaphoreGive(g_lcdMutex);
    }
}

void applyAlertToActuators(AlertState state)
{
    switch (state) 
    {
        case STATE_EMERGENCY:
            actuators.controlRelays(true, true);
            actuators.controlDoor(true);
            actuators.setLED(true);

            g_relayState = 3;
            g_doorOpen   = true;

            break;
 
        case STATE_GAS_ONLY:
            actuators.controlRelays(true, false);
            actuators.controlDoor(true);
            actuators.setLED(true);

            g_relayState = 1;
            g_doorOpen   = true;

            break;
 
        case STATE_FIRE_ONLY:
            actuators.controlRelays(false, true);
            actuators.controlDoor(true);
            actuators.setLED(true);

            g_relayState = 2;
            g_doorOpen   = true;

            break;
 
        case STATE_SAFE:

        default:
            actuators.controlRelays(false, false);
            actuators.controlDoor(false);
            actuators.setLED(false);

            g_relayState = 0;
            g_doorOpen   = false;

            break;
    }
}

void runHardwareTest() 
{
    Serial.println("\n[SYSTEM] Initiating Hardware Self-Test...");
    
    setLCDMenu(centerText("HARDWARE TEST"), centerText("Testing Buzzer..."), "", "", 2000);
    actuators.handleBuzzer(true); vTaskDelay(pdMS_TO_TICKS(1000)); actuators.handleBuzzer(false);

    setLCDMenu(centerText("HARDWARE TEST"), centerText("Testing Relays..."), "", "", 2000);
    actuators.controlRelays(true, true); vTaskDelay(pdMS_TO_TICKS(1000)); actuators.controlRelays(false, false);

    setLCDMenu(centerText("HARDWARE TEST"), centerText("Testing Door..."), "", "", 2000);
    actuators.controlDoor(true); vTaskDelay(pdMS_TO_TICKS(1000)); actuators.controlDoor(false);

    setLCDMenu(centerText("HARDWARE TEST"), centerText("Testing Camera..."), "", "", 2000);
    if (g_botToken.length() > 0) 
    {
        uart.sendSnapshotRequest();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    setLCDMenu(centerText("HARDWARE TEST"), centerText("Test Completed!"), "", "", 2000);
}

void Task_Safety(void* pv)
{
    bool gasAbove = false;   // Hysteresis status
 
    while (true) 
    {
        // 1. Read sensors
        sensors.readSensors();
        int  gas  = sensors.getGasValue();
        bool fire = sensors.isFireDetected();

        if (fire != g_fireDetected) 
        {
            g_dirty_fire = true;
        }
 
        g_gasValue     = gas;
        g_fireDetected = fire;
        g_dirty_gas    = true;
 
        // 2. Hysteresis gas
        if (gas >= g_gasThreshold)
        {
            gasAbove = true;
        }
        else if (gas < g_gasThreshold - GAS_HYSTERESIS)  
        {
            gasAbove = false;
        }
 
        // 3. Determine the alarm status
        AlertState newState;
        if (g_sosActive)       
        {
            newState = STATE_EMERGENCY;
        }
        else if (gasAbove && fire) 
        {
            newState = STATE_EMERGENCY;
        }
        else if (gasAbove)          
        {
            newState = STATE_GAS_ONLY;
        }
        else if (fire)
        {
            newState = STATE_FIRE_ONLY;
        }
        else 
        {
            newState = STATE_SAFE;
        }
 
        bool stateChanged = (newState != g_alertState);
        g_alertState      = newState;

        if (stateChanged) 
        {
            const char* stateName = "SAFE";

            if (newState == STATE_GAS_ONLY) 
            {
                stateName = "GAS WARNING";
            }
            else if (newState == STATE_FIRE_ONLY) 
            {
                stateName = "FIRE WARNING";
            }
            else if (newState == STATE_EMERGENCY) 
            {
                stateName = "EMERGENCY (GAS + FIRE)";
            }

            Serial.printf("\n[SAFETY] Alert State Changed: %s\n", stateName);
        }
 
        // 4. AUTO Mode → control devices immediately
        if (g_systemMode == MODE_AUTO) 
        {
            applyAlertToActuators(newState);
 
            if (stateChanged) 
            {
                g_dirty_relay = true;
                g_dirty_door  = true;
            }
        }
 
        // 5. Set Buzzer flag
        if (newState != STATE_SAFE) 
        {
            if (!g_userSilenced || (stateChanged && newState == STATE_EMERGENCY)) 
            {
                g_buzzerActive = true;
                g_userSilenced = false;
            }
        } 
        else if (newState == STATE_SAFE) 
        {
            g_buzzerActive  = false;
            g_userSilenced  = false;
        }
 
        // 6. Trigger Blynk notification 1 time / new event
        if (stateChanged && newState != STATE_SAFE) 
        {
            g_lastNotified = newState;
            g_dirty_notify = true;
        }
 
        // 7. Auto-resume back to AUTO after 5s all devices OFF (MANUAL Mode)
        if (g_systemMode == MODE_MANUAL) 
        {
            if (g_relayState == 0 && !g_doorOpen) 
            {
                if (g_deviceOffSince == 0) 
                {
                    g_deviceOffSince = millis();
                } 
                else if (millis() - g_deviceOffSince >= 5000) 
                {
                    Serial.println("[SAFETY] Inactivity detected! Auto-fallback to AUTO mode.");
                    g_systemMode = MODE_AUTO;
                    g_deviceOffSince = 0;

                    EEPROM.write(ADDR_MODE, (uint8_t)MODE_AUTO);
                    EEPROM.commit();

                    g_dirty_mode = true;
                }
            } 
            else 
            {
                g_deviceOffSince = 0;
            }
        } 
        else 
        {
            g_deviceOffSince = 0;
        }
 
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void Task_Buzzer(void* pv)
{
    while (true) 
    {
        actuators.handleBuzzer(g_buzzerActive);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Task_Keypad(void* pv)
{
    enum KMode { KM_IDLE, KM_MENU, KM_PIN, KM_INFO, KM_OLD_PIN, KM_NEW_PIN };
    KMode  kMode = KM_IDLE;
    String input = "";

    unsigned long menuStartTime = 0;
 
    while (true) 
    {
        if (!g_startupComplete) 
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Auto-exit the Menu and Info screens after 10 seconds of inactivity
        if ((kMode == KM_MENU || kMode == KM_INFO) && (millis() - menuStartTime > 10000)) 
        {
            kMode = KM_IDLE;
            
            if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
            {
                g_lcdOverrideUntil = 0;
                xSemaphoreGive(g_lcdMutex);
            }
        }

        // Passively enter the emergency state without consuming the 'key' event
        if (g_alertState != STATE_SAFE && !g_userSilenced && kMode != KM_PIN) 
        {
            kMode = KM_PIN; 
            input = "";
            
            // Clear the previous display contents
            updatePinDisplay("");

            if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
            {
                g_lcdOverrideUntil = 0; 
                xSemaphoreGive(g_lcdMutex);
            }
        }
 
        char key = ui.getPressedKey();
        if (key == '\0') 
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (kMode == KM_IDLE || kMode == KM_MENU) 
        {
            if (key == '*') 
            {
                if (kMode == KM_IDLE) 
                {
                    kMode = KM_MENU;
                    menuStartTime = millis();

                    setLCDMenu("A: System Info      ",
                               "B: Hardware Test    ", 
                               "C: S.O.S Button     ", 
                               "D: Change PIN       ", 10000);
                } 
                else 
                {
                    kMode = KM_IDLE;

                    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        g_lcdOverrideUntil = 0; 
                        xSemaphoreGive(g_lcdMutex);
                    }
                }
            }
            else if (key == 'A')
            {
                kMode = KM_INFO;
                menuStartTime = millis();

                String r0 = "D23 - IoT - Group 15";
                String r1 = "ESP32 - 192.168.4.1";
                String r2 = g_blynkConnected ? "Blynk: Connected" : "Blynk: Disconnected";
                String r3 = (g_botToken.length() > 0) ? "Tele: Connected" : "Tele: Disconnected";
                
                setLCDMenu(centerText(r0), centerText(r1), centerText(r2), centerText(r3), 10000);
            }
            else if (key == 'B')
            {
                kMode = KM_IDLE;

                if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
                {
                    g_lcdOverrideUntil = 0; 
                    xSemaphoreGive(g_lcdMutex);
                }

                runHardwareTest();
            }
            else if (key == 'C')
            {
                Serial.println("\n[KEYPAD] S.O.S BUTTON PRESSED! Triggering emergency protocol!");
                
                g_sosActive = true; 
                kMode = KM_IDLE;

                if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
                {
                    g_lcdOverrideUntil = 0; 
                    xSemaphoreGive(g_lcdMutex);
                }
            }
            else if (key == 'D')
            {
                if (g_alertState == STATE_SAFE) 
                {
                    kMode = KM_OLD_PIN; 
                    input = "";

                    setLCDOverrideRow3("Old PIN: ", 10000);
                } 
                else 
                {
                    setLCDOverrideRow3(centerText("Alert Active!"), 2000);

                    kMode = KM_IDLE;
                }
            }
            else 
            {
                if (kMode == KM_MENU) 
                {
                    menuStartTime = millis(); 
                }
            }
        }
        else if (kMode == KM_OLD_PIN)
        {
            if (key >= '0' && key <= '9' && input.length() < 4) 
            {
                input += key;
                setLCDOverrideRow3("Old PIN: " + input, 10000);
            } 
            else if (key == '#') 
            {
                if (input == g_currentPin) 
                {
                    kMode = KM_NEW_PIN; 
                    input = "";

                    setLCDOverrideRow3("New PIN: ", 10000);
                } 
                else 
                {
                    setLCDOverrideRow3(centerText("Wrong PIN!"), 2000);

                    input = ""; 
                    kMode = KM_IDLE;
                }
            } 
            else if (key == '*') 
            {
                // Smart backspace support for Old PIN input
                if (input.length() > 0) 
                {
                    input.remove(input.length() - 1);
                    setLCDOverrideRow3("Old PIN: " + input, 10000);
                } 
                else 
                {
                    kMode = KM_IDLE;

                    setLCDOverrideRow3(centerText("Cancelled"), 1000);
                }
            }
        }
        else if (kMode == KM_NEW_PIN)
        {
            if (key >= '0' && key <= '9' && input.length() < 4) 
            {
                input += key;
                setLCDOverrideRow3("New PIN: " + input, 10000);
            } 
            else if (key == '#') 
            {
                if (input.length() == 4) 
                {
                    g_currentPin = input;

                    for (int i = 0; i < 4; i++) 
                    {
                        EEPROM.write(ADDR_PIN + i, input[i]);
                    }
                    EEPROM.commit();

                    Serial.println("[KEYPAD] Security PIN successfully changed and saved to EEPROM.");
                    
                    setLCDOverrideRow3(centerText("PIN Changed!"), 2000);
                } 
                else 
                {
                    setLCDOverrideRow3(centerText("Must be 4 digits!"), 2000);
                }

                input = ""; 
                kMode = KM_IDLE;
            } 
            else if (key == '*') 
            {
                // Smart backspace support for New PIN input
                if (input.length() > 0) 
                {
                    input.remove(input.length() - 1);
                    setLCDOverrideRow3("New PIN: " + input, 10000);
                } 
                else 
                {
                    kMode = KM_IDLE;

                    setLCDOverrideRow3(centerText("Cancelled"), 1000);
                }
            }
        }
        else if (kMode == KM_PIN) 
        {
            if (key >= '0' && key <= '9' && input.length() < 4) 
            {
                input += key;

                if (g_alertState != STATE_SAFE && !g_userSilenced) 
                {
                    updatePinDisplay(input); 
                } 
                else 
                {
                    setLCDOverrideRow3("PIN: " + input, 8000); 
                }
            }
            else if (key == '#') 
            {
                if (input == g_currentPin)
                {
                    g_buzzerActive = false; 
                    g_userSilenced = true;
                    g_sosActive    = false;

                    Serial.println("[KEYPAD] Correct PIN entered. System silenced and SOS cleared.");
                    
                    setLCDOverrideRow3(centerText("Buzzer Silenced!"), 2000);
                } 
                else 
                {
                    setLCDOverrideRow3(centerText("Wrong PIN!"), 2000);
                }
                input = ""; 
                kMode = KM_IDLE; 
                
                updatePinDisplay("");
            } 
            else if (key == '*') 
            {
                // Smart backspace support for the alarm disable screen
                if (input.length() > 0) 
                {
                    input.remove(input.length() - 1);

                    if (g_alertState != STATE_SAFE && !g_userSilenced) 
                    {
                        updatePinDisplay(input);
                    } 
                    else 
                    {
                        setLCDOverrideRow3("PIN: " + input, 8000);
                    }
                } 
                else 
                {
                    // Prevent exit while a critical alarm is active
                    if (g_alertState == STATE_SAFE || g_userSilenced) 
                    {
                        kMode = KM_IDLE;
                        setLCDOverrideRow3(centerText("Cancelled"), 1000);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Task_LCD(void* pv)
{
    unsigned long warmupStart = millis();
    const unsigned long WARMUP_MS = 30000UL;
 
    while (!g_startupComplete) 
    {
        unsigned long elapsed = millis() - warmupStart;
        int remaining = (int)((WARMUP_MS - elapsed) / 1000) + 1;
 
        if (elapsed >= WARMUP_MS) 
        {
            g_startupComplete = true;

            ui.showMessage(0, 2, "   System Ready!    ", true);
            ui.showMessage(0, 3, "   Monitoring...    ", false);

            vTaskDelay(pdMS_TO_TICKS(1500));
            break;
        }
 
        char buf[LCD_COLS + 1];
        snprintf(buf, sizeof(buf), "  Warmup: %2ds left  ", remaining);
        ui.showMessage(0, 2, String(buf), false);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
 
    while (true) 
    {
        bool hasOverride = false;
        bool isFullOverride = false;
        bool isAlert = (g_alertState != STATE_SAFE && !g_userSilenced);

        String ol0, ol1, ol2, ol3;
        String currentPinDisplay = "";
 
        if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(20)) == pdTRUE) 
        {
            if (g_lcdOverrideUntil > 0 && millis() < g_lcdOverrideUntil) 
            {
                hasOverride = true;
                isFullOverride = g_lcdOverrideFull;

                ol0 = g_lcdLine0; 
                ol1 = g_lcdLine1; 
                ol2 = g_lcdLine2; 
                ol3 = g_lcdLine3;
            } 
            else 
            {
                g_lcdOverrideUntil = 0;   
            }

            currentPinDisplay = g_pinDisplay;
            xSemaphoreGive(g_lcdMutex);
        }
 
        char row0[21], row1[21], row2[21], row3[21];

        if (isAlert) 
        {
            String alertTitle = "";
            String alertDesc = "";
            
            if (g_sosActive) 
            {
                alertTitle = "EMERGENCY";
                alertDesc = "S.O.S BUTTON PRESSED";
            }
            else if (g_alertState == STATE_EMERGENCY) 
            {
                alertTitle = "EMERGENCY";
                alertDesc = "GAS & FIRE DETECTED!";
            } 
            else if (g_alertState == STATE_FIRE_ONLY) 
            {
                alertTitle = "EMERGENCY";
                alertDesc = "FIRE DETECTED!";
            } 
            else if (g_alertState == STATE_GAS_ONLY) 
            {
                alertTitle = "WARNING";
                alertDesc = "GAS LEAKED DETECTED!";
            }
            
            String r2 = "Door: OPEN Relay: " + String(g_relayState);
            String r3 = "Enter Pin: " + currentPinDisplay;
            
            // Blink effect for the first 2 rows (toggle every 500 ms)
            bool blinkState = (millis() / 500) % 2 == 0;
            
            if (blinkState) 
            {
                snprintf(row0, sizeof(row0), "%s", centerText(alertTitle).c_str());
                snprintf(row1, sizeof(row1), "%s", centerText(alertDesc).c_str());
            } 
            else 
            {
                // When off, display blank spaces to create a blinking effect
                snprintf(row0, sizeof(row0), "%s", centerText("").c_str());
                snprintf(row1, sizeof(row1), "%s", centerText("").c_str());
            }

            snprintf(row2, sizeof(row2), "%s", centerText(r2).c_str());
            snprintf(row3, sizeof(row3), "%-20s", r3.c_str());
        }
        else if (hasOverride && isFullOverride) 
        {
            snprintf(row0, sizeof(row0), "%s", centerText(ol0).c_str());
            snprintf(row1, sizeof(row1), "%s", centerText(ol1).c_str());
            snprintf(row2, sizeof(row2), "%s", centerText(ol2).c_str());
            snprintf(row3, sizeof(row3), "%s", centerText(ol3).c_str());
        }
        else 
        {
            snprintf(row0, sizeof(row0), "GAS: %-4dPPM FIRE: %d", (int)g_gasValue, g_fireDetected ? 1 : 0);
            
            String doorStr = g_doorOpen ? "OPEN " : "CLOSE";
            snprintf(row1, sizeof(row1), "RELAY: %d DOOR: %s", (int)g_relayState, doorStr.c_str());
            
            String wifiStr = g_wifiConnected ? "ON " : "OFF";
            String modeStr = (g_systemMode == MODE_AUTO) ? "AUTO  " : "MANUAL";
            snprintf(row2, sizeof(row2), "WIFI: %s MODE: %s", wifiStr.c_str(), modeStr.c_str());

            if (hasOverride && !isFullOverride) 
            {
                snprintf(row3, sizeof(row3), "%s", centerText(ol3).c_str());
            } 
            else 
            {
                snprintf(row3, sizeof(row3), "%s", centerText("> Press * for Menu").c_str());
            }
        }

        ui.showMessage(0, 0, String(row0), false);
        ui.showMessage(0, 1, String(row1), false);
        ui.showMessage(0, 2, String(row2), false);
        ui.showMessage(0, 3, String(row3), false);
 
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void Task_UART_CAM(void* pv)
{
    String rxBuf = "";
    AlertState lastSentAlert = STATE_SAFE;
 
    while (true) 
    {
        // Check sync flag to resend the full configuration to the Slave
        if (g_dirty_wifi_sync) 
        {
            Serial.println("[UART_TX] Requesting resynchronization of network & Cloud configuration to the Slave Node...");
            uart.sendWiFiConfig(g_ssid, g_password, g_botToken, g_chatId);

            g_dirty_wifi_sync = false;
        }

        // Read bytes from the Slave one by one
        while (Serial2.available()) 
        {
            char c = (char)Serial2.read();

            if (c == '\n') 
            {
                if (rxBuf.length() > 0) 
                {
                    JsonDocument doc;

                    if (!deserializeJson(doc, rxBuf)) 
                    {
                        const char* cmd = doc["cmd"] | "";
 
                        if (strcmp(cmd, "HEARTBEAT") == 0) 
                        {
                            // Log status to confirm the Slave is alive and connection is stable
                            Serial.println("[UART_RX] Heartbeat received from Slave. Responding with current status...");

                            const char* type = "";
                            switch ((AlertState)g_alertState) 
                            {
                                case STATE_GAS_ONLY:  type = "GAS";       break;
                                case STATE_FIRE_ONLY: type = "FIRE";      break;
                                case STATE_EMERGENCY: type = "EMERGENCY"; break;
                                default:              type = "SAFE";      break;
                            }

                            uart.sendStatus("ALERT", type, (int)g_gasValue);
                        }
                    }

                    rxBuf = "";
                }
            } 
            else if (rxBuf.length() < 512) 
            {
                rxBuf += c;
            }
        }
 
        // Detect alarm state changes → immediately send update to Slave
        AlertState cur = (AlertState)g_alertState;

        if (cur != lastSentAlert) 
        {
            if (cur == STATE_SAFE) 
            {
                Serial.println("[UART_TX] Sending SAFE command to Slave Node.");
                uart.sendStatus("SAFE", "", (int)g_gasValue);
            } 
            else 
            {
                const char* type = "";
                
                if (cur == STATE_GAS_ONLY)
                {
                    type = "GAS";
                }
                else if (cur == STATE_FIRE_ONLY) 
                {
                    type = "FIRE";
                }
                else if (cur == STATE_EMERGENCY) 
                {
                    type = "EMERGENCY";
                }
                
                Serial.printf("[UART_TX] Sending ALERT command (%s) to Slave Node...\n", type);
                uart.sendStatus("ALERT", type, (int)g_gasValue);
                
                // Only request image capture if the user has configured Telegram
                if (g_botToken.length() > 0) 
                {
                    Serial.println("[UART_TX] Sending SNAPSHOT command to Slave Node...");
                    uart.sendSnapshotRequest();   
                } 
                else 
                {
                    Serial.println("[UART_TX] Skipping SNAPSHOT due to missing Telegram configuration.");
                }
            }
 
            lastSentAlert = cur;
        }
 
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Task_Blynk(void* pv)
{
    bool initialSynced = false;
    unsigned long lastGasUpdate = 0;

    while (true) 
    {
        if (g_blynkConnected) 
        {
            Blynk.run();
            
            if (g_startupComplete) 
            {
                if (!initialSynced) 
                {
                    Blynk.virtualWrite(GAS_PIN,       (int)g_gasValue);
                    Blynk.virtualWrite(FIRE_PIN,      g_fireDetected ? 1 : 0);
                    Blynk.virtualWrite(RELAY_PIN,     (int)g_relayState);
                    Blynk.virtualWrite(SERVO_PIN,     g_doorOpen ? 1 : 0);
                    Blynk.virtualWrite(THRESHOLD_PIN, (int)g_gasThreshold);
                    Blynk.virtualWrite(MODE_PIN,      (g_systemMode == MODE_AUTO) ? 1 : 0);

                    initialSynced = true;
                    lastGasUpdate = millis();
                }
     
                // Periodically send gas concentration and flame status every 2 seconds
                if (millis() - lastGasUpdate >= 2000) 
                {
                    Blynk.virtualWrite(GAS_PIN, (int)g_gasValue);
                    Blynk.virtualWrite(FIRE_PIN, g_fireDetected ? 1 : 0);

                    lastGasUpdate = millis();
                }

                // Send device state changes (Dirty flag)
                if (g_dirty_fire) 
                {
                    Blynk.virtualWrite(FIRE_PIN, g_fireDetected ? 1 : 0);
                    g_dirty_fire = false;
                }
                if (g_dirty_relay) 
                { 
                    Blynk.virtualWrite(RELAY_PIN, (int)g_relayState); 
                    g_dirty_relay = false; 
                }
                if (g_dirty_door) 
                { 
                    Blynk.virtualWrite(SERVO_PIN, g_doorOpen ? 1 : 0); 
                    g_dirty_door = false; 
                }
                if (g_dirty_mode) 
                { 
                    Blynk.virtualWrite(MODE_PIN, (g_systemMode == MODE_AUTO) ? 1 : 0); 
                    g_dirty_mode = false; 
                }
                if (g_dirty_threshold) 
                { 
                    Blynk.virtualWrite(THRESHOLD_PIN, (int)g_gasThreshold); 
                    g_dirty_threshold = false; 
                }
     
                // Send alerts to the app
                if (g_dirty_notify) 
                {
                    const char* msg = nullptr;

                    switch ((AlertState)g_lastNotified) 
                    {
                        case STATE_EMERGENCY: msg = "EMERGENCY: Gas & Fire detected!";  break;
                        case STATE_GAS_ONLY:  msg = "WARNING: High gas concentration!"; break;
                        case STATE_FIRE_ONLY: msg = "WARNING: Fire detected!";          break;
                        default: break;
                    }

                    if (msg) 
                    {
                        Blynk.logEvent("gas_fire_detection", msg);
                    }

                    g_dirty_notify = false;
                }
            }
        }
 
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Task_WebServer(void* pv)
{
    while (true) 
    {
        wifiMgr.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}