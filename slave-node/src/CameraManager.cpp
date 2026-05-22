#include "CameraManager.h"

CameraManager::CameraManager() {}

bool CameraManager::begin() {
    pinMode(FLASH_PIN, OUTPUT);
    digitalWrite(FLASH_PIN, LOW);

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    // Configure image quality
    config.frame_size = FRAMESIZE_VGA;  // Giữ nguyên VGA (640x480) để tốc độ truyền Telegram nhanh
    config.jpeg_quality = 15;           // Tăng lên 15 (Số càng cao chất lượng càng thấp nhưng dung lượng file nhẹ, tránh tràn RAM)
    config.fb_count = 1;                // Chỉ dùng 1 bộ đệm

    esp_err_t err = esp_camera_init(&config);

    if (err != ESP_OK) {
        Serial.printf("[ERROR] Camera init failed with error 0x%x\n", err);

        return false;
    }
    
    Serial.println("[INFO] Camera initialized successfully");

    return true;
}

void CameraManager::setFlash(bool on) {
    digitalWrite(FLASH_PIN, on ? HIGH : LOW);
}

void CameraManager::clearBuffer() {
    // Call the image capture function and immediately return it to refresh the frame buffer
    camera_fb_t* fb = esp_camera_fb_get(); 
    if (fb) {
        esp_camera_fb_return(fb); 
    }
}