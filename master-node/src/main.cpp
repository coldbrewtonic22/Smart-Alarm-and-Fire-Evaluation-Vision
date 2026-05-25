#define BLYNK_PRINT Serial

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

volatile int        g_gasValue        = 0;
volatile bool       g_fireDetected    = false;
volatile AlertState g_alertState      = STATE_SAFE;
 
volatile int        g_relayState      = 0;   // 0 = off, 1 = fan, 2 = pump, 3 = both
volatile bool       g_doorOpen        = false;
volatile SystemMode g_systemMode      = MODE_AUTO;
volatile int        g_gasThreshold    = DEFAULT_GAS_THRESH;
 
volatile bool       g_buzzerActive    = false;
volatile bool       g_userSilenced    = false;   // Keypad PIN → OFF Buzzer
 
volatile bool       g_blynkConnected  = false;
volatile bool       g_wifiConnected   = false;
volatile bool       g_startupComplete = false;   // Finish 60-seconds Warm-up
 
// Blynk dirty-flag (avoid flood)
volatile bool       g_dirty_gas       = false;
volatile bool       g_dirty_relay     = false;
volatile bool       g_dirty_door      = false;
volatile bool       g_dirty_mode      = false;
volatile bool       g_dirty_threshold = false;
volatile bool       g_dirty_notify    = false;   // logEvent 1 time / event
volatile AlertState g_lastNotified    = STATE_SAFE;

// Auto switch back to AUTO
volatile unsigned long g_deviceOffSince = 0;

// --- LCD OVERRIDE ---

SemaphoreHandle_t g_lcdMutex        = NULL;
String            g_lcdLine1        = "";
String            g_lcdLine2        = "";
unsigned long     g_lcdOverrideUntil = 0;   // millis() expired

// --- EEPROM's Credentials ---

String g_ssid       = "";
String g_password   = "";
String g_blynkToken = "";
String g_botToken   = "";
String g_chatId     = "";

// --- TASK HANDLES ---

TaskHandle_t hTask_Safety    = NULL;
TaskHandle_t hTask_Buzzer    = NULL;
TaskHandle_t hTask_Keypad    = NULL;
TaskHandle_t hTask_LCD       = NULL;
TaskHandle_t hTask_UART_CAM  = NULL;
TaskHandle_t hTask_Blynk     = NULL;
TaskHandle_t hTask_WebServer = NULL;

// --- FUNCTION PROTOTYPES ---

void Task_Safety   (void* pv);
void Task_Buzzer   (void* pv);
void Task_Keypad   (void* pv);
void Task_LCD      (void* pv);
void Task_UART_CAM (void* pv);
void Task_Blynk    (void* pv);
void Task_WebServer(void* pv);

void setLCDOverride(const String& l1, const String& l2, unsigned long durationMs);
void applyAlertToActuators(AlertState state);
void connectToWiFiAndBlynk();

// --- BLYNK CALLBACKS ---

// V1 - Relay control (Only MANUAL)
BLYNK_WRITE(RELAY_PIN) 
{
    if (g_systemMode != MODE_MANUAL) return;
 
    int state = param.asInt();

    g_relayState = state;
    actuators.controlRelays(state == 1 || state == 3, state == 2 || state == 3);
    g_dirty_relay = false;
}

// V2 - Servo control (Only MANUAL)
BLYNK_WRITE(SERVO_PIN) 
{
    if (g_systemMode != MODE_MANUAL) return;
 
    g_doorOpen = (bool)param.asInt();

    actuators.controlDoor(g_doorOpen);
    g_dirty_door = false;
}

// V3 - Threshold
BLYNK_WRITE(THRESHOLD_PIN)
{
    int val = param.asInt();
    if (val < 200 || val > 9999) return;
    g_gasThreshold = val;

    EEPROM.write(ADDR_THRESH_H, val / 100);
    EEPROM.write(ADDR_THRESH_L, val % 100);
    EEPROM.commit();
 
    setLCDOverride("Threshold:", String(val) + " PPM", 2000);

    g_dirty_threshold = false;
}

// V4 - AUTO/MANUAL
BLYNK_WRITE(MODE_PIN)
{
    g_systemMode = (param.asInt() == 1) ? MODE_AUTO : MODE_MANUAL;

    EEPROM.write(ADDR_MODE, (uint8_t)g_systemMode);
    EEPROM.commit();
 
    g_deviceOffSince = 0;
    g_dirty_mode = false;
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
 
    // Warm-up display
    ui.showStartupScreen();
    delay(2000);
 
    // Read credentials from EEPROM
    wifiMgr.loadConfigurations(g_ssid, g_password, g_blynkToken, g_botToken, g_chatId);
 
    int threshVal = EEPROM.read(ADDR_THRESH_H) * 100 + EEPROM.read(ADDR_THRESH_L);
    g_gasThreshold = (threshVal >= 200 && threshVal <= 9999) ? threshVal : DEFAULT_GAS_THRESH;
 
    uint8_t modeVal = EEPROM.read(ADDR_MODE);
    g_systemMode    = (modeVal == 0) ? MODE_MANUAL : MODE_AUTO;
 
    connectToWiFiAndBlynk();    // Save before create Tasks
 
    // Create Tasks
    //                      Function        Name         Stack  Param  Prio Handle            Core
    xTaskCreatePinnedToCore(Task_Safety,    "Safety",    8192,  NULL,  5,   &hTask_Safety,    1);
    xTaskCreatePinnedToCore(Task_Buzzer,    "Buzzer",    2048,  NULL,  4,   &hTask_Buzzer,    1);
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
        ui.showMessage(0, 2, "No config → AP   ", false);
        ui.showMessage(0, 3, "Connect ESP32:4.1", false);

        delay(2000);
        return;
    }
 
    // Upgrade WiFi mode from AP → AP_STA (AP still running)
    WiFi.mode(WIFI_AP_STA);
    delay(100);
 
    ui.showMessage(0, 2, "WiFi connecting... ", false);
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
        ui.showMessage(0, 2, "WiFi: FAILED      ", false);
        ui.showMessage(0, 3, "AP still running  ", false);

        delay(2000);
        return;
    }
 
    g_wifiConnected = true;
    ui.showMessage(0, 2, ("WiFi OK: " + g_ssid).substring(0, 20), false);
    delay(1000);
 
    if (g_blynkToken.length() != 32) 
    {
        ui.showMessage(0, 3, "Blynk: bad token  ", false);

        delay(1500);
        return;
    }
 
    Blynk.config(g_blynkToken.c_str());
    bool ok = Blynk.connect(5000);   // 5s timeout
 
    if (ok) 
    {
        g_blynkConnected = true;

        ui.showMessage(0, 3, "Blynk: Connected  ", false);
    } 
    else 
    {
        ui.showMessage(0, 3, "Blynk: FAILED     ", false);
    }

    delay(1500);
}

void setLCDOverride(const String& l1, const String& l2, unsigned long durationMs) 
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE) 
    {
        g_lcdLine1        = l1.substring(0, LCD_COLS);
        g_lcdLine2        = l2.substring(0, LCD_COLS);
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

void Task_Safety(void* pv)
{
    bool gasAbove = false;   // Hysteresis status
 
    while (true) 
    {
        // 1. Read sensors
        sensors.readSensors();
        int  gas  = sensors.getGasValue();
        bool fire = sensors.isFireDetected();
 
        g_gasValue     = gas;
        g_fireDetected = fire;
        g_dirty_gas    = true;
 
        // 2. Hysteresis gas
        if      (gas >= g_gasThreshold)                  gasAbove = true;
        else if (gas < g_gasThreshold - GAS_HYSTERESIS)  gasAbove = false;
 
        // 3. Determine the alarm status
        AlertState newState;
        if      (gasAbove && fire)  newState = STATE_EMERGENCY;
        else if (gasAbove)          newState = STATE_GAS_ONLY;
        else if (fire)              newState = STATE_FIRE_ONLY;
        else                        newState = STATE_SAFE;
 
        bool stateChanged = (newState != g_alertState);
        g_alertState      = newState;
 
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
        if (newState != STATE_SAFE && !g_userSilenced) 
        {
            g_buzzerActive = true;
        } 
        else if (newState == STATE_SAFE) 
        {
            g_buzzerActive  = false;
            g_userSilenced  = false;   // auto reset silence when safe
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
    enum KMode { KM_IDLE, KM_PIN, KM_THRESHOLD };
    KMode  kMode = KM_IDLE;
    String input = "";
 
    while (true) 
    {
        // Only start scanning after warm-up
        if (!g_startupComplete) 
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
 
        char key = ui.getPressedKey();
        if (key == NO_KEY) 
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
 
        // Alarm active → enter PIN to disable the buzzer
        if (g_alertState != STATE_SAFE) 
        {
            if (kMode == KM_IDLE) 
            {
                if (key >= '0' && key <= '9') 
                {
                    kMode = KM_PIN;
                    input = String(key);
                    setLCDOverride("Enter PIN:", String(input.length()) + " digit(s)", 4000);
                }
            } 
            else if (kMode == KM_PIN) 
            {
                if (key >= '0' && key <= '9') 
                {
                    input += key;
                    setLCDOverride("Enter PIN:", String(input.length()) + " digit(s)", 4000);
                } 
                else if (key == '#') 
                {
                    if (input == DEFAULT_PIN_CODE) 
                    {
                        g_buzzerActive = false;
                        g_userSilenced = true;
                        setLCDOverride("PIN Correct!", "Buzzer Silenced", 2000);
                    } 
                    else 
                    {
                        setLCDOverride("Wrong PIN!", "Try Again", 2000);
                    }

                    input = ""; kMode = KM_IDLE;
                } 
                else if (key == '*') 
                {
                    input = ""; kMode = KM_IDLE;
                    setLCDOverride("Cancelled", "", 1000);
                }
            }
        }

        // Normal state → press '*' to change the threshold
        else 
        {
            if (kMode == KM_IDLE && key == '*') 
            {
                kMode = KM_THRESHOLD;
                input = "";
                setLCDOverride("New threshold:", "Enter 200-9999 + #", 8000);
            } 
            else if (kMode == KM_THRESHOLD) 
            {
                if (key >= '0' && key <= '9' && input.length() < 4) 
                {
                    input += key;
                    setLCDOverride("Threshold:", input + " PPM", 8000);
                } 
                else if (key == '#') 
                {
                    int val = input.toInt();
                    if (val >= 200 && val <= 9999) 
                    {
                        g_gasThreshold = val;
                        EEPROM.write(ADDR_THRESH_H, val / 100);
                        EEPROM.write(ADDR_THRESH_L, val % 100);
                        EEPROM.commit();
                        g_dirty_threshold = true;
                        setLCDOverride("Threshold set:", String(val) + " PPM", 2000);
                    } 
                    else 
                    {
                        setLCDOverride("Invalid! Range:", "200 - 9999 PPM", 2000);
                    }

                    input = ""; kMode = KM_IDLE;
                } 
                else if (key == '*') 
                {
                    input = ""; kMode = KM_IDLE;
                    setLCDOverride("Cancelled", "", 1000);
                }
            }
        }
 
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Task_LCD(void* pv)
{
    // Warm-up countdown
    unsigned long warmupStart = millis();
    const unsigned long WARMUP_MS = 10000UL;
 
    while (!g_startupComplete) 
    {
        unsigned long elapsed = millis() - warmupStart;
        int remaining = (int)((WARMUP_MS - elapsed) / 1000) + 1;
 
        if (elapsed >= WARMUP_MS) 
        {
            g_startupComplete = true;

            ui.showMessage(2, 2, "System  Ready!  ", true);
            ui.showMessage(0, 3, "  Monitoring... ", false);

            vTaskDelay(pdMS_TO_TICKS(1500));
            break;
        }
 
        char buf[LCD_COLS + 1];
        snprintf(buf, sizeof(buf), " Warmup: %2ds left  ", remaining);
        ui.showMessage(0, 2, String(buf), false);
 
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
 
    // Main display loop
    while (true) 
    {
        bool hasOverride = false;
        String ol1, ol2;
 
        if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(20)) == pdTRUE) 
        {
            if (g_lcdOverrideUntil > 0 && millis() < g_lcdOverrideUntil) 
            {
                hasOverride = true;
                ol1 = g_lcdLine1;
                ol2 = g_lcdLine2;
            } 
            else 
            {
                g_lcdOverrideUntil = 0;   // Expired, Delete
            }

            xSemaphoreGive(g_lcdMutex);
        }
 
        if (hasOverride) 
        {
            // Only override row 1 & 2, keep row 0 & 3 (sensors's data)
            char pad[LCD_COLS + 1];

            snprintf(pad, sizeof(pad), "%-20s", ol1.substring(0, LCD_COLS).c_str());
            ui.showMessage(0, 1, String(pad), false);

            snprintf(pad, sizeof(pad), "%-20s", ol2.substring(0, LCD_COLS).c_str());
            ui.showMessage(0, 2, String(pad), false);
        } 
        else 
        {
            ui.updateMainScreen(
                (int)g_gasValue,
                (bool)g_fireDetected,
                (int)g_relayState,
                (bool)g_doorOpen,
                (bool)g_wifiConnected,
                (SystemMode)g_systemMode
            );
        }
 
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Task_UART_CAM(void* pv)
{
    String rxBuf = "";
    AlertState lastSentAlert = STATE_SAFE;
 
    while (true) 
    {
        // Read every byte from slave
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
                            // Slave alive → send the current status
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
 
        // Detect changes in AlertState → immediately push updates to the slave
        AlertState cur = (AlertState)g_alertState;

        if (cur != lastSentAlert) 
        {
            if (cur == STATE_SAFE) 
            {
                uart.sendStatus("SAFE", "", (int)g_gasValue);
            } 
            else 
            {
                const char* type = "";
                if (cur == STATE_GAS_ONLY)       type = "GAS";
                else if (cur == STATE_FIRE_ONLY) type = "FIRE";
                else if (cur == STATE_EMERGENCY) type = "EMERGENCY";
                
                uart.sendStatus("ALERT", type, (int)g_gasValue);
                uart.sendSnapshotRequest();   // Chỉ chụp ảnh khi có sự cố
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
        // 1. LUÔN LUÔN chạy Blynk.run() để duy trì Heartbeat với máy chủ, tránh bị đá khỏi mạng
        if (g_blynkConnected) 
        {
            Blynk.run();
            
            // 2. CHỈ ĐƯỢC đẩy dữ liệu cảm biến lên App KHI VÀ CHỈ KHI đã hết 60s khởi động
            if (g_startupComplete) 
            {
                // Đồng bộ 5 thông số lần đầu tiên sau khi khởi động xong
                if (!initialSynced) {
                    Blynk.virtualWrite(GAS_PIN,       (int)g_gasValue);
                    Blynk.virtualWrite(RELAY_PIN,     (int)g_relayState);
                    Blynk.virtualWrite(SERVO_PIN,     g_doorOpen ? 1 : 0);
                    Blynk.virtualWrite(THRESHOLD_PIN, (int)g_gasThreshold);
                    Blynk.virtualWrite(MODE_PIN,      (g_systemMode == MODE_AUTO) ? 1 : 0);
                    initialSynced = true;
                    lastGasUpdate = millis(); // Bắt đầu tính mốc thời gian
                }
     
                // Gửi nồng độ Gas định kỳ mỗi 2 giây
                if (millis() - lastGasUpdate >= 2000) {
                    Blynk.virtualWrite(GAS_PIN, (int)g_gasValue);
                    lastGasUpdate = millis();
                }

                // Gửi các thay đổi trạng thái thiết bị (Cờ Dirty)
                if (g_dirty_relay) { 
                    Blynk.virtualWrite(RELAY_PIN, (int)g_relayState); 
                    g_dirty_relay = false; 
                }
                if (g_dirty_door) { 
                    Blynk.virtualWrite(SERVO_PIN, g_doorOpen ? 1 : 0); 
                    g_dirty_door = false; 
                }
                if (g_dirty_mode) { 
                    Blynk.virtualWrite(MODE_PIN, (g_systemMode == MODE_AUTO) ? 1 : 0); 
                    g_dirty_mode = false; 
                }
                if (g_dirty_threshold) { 
                    Blynk.virtualWrite(THRESHOLD_PIN, (int)g_gasThreshold); 
                    g_dirty_threshold = false; 
                }
     
                // Gửi cảnh báo sự cố về App
                if (g_dirty_notify) {
                    const char* msg = nullptr;
                    switch ((AlertState)g_lastNotified) {
                        case STATE_EMERGENCY: msg = "EMERGENCY: Gas & Fire detected!"; break;
                        case STATE_GAS_ONLY:  msg = "WARNING: High gas concentration!"; break;
                        case STATE_FIRE_ONLY: msg = "WARNING: Fire detected!";          break;
                        default: break;
                    }
                    if (msg) Blynk.logEvent("gas_fire_detection", msg);
                    g_dirty_notify = false;
                }
            }
        }
 
        vTaskDelay(pdMS_TO_TICKS(50)); // Nghỉ 50ms để nhường CPU cho Task khác
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