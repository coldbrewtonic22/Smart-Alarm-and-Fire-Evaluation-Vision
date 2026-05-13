#include "UartCommManager.h"

UartCommManager::UartCommManager() {}

void UartCommManager::begin() {
    // Using ESP32's Hardware UART2
    Serial2.begin(SERIAL_COMM_BAUD, SERIAL_8N1, PIN_UART2_RX, PIN_UART2_TX);
}

void UartCommManager::sendStatus(const char* cmd, const char* type, int gasValue) {
    // Using ArduinoJSON v7
    JsonDocument doc;
    
    doc["cmd"] = cmd;
    
    // Only add "type" if it's not empty
    if (type != nullptr && strlen(type) > 0) {
        doc["type"] = type;
    }
    
    doc["gas"] = gasValue;

    // Serialize JSON directly into the Serial2 buffer
    serializeJson(doc, Serial2);
    
    // Add \n so Slave knows string is end
    Serial2.println(); 
}

void UartCommManager::sendSnapshotRequest() {
    JsonDocument doc;
    doc["cmd"] = "SNAPSHOT";
    
    serializeJson(doc, Serial2);
    Serial2.println();
}