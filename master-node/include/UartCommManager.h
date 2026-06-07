#ifndef UART_COMM_MANAGER_H
#define UART_COMM_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

void sendWiFiConfig(String ssid, String pass);
class UartCommManager {
public:
    UartCommManager();
    
    // Initialize UART2
    void begin();
    
    void sendStatus(const char* cmd, const char* type, int gasValue);
    
    void sendSnapshotRequest();

    void sendWiFiConfig(String ssid, String pass, String botToken, String chatId);
};

#endif 