#ifndef SLAVE_CONFIG_H
#define SLAVE_CONFIG_H

#include <Arduino.h>

// 1. PIN DEFINITIONS (ESP32-CAM AI-THINKER)

#define PIN_UART1_RX      12 
#define PIN_UART1_TX      13 

#define FLASH_PIN          4 

// Sơ đồ chân Camera chuẩn AI-THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// 2. CLOUD CONFIGURATIONS

#define TG_TOKEN        "8482773700:AAEHaS262jfhuiuB-lnwhSgD6TQZ41iv4yM"
#define TG_CHAT_ID      "7257541474"
#define AWS_API_URL     "https://4249qf98qj.execute-api.ap-southeast-1.amazonaws.com/default/LuuAnhBaoChay"

// 3. SYSTEM CONSTANTS

#define COOLDOWN_TIME     15000 

#endif