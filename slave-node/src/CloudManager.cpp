#include "CloudManager.h"

CloudManager::CloudManager() {}

bool CloudManager::sendTelegramPhoto(camera_fb_t* fb, String caption) {
    if (!fb) return false;

    WiFiClientSecure client;
    client.setInsecure();   // Bypass SSL certificate verification (Required for microcontrollers)

    Serial.println("[INFO] Connecting to Telegram API...");
    if (!client.connect("api.telegram.org", 443)) {
        Serial.println("[ERROR] Unable to connect to Telegram!");
        return false;
    }

    // Build the HTTP header and payload for multipart/form-data
    String head = "--Boundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(TG_CHAT_ID) + 
                  "\r\n--Boundary\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption +
                  "\r\n--Boundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"fire.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Boundary--\r\n";
    
    uint32_t totalLen = head.length() + fb->len + tail.length();
    
    client.println("POST /bot" + String(TG_TOKEN) + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: multipart/form-data; boundary=Boundary");
    client.print("Content-Length: "); 
    client.println(totalLen);
    client.println();
    
    // Send data sequentially in chunks to avoid overflowing the ESP32 RAM
    client.print(head);
    client.write(fb->buf, fb->len);
    client.print(tail);
    
    client.stop();
    Serial.println("[INFO] Image sent to Telegram successfully!");
    return true;
}

bool CloudManager::sendAWS(camera_fb_t* fb) {
    if (!fb) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.println("[INFO] Connecting to AWS API...");
    if (!http.begin(client, AWS_API_URL)) {
        Serial.println("[ERROR] Failed to initialize AWS connection!");
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    // Encode the image buffer into Base64 format
    String base64Image = base64::encode(fb->buf, fb->len);
    
    // Create JSON payload using ArduinoJson v7
    JsonDocument doc;
    doc["report_id"] = "NODE_S3_01";    // Convert to the device's standardized ID
    doc["image"] = base64Image;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // Send HTTP POST request
    int httpCode = http.POST(jsonPayload);
    http.end();

    if (httpCode > 0) {
        Serial.printf("[INFO] AWS responded with status code: %d\n", httpCode);
        return (httpCode == HTTP_CODE_OK || httpCode == 200);
    } 
    else {
        Serial.printf("[ERROR] Failed to send AWS POST request: %s\n", http.errorToString(httpCode).c_str());
        return false;
    }
}