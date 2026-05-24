#include "WifiConfigManager.h"

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

WifiConfigManager::WifiConfigManager() : server(80) {}

void WifiConfigManager::beginAP() {
    EEPROM.begin(EEPROM_SIZE);
    
    WiFi.disconnect(true); 
    delay(100);            
    WiFi.mode(WIFI_AP);    

    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32_SmartHome", ""); 

    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", HTTP_GET, std::bind(&WifiConfigManager::handleRoot, this));
    server.on("/save-config", HTTP_GET, std::bind(&WifiConfigManager::handleSaveConfig, this));
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
        .tab { display: flex; justify-content: space-between; margin-bottom: 20px; background: #f1f1f1; border-radius: 10px; overflow: hidden; }
        .tab button { flex: 1; background-color: inherit; border: none; outline: none; cursor: pointer; padding: 14px 10px; transition: 0.3s; font-size: 16px; font-weight: bold; color: #555; }
        .tab button:hover { background-color: #ddd; }
        .tab button.active { background-color: #ff6b35; color: white; }
        .tabcontent { display: none; animation: fadeEffect 0.5s; }
        @keyframes fadeEffect { from {opacity: 0;} to {opacity: 1;} }
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
        
        <form action="/save-config" method="GET">
            <div class="tab">
                <button type="button" class="tablinks" onclick="openTab(event, 'WiFi')" id="defaultOpen">📡 WiFi</button>
                <button type="button" class="tablinks" onclick="openTab(event, 'Blynk')">🟢 Blynk</button>
                <button type="button" class="tablinks" onclick="openTab(event, 'Telegram')">✈️ Telegram</button>
            </div>

            <div id="WiFi" class="tabcontent">
                <div class="form-group">
                    <label>Network SSID (Tên WiFi)</label>
                    <input type="text" name="ssid" placeholder="Nhập tên WiFi...">
                </div>
                <div class="form-group">
                    <label>Password (Mật khẩu)</label>
                    <input type="password" name="pass" placeholder="Để trống nếu không có pass">
                </div>
            </div>

            <div id="Blynk" class="tabcontent">
                <div class="form-group">
                    <label>Blynk Auth Token</label>
                    <input type="text" name="token" placeholder="Dán 32 ký tự Token vào đây..." maxlength="32">
                </div>
            </div>

            <div id="Telegram" class="tabcontent">
                <div class="form-group">
                    <label>Telegram Bot Token</label>
                    <input type="text" name="bot_token" placeholder="VD: 123456789:ABCdefGHI...">
                </div>
                <div class="form-group">
                    <label>Telegram Chat ID</label>
                    <input type="text" name="chat_id" placeholder="VD: 123456789">
                </div>
            </div>
            
            <button type="submit" class="submit-btn">Lưu Toàn Bộ Cấu Hình</button>
        </form>
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

void WifiConfigManager::handleSaveConfig() {
    bool hasData = false;

    // Lưu WiFi
    if (server.hasArg("ssid") && server.arg("ssid") != "") {
        saveStringToEEPROM(ADDR_SSID, server.arg("ssid"), 32);
        saveStringToEEPROM(ADDR_PASS, server.arg("pass"), 64);
        hasData = true;
    }
    
    // Lưu Blynk
    if (server.hasArg("token") && server.arg("token") != "") {
        saveStringToEEPROM(ADDR_BLYNK, server.arg("token"), 32);
        hasData = true;
    }

    // Lưu Telegram
    if (server.hasArg("bot_token") && server.arg("bot_token") != "") {
        saveStringToEEPROM(ADDR_TELE_BOT, server.arg("bot_token"), 64);
        if (server.hasArg("chat_id")) {
            saveStringToEEPROM(ADDR_TELE_CHAT, server.arg("chat_id"), 32);
        }
        hasData = true;
    }

    if (hasData) {
        String successHtml = R"rawliteral(
            <!DOCTYPE html>
            <html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>body{text-align:center;font-family:sans-serif;background:#e8f5e9;padding-top:50px;}
            .box{background:#fff;padding:30px;border-radius:15px;box-shadow:0 10px 30px rgba(0,0,0,0.1);display:inline-block;}</style>
            </head><body><div class="box">
            <h2 style="color:#4caf50;">✅ Đã lưu cấu hình thành công!</h2>
            <p style="color:#555;">Hệ thống đang khởi động lại để kết nối...<br>Bạn có thể đóng trang Web này.</p>
            </div></body></html>
        )rawliteral";
        
        server.send(200, "text/html", successHtml);
        
        delay(1500); 
        ESP.restart();
    }
    else {
        server.send(400, "text/html", "<h2>Vui lòng nhập ít nhất 1 thông tin (WiFi hoặc Blynk)!</h2><br><a href='/'>Quay lai</a>");
    }
}

void WifiConfigManager::handleNotFound() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void WifiConfigManager::saveStringToEEPROM(int addr, String data, int maxLength) {
    for (int i = 0; i < maxLength; ++i) {
        if (i < data.length()) {
            EEPROM.write(addr + i, data[i]);
        } 
        else {
            EEPROM.write(addr + i, 0); 
        }
    }

    EEPROM.commit();
}

String WifiConfigManager::readStringFromEEPROM(int addr, int maxLength) {
    EEPROM.begin(EEPROM_SIZE);
    String result = "";

    for (int i = 0; i < maxLength; ++i) {
        char c = char(EEPROM.read(addr + i));

        if (c == 0 || c == 255) break;  

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