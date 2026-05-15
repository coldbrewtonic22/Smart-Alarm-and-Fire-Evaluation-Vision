#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "base64.h"
#include "Config.h"

class CloudManager {
public:
    CloudManager();
    
    // Send the image with a caption via Telegram
    bool sendTelegramPhoto(camera_fb_t* fb, String caption);
    
    // Upload the image to the AWS server
    bool sendAWS(camera_fb_t* fb);
};

#endif