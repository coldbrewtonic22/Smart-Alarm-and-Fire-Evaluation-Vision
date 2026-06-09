#include "CloudManager.h"

CloudManager::CloudManager() {}

bool CloudManager::sendTelegramPhoto(camera_fb_t* fb, String caption, String botToken, String chatId) {
    if (!fb) {
        return false;
    }

    Serial.printf("[INFO] Telegram sendPhoto start, heap: %u\n", ESP.getFreeHeap());

    IPAddress telegramIP;
    if (!WiFi.hostByName("api.telegram.org", telegramIP)) {
        Serial.println("[ERROR] DNS lookup failed for api.telegram.org");
        return false;
    }
    Serial.print("[INFO] api.telegram.org resolved: ");
    Serial.println(telegramIP);

    WiFiClientSecure client;
    client.setInsecure();   // Bypass SSL certificate verification (Required for microcontrollers)
    client.setTimeout(20);

    Serial.println("[INFO] Connecting to Telegram API...");

    if (!client.connect(telegramIP, 443)) {
        Serial.println("[ERROR] Cannot connect to Telegram IP:443");
        return false;
    }

    client.setHandshakeTimeout(10);

    String head = "--Boundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + chatId + 
                  "\r\n--Boundary\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption +
                  "\r\n--Boundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"fire.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--Boundary--\r\n";
    
    uint32_t totalLen = head.length() + fb->len + tail.length();
    
    // Gửi HTTP header
    client.print("POST /bot" + botToken + "/sendPhoto HTTP/1.1\r\n");
    client.print("Host: api.telegram.org\r\n");
    client.print("Content-Type: multipart/form-data; boundary=Boundary\r\n");
    client.print("Content-Length: " + String(totalLen) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    
    // Send data sequentially in chunks to avoid overflowing the ESP32 RAM
    client.print(head);

    const size_t CHUNK = 1024;
    size_t sent = 0;

    while (sent < fb->len) {
        size_t toSend = min((size_t)CHUNK, fb->len - sent);
        client.write(fb->buf + sent, toSend);
        sent += toSend;
    }

    client.print(tail);
    
    // Wait for the Telegram server response before disconnecting
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 15000) {
            Serial.println("[ERROR] Telegram response timeout!");
            client.stop();

            return false;
        }

        delay(10);
    }
    
    // The first HTTP status line is sufficient to confirm success (e.g., HTTP/1.1 200 OK)
    String response = client.readStringUntil('\n');
    Serial.println("[INFO] Telegram Response: " + response);

    while (client.available()) {
        client.read();
    }

    client.stop();
    
    bool ok = response.indexOf("200") >= 0;
    Serial.printf("[INFO] Telegram result: %s\n", ok ? "OK" : "FAILED");

    return ok;
}

bool CloudManager::sendAWS(camera_fb_t* fb) {
    if (!fb) {
        return false;
    }

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
    doc["report_id"] = "NODE_CAM_01";
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
        Serial.printf("[ERROR] AWS POST failed: %s\n", http.errorToString(httpCode).c_str());
        
        return false;
    }
}