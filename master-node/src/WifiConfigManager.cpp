#include "WifiConfigManager.h"

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

WifiConfigManager::WifiConfigManager() : server(80) {}

void WifiConfigManager::beginAP() 
{
    WiFi.disconnect(true); 
    delay(100);            
    WiFi.mode(WIFI_AP);    

    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("ESP32_SmartHome", ""); 

    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", HTTP_GET, std::bind(&WifiConfigManager::handleRoot, this));
    server.on("/save-config", HTTP_POST, std::bind(&WifiConfigManager::handleSaveConfig, this));
    server.on("/delete-telegram", HTTP_GET, std::bind(&WifiConfigManager::handleDeleteTelegram, this));
    server.onNotFound(std::bind(&WifiConfigManager::handleNotFound, this));

    server.begin();
}

void WifiConfigManager::loop() 
{
    dnsServer.processNextRequest();
    server.handleClient();
}

void WifiConfigManager::handleRoot() 
{
    // Read the current state from EEPROM to render the dynamic UI
    String currentSsid  = readStringFromEEPROM(ADDR_SSID, 32);
    String currentToken = readStringFromEEPROM(ADDR_BLYNK, 32);
    String currentBot   = readStringFromEEPROM(ADDR_TELE_BOT, 64);
    
    bool hasWiFi  = currentSsid.length() > 0;
    bool hasBlynk = currentToken.length() == 32;
    bool hasTele  = currentBot.length() > 0;

    String ssidPlaceholder = hasWiFi ? "Đang dùng: " + currentSsid + " (Bỏ trống để giữ nguyên)" : "Nhập tên WiFi (Bắt buộc cho lần đầu)";
    String blynkPlaceholder = hasBlynk ? "Đang dùng Token cũ (Bỏ trống để giữ nguyên)" : "Dán 32 ký tự Token vào đây (Bắt buộc)";
    
    String teleStatus = hasTele ? "<div class='tele-status'>Trạng thái: <span style='color:#4caf50; font-weight:bold;'>Đã cấu hình 🟢</span></div>" : "<div class='tele-status'>Trạng thái: <span style='color:#999; font-weight:bold;'>Chưa cấu hình (Đang tắt) ⚪</span></div>";
    String teleDeleteBtn = hasTele ? "<a href='/delete-telegram' class='delete-btn' onclick=\"return confirm('Bạn có chắc chắn muốn xóa cấu hình Telegram không? Chức năng gửi ảnh sẽ bị vô hiệu hóa.');\">🗑️ Xóa cấu hình Telegram</a>" : "";

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
        .tele-status { margin-bottom: 15px; font-size: 15px; background: #f9f9f9; padding: 12px; border-radius: 8px; text-align: center; border: 1px dashed #ccc;}
        .delete-btn { display: block; text-align: center; margin-top: 15px; padding: 12px; background: #ffebee; color: #f44336; text-decoration: none; border-radius: 10px; font-weight: bold; transition: 0.3s; border: 1px solid #ffcdd2;}
        .delete-btn:hover { background: #ffcdd2; }
        button.submit-btn { width: 100%; padding: 15px; background: linear-gradient(135deg, #ff4444, #ff6b35); border: none; border-radius: 10px; color: white; font-size: 18px; font-weight: bold; cursor: pointer; transition: 0.3s; margin-top: 10px; }
        button.submit-btn:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(255, 107, 53, 0.4); }
    </style>
</head>
<body>
    <div class="container">
        <h2>⚙️ System Setup</h2>
        
        <form action="/save-config" method="POST" onsubmit="return handleSubmit()">
            <div class="tab">
                <button type="button" class="tablinks" onclick="openTab(event, 'WiFi')" id="defaultOpen">📡 WiFi</button>
                <button type="button" class="tablinks" onclick="openTab(event, 'Blynk')">🟢 Blynk</button>
                <button type="button" class="tablinks" onclick="openTab(event, 'Telegram')">✈️ Telegram</button>
            </div>

            <div id="WiFi" class="tabcontent">
                <div class="form-group">
                    <label>Network SSID (Tên WiFi)</label>
                    <input type="text" name="ssid" placeholder=")rawliteral" + ssidPlaceholder + R"rawliteral(">
                </div>
                <div class="form-group">
                    <label>Password (Mật khẩu)</label>
                    <input type="password" name="pass" placeholder="Nhập pass mới hoặc để trống">
                </div>
            </div>

            <div id="Blynk" class="tabcontent">
                <div class="form-group">
                    <label>Blynk Auth Token</label>
                    <input type="text" name="token" placeholder=")rawliteral" + blynkPlaceholder + R"rawliteral(" maxlength="32">
                </div>
            </div>

            <div id="Telegram" class="tabcontent">
                )rawliteral" + teleStatus + R"rawliteral(
                <div class="form-group">
                    <label>Telegram Bot Token</label>
                    <input type="text" name="bot_token" placeholder="VD: 123456789:ABCdefGHI... (Tùy chọn)">
                </div>
                <div class="form-group">
                    <label>Telegram Chat ID</label>
                    <input type="text" name="chat_id" placeholder="VD: 123456789 (Tùy chọn)">
                </div>
                )rawliteral" + teleDeleteBtn + R"rawliteral(
            </div>
            
            <button type="submit" class="submit-btn">Lưu Cấu Hình Mới</button>
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
        
        function handleSubmit() {
            const btn = document.querySelector(".submit-btn");
            btn.innerHTML = "⏳ Đang xử lý...";
            btn.style.opacity = "0.7";
            return true;
        }
    </script>
</body>
</html>
    )rawliteral";
    
    server.send(200, "text/html", html);
}

void WifiConfigManager::handleSaveConfig() 
{
    String ssid      = server.arg("ssid");
    String pass      = server.arg("pass");
    String token     = server.arg("token");
    String bot_token = server.arg("bot_token");
    String chat_id   = server.arg("chat_id");

    String currentSsid  = readStringFromEEPROM(ADDR_SSID, 32);
    String currentToken = readStringFromEEPROM(ADDR_BLYNK, 32);

    // 1. Validation for the initial startup
    if (currentSsid.length() == 0 && ssid.length() == 0) 
    {
        sendErrorPage("Lần đầu khởi động: Tên WiFi (SSID) không được để trống!");
        return;
    }
    if (currentToken.length() != 32 && token.length() == 0) 
    {
        sendErrorPage("Lần đầu khởi động: Blynk Token không được để trống!");
        return;
    }

    // 2. Validate token length (if new data is entered)
    if (token.length() > 0 && token.length() != 32) 
    {
        sendErrorPage("Blynk Token phải có đúng 32 ký tự! (Hiện tại: " + String(token.length()) + ")");
        return;
    }

    // 3. Handle overwrite logic (Only save if the input field is not empty)
    bool updated = false;

    if (ssid.length() > 0) 
    {
        saveStringToEEPROM(ADDR_SSID, ssid, 32);
        saveStringToEEPROM(ADDR_PASS, pass, 64);    // Password can be empty
        updated = true;
    }
    
    if (token.length() == 32) 
    {
        saveStringToEEPROM(ADDR_BLYNK, token, 32);
        updated = true;
    }

    if (bot_token.length() > 0) 
    {
        saveStringToEEPROM(ADDR_TELE_BOT, bot_token, 64);

        if (chat_id.length() > 0) 
        {
            saveStringToEEPROM(ADDR_TELE_CHAT, chat_id, 32);
        }

        updated = true;
    }

    // 4. Trả về kết quả
    if (updated || currentSsid.length() > 0) 
    {
        sendSuccessPage("Lưu Cấu Hình Thành Công!");

        delay(2000); 
        ESP.restart();
    } 
    else 
    {
        sendErrorPage("Lỗi hệ thống: Không có dữ liệu nào được lưu.");
    }
}

void WifiConfigManager::handleDeleteTelegram() 
{
    saveStringToEEPROM(ADDR_TELE_BOT, "", 64);
    saveStringToEEPROM(ADDR_TELE_CHAT, "", 32);
    
    sendSuccessPage("Đã Xóa Cấu Hình Telegram!");

    delay(2000);
    ESP.restart();
}

void WifiConfigManager::sendSuccessPage(String message) 
{
    String html = R"rawliteral(
        <!DOCTYPE html>
        <html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
        <style>body{text-align:center;font-family:'Segoe UI',sans-serif;background:#e8f5e9;padding-top:50px;margin:0;}
        .box{background:#fff;padding:40px;border-radius:20px;box-shadow:0 10px 30px rgba(0,0,0,0.1);display:inline-block;max-width:90%;}</style>
        </head><body><div class="box">
        <h1 style="color:#4caf50;font-size:60px;margin:0;">✅</h1>
        <h2 style="color:#333;margin-top:20px;">)rawliteral" + message + R"rawliteral(</h2>
        <p style="color:#666;font-size:16px;">Hệ thống đang khởi động lại để áp dụng cài đặt...<br>Bạn có thể đóng trang Web này.</p>
        </div></body></html>
    )rawliteral";
    server.send(200, "text/html", html);
}

void WifiConfigManager::sendErrorPage(String message) 
{
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<style>body{text-align:center;font-family:'Segoe UI',sans-serif;background:#ffebee;padding-top:50px;margin:0;}";
    html += ".box{background:#fff;padding:40px;border-radius:20px;box-shadow:0 10px 30px rgba(0,0,0,0.1);display:inline-block;max-width:90%;}";
    html += ".btn{background:#f44336;color:#fff;padding:12px 25px;border:none;border-radius:8px;font-size:16px;cursor:pointer;margin-top:20px;text-decoration:none;display:inline-block;}</style>";
    html += "</head><body><div class='box'><h1 style='color:#f44336;font-size:60px;margin:0;'>❌</h1>";
    html += "<h2 style='color:#333;margin-top:20px;'>Cấu Hình Thất Bại!</h2>";
    html += "<p style='color:#666;font-size:16px;'>" + message + "</p>";
    html += "<a href='/' class='btn'>Quay Lại Bảng Điều Khiển</a></div></body></html>";
    server.send(400, "text/html", html);
}

void WifiConfigManager::handleNotFound() 
{
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void WifiConfigManager::saveStringToEEPROM(int addr, String data, int maxLength) 
{
    for (int i = 0; i < maxLength; ++i) 
    {
        if (i < data.length()) 
        {
            EEPROM.write(addr + i, data[i]);
        } 
        else 
        {
            EEPROM.write(addr + i, 0); 
        }
    }

    EEPROM.commit();
}

String WifiConfigManager::readStringFromEEPROM(int addr, int maxLength) 
{
    String result = "";

    for (int i = 0; i < maxLength; ++i) 
    {
        char c = char(EEPROM.read(addr + i));
        if (c == 0 || c == 255) break;  
        result += c;
    }

    return result;
}

void WifiConfigManager::loadConfigurations(String &ssid, String &pass, String &blynk, String &bot, String &chat) 
{
    ssid = readStringFromEEPROM(ADDR_SSID, 32);
    pass = readStringFromEEPROM(ADDR_PASS, 64);
    blynk = readStringFromEEPROM(ADDR_BLYNK, 32);
    bot = readStringFromEEPROM(ADDR_TELE_BOT, 64);
    chat = readStringFromEEPROM(ADDR_TELE_CHAT, 32);
}