#define BLYNK_TEMPLATE_ID   "TMPL6PxCh11q_"
#define BLYNK_TEMPLATE_NAME "Smart Alarm and Fire Evaluation Vision"
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

// ============================================================
// GLOBAL INSTANCES
// ============================================================

UIManager       ui;
UartCommManager uart;
SensorManager   sensors;
ActuatorManager actuators;
WifiConfigManager wifiMgr;

// ============================================================
// SHARED STATE  (written from multiple tasks/cores → volatile)
// ============================================================

volatile int        g_gasValue        = 0;
volatile bool       g_fireDetected    = false;
volatile AlertState g_alertState      = STATE_SAFE;

volatile int        g_relayState      = 0;      // 0=off 1=fan 2=pump 3=both
volatile bool       g_doorOpen        = false;
volatile SystemMode g_systemMode      = MODE_AUTO;
volatile int        g_gasThreshold    = DEFAULT_GAS_THRESH;

volatile bool       g_buzzerActive    = false;
volatile bool       g_userSilenced    = false;

volatile bool       g_blynkConnected  = false;
volatile bool       g_wifiConnected   = false;
volatile bool       g_startupComplete = false;

// ============================================================
// PENDING ACTUATOR COMMANDS
//
// Blynk callbacks run inside Blynk.run() on Task_Blynk (core 0).
// controlDoor() has a 600 ms vTaskDelay — calling it there blocks
// Task_Blynk → Blynk heartbeat timeout → disconnect.
//
// Pattern: callback ONLY writes these flags.
//          Task_Safety reads & executes them (core 1, safe to block).
// ============================================================

volatile bool g_pendingDoorCmd    = false;
volatile bool g_pendingDoorValue  = false;
volatile bool g_pendingRelayCmd   = false;
volatile int  g_pendingRelayValue = 0;

// ============================================================
// BLYNK DIRTY FLAGS
// One flag = one pending virtualWrite.
// Task_Blynk sends ONE per iteration (50 ms apart) → no flood.
//
// NEVER set dirty flags inside BLYNK_WRITE callbacks — that
// would create a write→echo→write loop with the server.
// ============================================================

volatile bool       g_dirty_relay     = false;
volatile bool       g_dirty_door      = false;
volatile bool       g_dirty_mode      = false;
volatile bool       g_dirty_threshold = false;
volatile bool       g_dirty_notify    = false;
volatile AlertState g_lastNotified    = STATE_SAFE;

volatile unsigned long g_deviceOffSince = 0;

// ============================================================
// LCD MUTEX  (Task_LCD owns the display; others write metadata)
// ============================================================

SemaphoreHandle_t g_lcdMutex         = NULL;
String            g_lcdLine0         = "";
String            g_lcdLine1         = "";
String            g_lcdLine2         = "";
String            g_lcdLine3         = "";
bool              g_lcdOverrideFull  = false;
unsigned long     g_lcdOverrideUntil = 0;

// ============================================================
// CREDENTIALS  (loaded from EEPROM once in setup)
// ============================================================

String g_ssid       = "";
String g_password   = "";
String g_blynkToken = "";
String g_botToken   = "";
String g_chatId     = "";

// ============================================================
// TASK HANDLES
// ============================================================

TaskHandle_t hTask_Safety    = NULL;
TaskHandle_t hTask_Buzzer    = NULL;
TaskHandle_t hTask_Keypad    = NULL;
TaskHandle_t hTask_LCD       = NULL;
TaskHandle_t hTask_UART_CAM  = NULL;
TaskHandle_t hTask_Blynk     = NULL;
TaskHandle_t hTask_WebServer = NULL;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void Task_Safety   (void* pv);
void Task_Buzzer   (void* pv);
void Task_Keypad   (void* pv);
void Task_LCD      (void* pv);
void Task_UART_CAM (void* pv);
void Task_Blynk    (void* pv);
void Task_WebServer(void* pv);

void connectToWiFiAndBlynk();
void applyAlertToActuators(AlertState state);
void setLCDOverrideRow3(const String& msg, unsigned long durationMs);
void setLCDMenu(const String& l0, const String& l1, const String& l2,
                const String& l3, unsigned long durationMs);

// ============================================================
// BLYNK CALLBACKS
//
// KEY RULE: callbacks run on Task_Blynk (core 0) inside Blynk.run().
//   • NEVER call actuators directly here.
//   • NEVER set dirty flags here (causes write→server-echo→callback loop).
//   • ONLY update state variables + pending flags.
// ============================================================

// V1 — Relay  (MANUAL only)
BLYNK_WRITE(RELAY_PIN)
{
    if (g_systemMode != MODE_MANUAL) return;
    int state           = param.asInt();
    g_relayState        = state;   // immediate LCD update
    g_pendingRelayValue = state;
    g_pendingRelayCmd   = true;
}

// V2 — Servo / Door  (MANUAL only)
BLYNK_WRITE(SERVO_PIN)
{
    if (g_systemMode != MODE_MANUAL) return;
    bool open          = (bool)param.asInt();
    g_doorOpen         = open;     // immediate LCD update
    g_pendingDoorValue = open;
    g_pendingDoorCmd   = true;
}

// V3 — Gas threshold
BLYNK_WRITE(THRESHOLD_PIN)
{
    int val = param.asInt();
    if (val < 200 || val > 9999) return;
    g_gasThreshold = val;
    EEPROM.write(ADDR_THRESH_H, val / 100);
    EEPROM.write(ADDR_THRESH_L, val % 100);
    EEPROM.commit();
    setLCDOverrideRow3("Saved: " + String(val) + " PPM", 2000);
    // NOTE: do NOT set g_dirty_threshold here — would echo back to server
}

// V4 — Mode switch
BLYNK_WRITE(MODE_PIN)
{
    g_systemMode = (param.asInt() == 1) ? MODE_AUTO : MODE_MANUAL;
    EEPROM.write(ADDR_MODE, (uint8_t)g_systemMode);
    EEPROM.commit();
    g_deviceOffSince = 0;
    // NOTE: do NOT set g_dirty_mode here — would echo back to server
}

// On (re)connect: push current sensor readings (read-only pins, no BLYNK_WRITE handler).
// Control-pin widgets (relay, door, mode) are updated by Task_Blynk via dirty flags
// AFTER a safe delay — this prevents the server-echo race condition where
// virtualWrite(SERVO_PIN, x) triggers BLYNK_WRITE(SERVO_PIN) and overwrites a
// pending user command.
BLYNK_CONNECTED()
{
    Blynk.virtualWrite(GAS_PIN, (int)g_gasValue);
    // Trigger dirty flags so Task_Blynk syncs control widgets on its next iterations
    // (Task_Blynk sends them one-at-a-time with 50 ms gaps — no echo risk there
    //  because those writes originate from the device, not from a user action).
    g_dirty_relay     = true;
    g_dirty_door      = true;
    g_dirty_mode      = true;
    g_dirty_threshold = true;
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(SERIAL_DEBUG_BAUD);
    EEPROM.begin(EEPROM_SIZE);

    g_lcdMutex = xSemaphoreCreateMutex();

    ui.begin();
    uart.begin();
    sensors.begin();
    actuators.begin();   // uses delay() directly — safe, scheduler not started yet

    ui.showStartupScreen();
    delay(2000);

    wifiMgr.loadConfigurations(g_ssid, g_password, g_blynkToken, g_botToken, g_chatId);

    int threshVal  = EEPROM.read(ADDR_THRESH_H) * 100 + EEPROM.read(ADDR_THRESH_L);
    g_gasThreshold = (threshVal >= 200 && threshVal <= 9999) ? threshVal : DEFAULT_GAS_THRESH;

    uint8_t modeVal = EEPROM.read(ADDR_MODE);
    g_systemMode    = (modeVal == 0) ? MODE_MANUAL : MODE_AUTO;

    connectToWiFiAndBlynk();

    Serial.println("[SYSTEM] Waiting 5s for CAM to boot...");
    delay(5000);
    uart.sendWiFiConfig(g_ssid, g_password, g_botToken, g_chatId);

    //                      Function        Name         Stack  Param  Prio  Handle           Core
    xTaskCreatePinnedToCore(Task_Safety,    "Safety",    8192,  NULL,  5,    &hTask_Safety,   1);
    xTaskCreatePinnedToCore(Task_Buzzer,    "Buzzer",    4096,  NULL,  4,    &hTask_Buzzer,   1);
    xTaskCreatePinnedToCore(Task_Keypad,    "Keypad",    8192,  NULL,  3,    &hTask_Keypad,   1);
    xTaskCreatePinnedToCore(Task_LCD,       "LCD",       8192,  NULL,  2,    &hTask_LCD,      1);
    xTaskCreatePinnedToCore(Task_UART_CAM,  "UART_CAM",  8192,  NULL,  4,    &hTask_UART_CAM, 0);
    xTaskCreatePinnedToCore(Task_Blynk,     "Blynk",     8192,  NULL,  2,    &hTask_Blynk,    0);
    xTaskCreatePinnedToCore(Task_WebServer, "WebServer", 8192,  NULL,  2,    &hTask_WebServer,0);
}

void loop()
{
    vTaskDelete(NULL);
}

// ============================================================
// WIFI + BLYNK INIT  (called once from setup, blocking OK)
// ============================================================

void connectToWiFiAndBlynk()
{
    wifiMgr.beginAP();

    if (g_ssid.length() == 0)
    {
        ui.showMessage(0, 2, "  No credentials!  ", false);
        ui.showMessage(0, 3, "  IP: 192.168.4.1  ", false);
        delay(2000);
        return;
    }

    WiFi.mode(WIFI_AP_STA);
    delay(100);

    ui.showMessage(0, 2, "WiFi connecting... ", false);
    WiFi.begin(g_ssid.c_str(), g_password.c_str());

    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++)
        delay(500);

    if (WiFi.status() != WL_CONNECTED)
    {
        g_wifiConnected = false;
        ui.showMessage(0, 2, "    WiFi: FAILED    ", false);
        ui.showMessage(0, 3, "  AP Still Running  ", false);
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);
        delay(2000);
        return;
    }

    g_wifiConnected = true;
    char buf[21];
    snprintf(buf, sizeof(buf), "WiFi OK: %-11s", g_ssid.substring(0, 11).c_str());
    ui.showMessage(0, 2, String(buf), false);
    delay(1000);

    if (g_blynkToken.length() != 32)
    {
        ui.showMessage(0, 3, "Blynk: Invalid Token", false);
        delay(1500);
        return;
    }

    Blynk.config(g_blynkToken.c_str());
    if (Blynk.connect(5000))
    {
        g_blynkConnected = true;
        ui.showMessage(0, 3, "  Blynk: Connected  ", false);
    }
    else
    {
        ui.showMessage(0, 3, "   Blynk: FAILED!   ", false);
    }
    delay(1500);
}

// ============================================================
// LCD HELPERS
// ============================================================

void setLCDMenu(const String& l0, const String& l1, const String& l2,
                const String& l3, unsigned long durationMs)
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_lcdLine0 = l0; g_lcdLine1 = l1;
        g_lcdLine2 = l2; g_lcdLine3 = l3;
        g_lcdOverrideFull  = true;
        g_lcdOverrideUntil = millis() + durationMs;
        xSemaphoreGive(g_lcdMutex);
    }
}

void setLCDOverrideRow3(const String& msg, unsigned long durationMs)
{
    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        g_lcdLine3 = msg;
        g_lcdOverrideFull  = false;
        g_lcdOverrideUntil = millis() + durationMs;
        xSemaphoreGive(g_lcdMutex);
    }
}

// ============================================================
// APPLY ALERT TO ACTUATORS
//
// Called only from Task_Safety on state change (AUTO mode).
// Does NOT call controlDoor() directly — that blocks 600 ms.
// Sets pending flag instead; top of Task_Safety loop handles it.
// ============================================================

void applyAlertToActuators(AlertState state)
{
    switch (state)
    {
        case STATE_EMERGENCY:
            actuators.controlRelays(true, true);
            actuators.setLED(true);
            g_relayState       = 3;
            g_doorOpen         = true;
            g_pendingDoorValue = true;
            g_pendingDoorCmd   = true;
            break;

        case STATE_GAS_ONLY:
            actuators.controlRelays(true, false);
            actuators.setLED(true);
            g_relayState       = 1;
            g_doorOpen         = true;
            g_pendingDoorValue = true;
            g_pendingDoorCmd   = true;
            break;

        case STATE_FIRE_ONLY:
            actuators.controlRelays(false, true);
            actuators.setLED(true);
            g_relayState       = 2;
            g_doorOpen         = true;
            g_pendingDoorValue = true;
            g_pendingDoorCmd   = true;
            break;

        case STATE_SAFE:
        default:
            actuators.controlRelays(false, false);
            actuators.setLED(false);
            g_relayState       = 0;
            g_doorOpen         = false;
            g_pendingDoorValue = false;
            g_pendingDoorCmd   = true;
            break;
    }
}

// ============================================================
// TASK: SAFETY  (core 1, priority 5 — highest)
//
// Owns ALL actuator execution. No other task/callback calls
// controlDoor() or controlRelays() directly.
// ============================================================

void Task_Safety(void* pv)
{
    bool gasAbove       = false;
    int  gasConfirmCount = 0;

    while (true)
    {
        // ── STEP 0: execute pending actuator commands ──────────────
        // Both MANUAL (from Blynk callbacks) and AUTO (from applyAlertToActuators)
        // route through here so controlDoor()'s 600 ms vTaskDelay never
        // blocks anyone except Task_Safety itself (which is fine).

        if (g_pendingRelayCmd)
        {
            g_pendingRelayCmd = false;
            int s = g_pendingRelayValue;
            actuators.controlRelays(s == 1 || s == 3, s == 2 || s == 3);
            g_dirty_relay = true;
            Serial.printf("[SAFETY] Relay → %d\n", s);
        }

        if (g_pendingDoorCmd)
        {
            g_pendingDoorCmd = false;
            bool open = g_pendingDoorValue;
            actuators.controlDoor(open);   // blocks 600 ms via vTaskDelay — OK here
            g_dirty_door = true;
            Serial.printf("[SAFETY] Door → %s\n", open ? "OPEN" : "CLOSE");
        }

        // ── STEP 1: read sensors ───────────────────────────────────
        sensors.readSensors();
        g_gasValue     = sensors.getGasValue();
        g_fireDetected = sensors.isFireDetected();

        // ── STEP 2: gas hysteresis + spike filter ──────────────────
        // gasAbove goes true only after 6 consecutive readings (≈ 3 s)
        if (g_gasValue >= g_gasThreshold)
        {
            if (++gasConfirmCount >= 6) gasAbove = true;
        }
        else if (g_gasValue < g_gasThreshold - GAS_HYSTERESIS)
        {
            gasConfirmCount = 0;
            gasAbove = false;
        }

        // ── STEP 3: determine alert state ──────────────────────────
        AlertState newState;
        if      (gasAbove && g_fireDetected) newState = STATE_EMERGENCY;
        else if (gasAbove)                   newState = STATE_GAS_ONLY;
        else if (g_fireDetected)             newState = STATE_FIRE_ONLY;
        else                                 newState = STATE_SAFE;

        bool stateChanged = (newState != g_alertState);
        g_alertState      = newState;

        if (stateChanged)
        {
            const char* n = (newState == STATE_EMERGENCY) ? "EMERGENCY" :
                            (newState == STATE_GAS_ONLY)  ? "GAS WARNING" :
                            (newState == STATE_FIRE_ONLY) ? "FIRE WARNING" : "SAFE";
            Serial.printf("\n[SAFETY] State → %s\n", n);
        }

        // ── STEP 4: AUTO mode — react on state change only ─────────
        if (g_systemMode == MODE_AUTO && stateChanged)
        {
            applyAlertToActuators(newState);
            g_dirty_relay = true;
            g_dirty_door  = true;
        }

        // ── STEP 5: buzzer ─────────────────────────────────────────
        if (newState == STATE_SAFE)
        {
            g_buzzerActive = false;
            g_userSilenced = false;
        }
        else if (!g_userSilenced || (stateChanged && newState == STATE_EMERGENCY))
        {
            g_buzzerActive = true;
            g_userSilenced = false;
        }

        // ── STEP 6: Blynk notification (once per new event) ────────
        if (stateChanged && newState != STATE_SAFE)
        {
            g_lastNotified = newState;
            g_dirty_notify = true;
        }

        // ── STEP 7: auto-fallback MANUAL → AUTO after 30 s idle ───
        if (g_systemMode == MODE_MANUAL)
        {
            if (g_relayState == 0 && !g_doorOpen)
            {
                if (g_deviceOffSince == 0)
                    g_deviceOffSince = millis();
                else if (millis() - g_deviceOffSince >= 30000)
                {
                    g_systemMode     = MODE_AUTO;
                    g_deviceOffSince = 0;
                    EEPROM.write(ADDR_MODE, (uint8_t)MODE_AUTO);
                    EEPROM.commit();
                    g_dirty_mode = true;
                    Serial.println("[SAFETY] Inactivity → fallback to AUTO");
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

// ============================================================
// TASK: BUZZER  (core 1, priority 4)
// ============================================================

void Task_Buzzer(void* pv)
{
    while (true)
    {
        actuators.handleBuzzer(g_buzzerActive);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ============================================================
// TASK: KEYPAD  (core 1, priority 3)
// ============================================================

void Task_Keypad(void* pv)
{
    enum KMode { KM_IDLE, KM_MENU, KM_PIN, KM_THRESHOLD };
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

        // Auto-exit menu after 10 s inactivity
        if (kMode == KM_MENU && (millis() - menuStartTime > 10000))
        {
            kMode = KM_IDLE;
            if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                g_lcdOverrideUntil = 0;
                xSemaphoreGive(g_lcdMutex);
            }
        }

        char key = ui.getPressedKey();
        if (key == '\0') { vTaskDelay(pdMS_TO_TICKS(50)); continue; }

        // Emergency: force PIN entry screen
        if (g_alertState != STATE_SAFE && !g_userSilenced && kMode != KM_PIN)
        {
            kMode = KM_PIN;
            input = "";
            if (key >= '0' && key <= '9') input += key;
            String stars(input.length(), '*');
            setLCDOverrideRow3("PIN: " + stars, 8000);
            continue;
        }

        if (kMode == KM_IDLE)
        {
            if (key == '*')
            {
                kMode = KM_MENU;
                menuStartTime = millis();
                setLCDMenu(" A: Toggle Mode     ",
                           " B: Set Threshold   ",
                           " C: Take Snapshot   ",
                           " D: Silence Buzzer  ", 10000);
            }
        }
        else if (kMode == KM_MENU)
        {
            switch (key)
            {
                case 'A':
                    g_systemMode = (g_systemMode == MODE_AUTO) ? MODE_MANUAL : MODE_AUTO;
                    EEPROM.write(ADDR_MODE, (uint8_t)g_systemMode);
                    EEPROM.commit();
                    g_dirty_mode = true;
                    setLCDOverrideRow3(g_systemMode == MODE_AUTO ? "Mode: AUTO" : "Mode: MANUAL", 2000);
                    kMode = KM_IDLE;
                    break;

                case 'B':
                    kMode = KM_THRESHOLD;
                    input = "";
                    setLCDOverrideRow3("Thresh: Enter + #", 8000);
                    break;

                case 'C':
                    uart.sendSnapshotRequest();
                    setLCDOverrideRow3("Snapshot Requested!", 2000);
                    kMode = KM_IDLE;
                    break;

                case 'D':
                    if (g_alertState != STATE_SAFE && !g_userSilenced)
                    {
                        kMode = KM_PIN; input = "";
                        setLCDOverrideRow3("PIN: ", 8000);
                    }
                    else
                    {
                        setLCDOverrideRow3(g_alertState == STATE_SAFE ?
                            "Safe! No Alert" : "Already Silenced!", 2000);
                        kMode = KM_IDLE;
                    }
                    break;

                case '*':
                    kMode = KM_IDLE;
                    if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(50)) == pdTRUE)
                    {
                        g_lcdOverrideUntil = 0;
                        xSemaphoreGive(g_lcdMutex);
                    }
                    break;

                default:
                    menuStartTime = millis();
                    break;
            }
        }
        else if (kMode == KM_PIN)
        {
            if (key >= '0' && key <= '9' && input.length() < 4)
            {
                input += key;
                String stars(input.length(), '*');
                setLCDOverrideRow3("PIN: " + stars, 8000);
            }
            else if (key == '#')
            {
                if (input == DEFAULT_PIN_CODE)
                {
                    g_buzzerActive = false;
                    g_userSilenced = true;
                    setLCDOverrideRow3("Buzzer Silenced!", 2000);
                }
                else
                {
                    setLCDOverrideRow3("Wrong PIN!", 2000);
                }
                input = ""; kMode = KM_IDLE;
            }
            else if (key == '*')
            {
                input = ""; kMode = KM_IDLE;
                setLCDOverrideRow3("Cancelled", 1000);
            }
        }
        else if (kMode == KM_THRESHOLD)
        {
            if (key >= '0' && key <= '9' && input.length() < 4)
            {
                input += key;
                setLCDOverrideRow3("Thresh: " + input + " PPM", 8000);
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
                    setLCDOverrideRow3("Saved: " + String(val) + " PPM", 2000);
                }
                else
                {
                    setLCDOverrideRow3("Invalid! 200-9999", 2000);
                }
                input = ""; kMode = KM_IDLE;
            }
            else if (key == '*')
            {
                input = ""; kMode = KM_IDLE;
                setLCDOverrideRow3("Cancelled", 1000);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
// TASK: LCD  (core 1, priority 2)
// ============================================================

void Task_LCD(void* pv)
{
    unsigned long warmupStart     = millis();
    const unsigned long WARMUP_MS = 60000UL;

    // Warmup phase
    while (!g_startupComplete)
    {
        unsigned long elapsed = millis() - warmupStart;

        if (elapsed >= WARMUP_MS)
        {
            g_startupComplete = true;
            ui.showMessage(0, 2, "   System Ready!    ", true);
            ui.showMessage(0, 3, "   Monitoring...    ", false);
            vTaskDelay(pdMS_TO_TICKS(1500));
            break;
        }

        int remaining = (int)((WARMUP_MS - elapsed) / 1000) + 1;
        char buf[LCD_COLS + 1];
        snprintf(buf, sizeof(buf), "  Warmup: %2ds left  ", remaining);
        ui.showMessage(0, 2, String(buf), false);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Main display loop
    while (true)
    {
        bool   hasOverride    = false;
        bool   isFullOverride = false;
        String ol0, ol1, ol2, ol3;

        if (xSemaphoreTake(g_lcdMutex, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            if (g_lcdOverrideUntil > 0 && millis() < g_lcdOverrideUntil)
            {
                hasOverride    = true;
                isFullOverride = g_lcdOverrideFull;
                ol0 = g_lcdLine0; ol1 = g_lcdLine1;
                ol2 = g_lcdLine2; ol3 = g_lcdLine3;
            }
            else
            {
                g_lcdOverrideUntil = 0;
            }
            xSemaphoreGive(g_lcdMutex);
        }

        char r0[21], r1[21], r2[21], r3[21];

        if (hasOverride && isFullOverride)
        {
            snprintf(r0, sizeof(r0), "%-20s", ol0.substring(0, LCD_COLS).c_str());
            snprintf(r1, sizeof(r1), "%-20s", ol1.substring(0, LCD_COLS).c_str());
            snprintf(r2, sizeof(r2), "%-20s", ol2.substring(0, LCD_COLS).c_str());
            snprintf(r3, sizeof(r3), "%-20s", ol3.substring(0, LCD_COLS).c_str());
        }
        else
        {
            snprintf(r0, sizeof(r0), "GAS: %-4dPPM FIRE: %d",
                     (int)g_gasValue, g_fireDetected ? 1 : 0);

            const char* doorStr = g_doorOpen ? "OPEN " : "CLOSE";
            snprintf(r1, sizeof(r1), "RELAY: %d DOOR: %-5s",
                     (int)g_relayState, doorStr);

            const char* wifiStr = g_wifiConnected ? "ON " : "OFF";
            const char* modeStr = (g_systemMode == MODE_AUTO) ? "AUTO  " : "MANUAL";
            snprintf(r2, sizeof(r2), "WIFI: %-3s MODE: %-6s", wifiStr, modeStr);

            if (hasOverride && !isFullOverride)
                snprintf(r3, sizeof(r3), "%-20s", ol3.substring(0, LCD_COLS).c_str());
            else
                snprintf(r3, sizeof(r3), " > Press * for Menu ");
        }

        ui.showMessage(0, 0, String(r0), false);
        ui.showMessage(0, 1, String(r1), false);
        ui.showMessage(0, 2, String(r2), false);
        ui.showMessage(0, 3, String(r3), false);

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// ============================================================
// TASK: UART CAM  (core 0, priority 4)
// ============================================================

void Task_UART_CAM(void* pv)
{
    String     rxBuf        = "";
    AlertState lastSentAlert = STATE_SAFE;

    while (true)
    {
        // Receive from slave
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
                            AlertState cur = (AlertState)g_alertState;
                            if (cur == STATE_SAFE)
                                uart.sendStatus("SAFE", "", (int)g_gasValue);
                            else
                            {
                                const char* t =
                                    (cur == STATE_GAS_ONLY)  ? "GAS"       :
                                    (cur == STATE_FIRE_ONLY) ? "FIRE"      : "EMERGENCY";
                                uart.sendStatus("ALERT", t, (int)g_gasValue);
                            }
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

        // Push state changes to slave immediately
        AlertState cur = (AlertState)g_alertState;
        if (cur != lastSentAlert)
        {
            if (cur == STATE_SAFE)
            {
                uart.sendStatus("SAFE", "", (int)g_gasValue);
            }
            else
            {
                const char* t =
                    (cur == STATE_GAS_ONLY)  ? "GAS"       :
                    (cur == STATE_FIRE_ONLY) ? "FIRE"      : "EMERGENCY";
                uart.sendStatus("ALERT", t, (int)g_gasValue);
                uart.sendSnapshotRequest();
            }
            lastSentAlert = cur;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================
// TASK: BLYNK  (core 0, priority 2)
//
// Responsibilities:
//   1. Keep Blynk connected (WiFi watchdog + auto-reconnect).
//   2. Call Blynk.run() — processes incoming callbacks.
//   3. Push outgoing data via dirty flags, ONE per iteration,
//      with 50 ms spacing → no flood / rate-limit disconnect.
//
// What it must NOT do:
//   • Call any actuator function.
//   • Set dirty flags inside BLYNK_WRITE (echo loop risk).
// ============================================================

void Task_Blynk(void* pv)
{
    bool          initialSynced  = false;
    unsigned long lastGasUpdate  = 0;
    unsigned long lastConnectTry = 0;
    const unsigned long RECONNECT_MS = 10000;

    while (true)
    {
        // ── WiFi watchdog ──────────────────────────────────────────
        if (WiFi.status() != WL_CONNECTED)
        {
            g_blynkConnected = false;
            g_wifiConnected  = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        g_wifiConnected = true;

        // ── Blynk connection watchdog + auto-reconnect ─────────────
        if (!Blynk.connected())
        {
            g_blynkConnected = false;
            initialSynced    = false;   // force re-sync on next connect

            unsigned long now = millis();
            if (now - lastConnectTry >= RECONNECT_MS)
            {
                lastConnectTry = now;
                Serial.println("[BLYNK] Reconnecting...");
                if (Blynk.connect(3000))
                {
                    g_blynkConnected = true;
                    Serial.println("[BLYNK] Reconnected.");
                }
                else
                {
                    Serial.println("[BLYNK] Reconnect failed, retry in 10s.");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        g_blynkConnected = true;

        // ── Process incoming Blynk messages (triggers callbacks) ───
        Blynk.run();

        // ── Warmup: don't send any data yet ───────────────────────
        if (!g_startupComplete)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── Initial sync after (re)connect ────────────────────────
        // Send only the read-only sensor pin here.
        // Control-widget pins are queued as dirty flags below and sent
        // one-at-a-time; they do NOT echo back as BLYNK_WRITE because
        // dirty-flag writes originate from the device (not a user tap).
        if (!initialSynced)
        {
            Blynk.virtualWrite(GAS_PIN, (int)g_gasValue);
            // BLYNK_CONNECTED already set dirty flags for relay/door/mode/threshold
            initialSynced = true;
            lastGasUpdate = millis();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── Periodic gas reading every 2 s ────────────────────────
        unsigned long now = millis();
        if (now - lastGasUpdate >= 2000)
        {
            Blynk.virtualWrite(GAS_PIN, (int)g_gasValue);
            lastGasUpdate = now;
        }

        // ── Dirty flags — ONE per iteration (50 ms spacing) ───────
        // Priority: alert notification > relay > door > mode > threshold
        if (g_dirty_notify)
        {
            const char* msg =
                (g_lastNotified == STATE_EMERGENCY) ? "EMERGENCY: Gas & Fire detected!"  :
                (g_lastNotified == STATE_GAS_ONLY)  ? "WARNING: High gas concentration!" :
                (g_lastNotified == STATE_FIRE_ONLY) ? "WARNING: Fire detected!"          : nullptr;
            if (msg) Blynk.logEvent("gas_fire_detection", msg);
            g_dirty_notify = false;
        }
        else if (g_dirty_relay)
        {
            Blynk.virtualWrite(RELAY_PIN, (int)g_relayState);
            g_dirty_relay = false;
        }
        else if (g_dirty_door)
        {
            Blynk.virtualWrite(SERVO_PIN, g_doorOpen ? 1 : 0);
            g_dirty_door = false;
        }
        else if (g_dirty_mode)
        {
            Blynk.virtualWrite(MODE_PIN, (g_systemMode == MODE_AUTO) ? 1 : 0);
            g_dirty_mode = false;
        }
        else if (g_dirty_threshold)
        {
            Blynk.virtualWrite(THRESHOLD_PIN, (int)g_gasThreshold);
            g_dirty_threshold = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
// TASK: WEB SERVER  (core 0, priority 2)
// ============================================================

void Task_WebServer(void* pv)
{
    while (true)
    {
        wifiMgr.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}