#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <Arduino.h>

#include "esp_camera.h"
#include "Config.h"

class CameraManager {
public:
    CameraManager();
    
    bool begin();
    
    // Turn the flash LED on/off
    void setFlash(bool on);
    
    // Clear the old buffer (to avoid retrieving stale images)
    void clearBuffer();

    // Capture an image and return the frame buffer pointer
    camera_fb_t* capture();
    
    // Release memory after image processing is complete
    void freeFrame(camera_fb_t* fb);
};

#endif