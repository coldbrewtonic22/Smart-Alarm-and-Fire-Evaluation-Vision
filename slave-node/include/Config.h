#ifndef SLAVE_CONFIG_H
#define SLAVE_CONFIG_H

#include <Arduino.h>

// 1. PIN DEFINITIONS (S3-CAM HARDWARE)

// Use pins that do not conflict with the camera for UART1
#define PIN_UART1_RX       43 
#define PIN_UART1_TX       44 

// Change the Flash LED pin
#define FLASH_PIN          48 

// Standard camera pin mapping for the ESP32-S3 board (OV5640)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM      4
#define SIOC_GPIO_NUM      5
#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM        8
#define Y3_GPIO_NUM        9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM     6
#define HREF_GPIO_NUM      7
#define PCLK_GPIO_NUM     13

// 2. CLOUD CONFIGURATIONS

#define TG_TOKEN        "8482773700:AAEHaS262jfhuiuB-lnwhSgD6TQZ41iv4yM"
#define TG_CHAT_ID      "7257541474"
#define AWS_API_URL     "https://4249qf98qj.execute-api.ap-southeast-1.amazonaws.com/default/LuuAnhBaoChay"

// 3. SYSTEM CONSTANTS

#define COOLDOWN_TIME      15000    // 15-second cooldown between captures

#endif