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
    
    String webSsid, webPass, webBlynk, webTeleBot, webTeleChat;

    void handleRoot();
    void handleSaveConfig();
    void handleNotFound();
    
    void saveStringToEEPROM(int addr, String data, int maxLength);
    String readStringFromEEPROM(int addr, int maxLength);

public:
    WifiConfigManager();
    void beginAP();
    void loop();
    
    void loadConfigurations(String &ssid, String &pass, String &blynk, String &bot, String &chat);
};

#endif