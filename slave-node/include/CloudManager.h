#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include "esp_camera.h"
#include "base64.h"
#include "Config.h"

class CloudManager {
public:
    CloudManager();
    
    bool sendTelegramPhoto(camera_fb_t* fb, String caption, String botToken, String chatId);
    
    bool sendAWS(camera_fb_t* fb);
};

#endif