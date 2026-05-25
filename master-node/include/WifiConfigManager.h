#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include "Config.h"

class WifiConfigManager {
private:
    WebServer server;
    DNSServer dnsServer;

    void handleRoot();
    void handleNotFound();
    void sendSuccessPage();
    void handleSaveConfig();
    void sendErrorPage(String errorMessage);
    
    void saveStringToEEPROM(int addr, String data, int maxLength);
    String readStringFromEEPROM(int addr, int maxLength);

public:
    WifiConfigManager();
    void beginAP();
    void loop();
    
    void loadConfigurations(String &ssid, String &pass, String &blynk, String &bot, String &chat);
};

#endif