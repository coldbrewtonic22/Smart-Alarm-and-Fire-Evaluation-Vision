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

#define DEFAULT_GAS_THRESH  2200
#define GAS_HYSTERESIS      100
#define DEFAULT_PIN_CODE    "001"

// Servo Angles
#define SERVO_CLOSE_ANGLE   0
#define SERVO_OPEN_ANGLE    90

// 3. SYSTEM STATES (ENUMS)

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

#endif