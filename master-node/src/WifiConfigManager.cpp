#include "WifiConfigManager.h"

// Default IP for Captive Portal
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

WifiConfigManager::WifiConfigManager() : server(80) {}

void WifiConfigManager::beginAP() {
    EEPROM.begin(EEPROM_SIZE);
    
    WiFi.disconnect(true); // Ngắt hoàn toàn chức năng Thu (STA) đang chạy ngầm
    delay(100);            // Chờ 0.1s để Radio ổn định
    WiFi.mode(WIFI_AP);    // Ép chip CHỈ hoạt động ở chế độ Phát (Access Point)

    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32_SmartHome", ""); // SSID, no Password

    // Initialize DNS Server for Captive Portal (Turn-on Web automatically when connect WiFi)
    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", HTTP_GET, std::bind(&WifiConfigManager::handleRoot, this));
    server.on("/save-wifi", HTTP_GET, std::bind(&WifiConfigManager::handleSaveWiFi, this));
    server.on("/save-blynk", HTTP_GET, std::bind(&WifiConfigManager::handleSaveBlynk, this));
    server.on("/save-telegram", HTTP_GET, std::bind(&WifiConfigManager::handleSaveTelegram, this));
    server.onNotFound(std::bind(&WifiConfigManager::handleNotFound, this));

    server.begin();
}

void WifiConfigManager::loop() {
    dnsServer.processNextRequest();
    server.handleClient();
}

void WifiConfigManager::handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>System Configuration</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }
        body { background: linear-gradient(-45deg, #ff4444, #ff6b35, #ffa500, #4caf50, #2196f3); background-size: 400% 400%; animation: gradientShift 15s ease infinite; display: flex; justify-content: center; align-items: center; min-height: 100vh; padding: 20px; }
        @keyframes gradientShift { 0% { background-position: 0% 50%; } 50% { background-position: 100% 50%; } 100% { background-position: 0% 50%; } }
        .container { background: rgba(255, 255, 255, 0.95); backdrop-filter: blur(20px); border-radius: 20px; padding: 30px; width: 100%; max-width: 500px; box-shadow: 0 20px 50px rgba(0,0,0,0.3); }
        h2 { text-align: center; color: #333; margin-bottom: 20px; font-size: 24px; }
        
        /* Tab CSS */
        .tab { display: flex; justify-content: space-between; margin-bottom: 20px; background: #f1f1f1; border-radius: 10px; overflow: hidden; }
        .tab button { flex: 1; background-color: inherit; border: none; outline: none; cursor: pointer; padding: 14px 10px; transition: 0.3s; font-size: 16px; font-weight: bold; color: #555; }
        .tab button:hover { background-color: #ddd; }
        .tab button.active { background-color: #ff6b35; color: white; }
        
        .tabcontent { display: none; animation: fadeEffect 0.5s; }
        @keyframes fadeEffect { from {opacity: 0;} to {opacity: 1;} }
        
        /* Form CSS */
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 8px; font-weight: bold; color: #444; font-size: 14px; }
        input { width: 100%; padding: 12px 15px; border: 2px solid #ddd; border-radius: 10px; font-size: 16px; transition: 0.3s; }
        input:focus { border-color: #ff6b35; outline: none; }
        button.submit-btn { width: 100%; padding: 15px; background: linear-gradient(135deg, #ff4444, #ff6b35); border: none; border-radius: 10px; color: white; font-size: 18px; font-weight: bold; cursor: pointer; transition: 0.3s; margin-top: 10px; }
        button.submit-btn:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(255, 107, 53, 0.4); }
    </style>
</head>
<body>
    <div class="container">
        <h2>⚙️ System Setup</h2>
        
        <div class="tab">
            <button class="tablinks" onclick="openTab(event, 'WiFi')" id="defaultOpen">📡 WiFi</button>
            <button class="tablinks" onclick="openTab(event, 'Blynk')">🟢 Blynk</button>
            <button class="tablinks" onclick="openTab(event, 'Telegram')">✈️ Telegram</button>
        </div>

        <div id="WiFi" class="tabcontent">
            <form action="/save-wifi" method="GET">
                <div class="form-group">
                    <label>Network SSID (Tên WiFi)</label>
                    <input type="text" name="ssid" required placeholder="Nhập tên WiFi...">
                </div>
                <div class="form-group">
                    <label>Password (Mật khẩu)</label>
                    <input type="password" name="pass" placeholder="Để trống nếu WiFi không có pass">
                </div>
                <button type="submit" class="submit-btn">Lưu WiFi & Khởi động lại</button>
            </form>
        </div>

        <div id="Blynk" class="tabcontent">
            <form action="/save-blynk" method="GET">
                <div class="form-group">
                    <label>Blynk Auth Token</label>
                    <input type="text" name="token" required placeholder="Dán 32 ký tự Token vào đây..." maxlength="32">
                </div>
                <button type="submit" class="submit-btn">Lưu Blynk & Khởi động lại</button>
            </form>
        </div>

        <div id="Telegram" class="tabcontent">
            <form action="/save-telegram" method="GET">
                <div class="form-group">
                    <label>Telegram Bot Token</label>
                    <input type="text" name="bot_token" required placeholder="VD: 123456789:ABCdefGHI...">
                </div>
                <div class="form-group">
                    <label>Telegram Chat ID</label>
                    <input type="text" name="chat_id" required placeholder="VD: 123456789">
                </div>
                <button type="submit" class="submit-btn">Lưu Telegram & Khởi động lại</button>
            </form>
        </div>
    </div>

    <script>
        function openTab(evt, tabName) {
            var i, tabcontent, tablinks;
            tabcontent = document.getElementsByClassName("tabcontent");
            for (i = 0; i < tabcontent.length; i++) { tabcontent[i].style.display = "none"; }
            tablinks = document.getElementsByClassName("tablinks");
            for (i = 0; i < tablinks.length; i++) { tablinks[i].className = tablinks[i].className.replace(" active", ""); }
            document.getElementById(tabName).style.display = "block";
            evt.currentTarget.className += " active";
        }
        document.getElementById("defaultOpen").click();
    </script>
</body>
</html>
    )rawliteral";
    
    server.send(200, "text/html", html);
}

void WifiConfigManager::handleSaveWiFi() {
    if (server.hasArg("ssid")) {
        saveStringToEEPROM(ADDR_SSID, server.arg("ssid"), 32);
        saveStringToEEPROM(ADDR_PASS, server.arg("pass"), 64);
        server.send(200, "text/html", "<h2>Da luu WiFi! He thong dang khoi dong lai...</h2>");
        delay(2000);
        ESP.restart();
    }
}

void WifiConfigManager::handleSaveBlynk() {
    if (server.hasArg("token")) {
        saveStringToEEPROM(ADDR_BLYNK, server.arg("token"), 32);
        server.send(200, "text/html", "<h2>Da luu Blynk Token! He thong dang khoi dong lai...</h2>");
        delay(2000);
        ESP.restart();
    }
}

void WifiConfigManager::handleSaveTelegram() {
    if (server.hasArg("bot_token") && server.hasArg("chat_id")) {
        saveStringToEEPROM(ADDR_TELE_BOT, server.arg("bot_token"), 64);
        saveStringToEEPROM(ADDR_TELE_CHAT, server.arg("chat_id"), 32);
        server.send(200, "text/html", "<h2>Da luu Telegram! He thong dang khoi dong lai...</h2>");
        delay(2000);
        ESP.restart();
    }
}

void WifiConfigManager::handleNotFound() {
    // Captive Portal technique: Redirect all unexpected requests to the homepage
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void WifiConfigManager::saveStringToEEPROM(int addr, String data, int maxLength) {
    for (int i = 0; i < maxLength; ++i) {
        if (i < data.length()) {
            EEPROM.write(addr + i, data[i]);
        } 
        else {
            EEPROM.write(addr + i, 0); // Write NULL to remove previous data
        }
    }

    EEPROM.commit();
}

String WifiConfigManager::readStringFromEEPROM(int addr, int maxLength) {
    EEPROM.begin(EEPROM_SIZE);
    String result = "";

    for (int i = 0; i < maxLength; ++i) {
        char c = char(EEPROM.read(addr + i));
        if (c == 0 || c == 255) break;  // End string or empty
        result += c;
    }

    return result;
}

void WifiConfigManager::loadConfigurations(String &ssid, String &pass, String &blynk, String &bot, String &chat) {
    ssid = readStringFromEEPROM(ADDR_SSID, 32);
    pass = readStringFromEEPROM(ADDR_PASS, 64);
    blynk = readStringFromEEPROM(ADDR_BLYNK, 32);
    bot = readStringFromEEPROM(ADDR_TELE_BOT, 64);
    chat = readStringFromEEPROM(ADDR_TELE_CHAT, 32);
}