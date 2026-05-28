#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// 1. PIN DEFINITIONS (HARDWARE MAPPING)

// --- Sensors ---
#define PIN_MQ2         35
#define PIN_FLAME       34

// --- Actuators ---
#define PIN_RELAY_FAN   18
#define PIN_RELAY_PUMP  5
#define PIN_SERVO       33
#define PIN_BUZZER      23
#define PIN_LED         19

// --- Displays & Communication ---
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22
#define PIN_UART2_RX    16
#define PIN_UART2_TX    17

// --- Keypad 4x4 ---
#define KP_ROW_1        13
#define KP_ROW_2        12
#define KP_ROW_3        14
#define KP_ROW_4        27
#define KP_COL_1        26
#define KP_COL_2        25
#define KP_COL_3        32
#define KP_COL_4        4

// 2. SYSTEM CONSTANTS & PARAMETERS

#define SERIAL_DEBUG_BAUD   115200
#define SERIAL_COMM_BAUD    115200

#define DEFAULT_GAS_THRESH  1200
#define GAS_HYSTERESIS      100
#define DEFAULT_PIN_CODE    "2212"

// Servo Angles
#define SERVO_CLOSE_ANGLE   0
#define SERVO_OPEN_ANGLE    90

// 3. SYSTEM STATES

enum SystemMode {
    MODE_MANUAL,
    MODE_AUTO
};

enum AlertState {
    STATE_SAFE,
    STATE_GAS_ONLY,
    STATE_FIRE_ONLY,
    STATE_EMERGENCY    // Gas and Fire
};

// 4. LCD & UI CONFIGURATIONS

#define LCD_COLS        20
#define LCD_ROWS        4
#define LCD_ADDR        0x27

// 5. EEPROM MEMORY MAP

#define EEPROM_SIZE     512
#define ADDR_SSID       0     // 32 bytes (0-31)
#define ADDR_PASS       32    // 64 bytes (32-95)
#define ADDR_BLYNK      96    // 32 bytes (96-127)
#define ADDR_TELE_BOT   128   // 64 bytes (128-191) - Bot Token (≈ 46 chars)
#define ADDR_TELE_CHAT  192   // 32 bytes (192-223) - Chat ID (≈ 10-15 chars)
#define ADDR_MODE       224   // 1 byte   (Auto/Manual)
#define ADDR_THRESH_H   225   // 1 byte   (Threshold High)
#define ADDR_THRESH_L   226   // 1 byte   (Threshold Low)

// 6. SYSTEM INFO

#define GROUP_NAME      "ESPecially-Talented"
#define CLASS_ID        "D23CQCI01-N"

// 7. BLYNK CLOUD CONFIGURATION

#define BLYNK_TEMPLATE_ID       "TMPL6PxCh11q_"
#define BLYNK_TEMPLATE_NAME     "Smart Alarm and Fire Evaluation Vision"

#define GAS_PIN           V0
#define RELAY_PIN         V1
#define SERVO_PIN         V2
#define THRESHOLD_PIN     V3
#define MODE_PIN          V4
#define FIRE_PIN          V5

#endif