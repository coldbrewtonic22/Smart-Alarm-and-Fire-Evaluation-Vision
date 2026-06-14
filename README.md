# 🚨 Smart Alert and Fire Evaluation Vision

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--CAM-lightgrey.svg)![Framework](https://img.shields.io/badge/framework-Arduino%20%7C%20FreeRTOS-orange.svg)![Cloud](https://img.shields.io/badge/cloud-AWS%20S3%20%7C%20Blynk%20%7C%20Telegram-yellow.svg)Hệ thống giám sát và cảnh báo an toàn thông minh ứng dụng công nghệ Internet of Things (IoT) và Xử lý tại biên (Edge Computing). Dự án được thiết kế theo kiến trúc phân tán (Distributed Architecture) nhằm tối ưu hóa hiệu năng xử lý đa nhiệm theo thời gian thực, đảm bảo an toàn tuyệt đối cho người dùng trước các rủi ro hỏa hoạn và rò rỉ khí gas.

**🎓 Project by:** D23 - IoT - Group 15

---

## 🌟 Key Features

Dự án không chỉ dừng lại ở mức độ lắp ráp phần cứng mà tập trung sâu vào Kỹ thuật Phần mềm Nhúng (Embedded Software Engineering) với các giải pháp kỹ thuật cao cấp:

- **🧠 Xử lý tín hiệu số (DSP) tại biên:** Ứng dụng kỹ thuật **Oversampling** (Lấy mẫu quá mức) kết hợp bộ lọc toán học **SimpleKalmanFilter** để triệt tiêu hoàn toàn nhiễu trắng (white noise) trên tín hiệu Analog của cảm biến MQ-2, ngăn chặn triệt để tình trạng báo động giả.
- **⚡ Kiến trúc Đa nhiệm Thời gian thực (FreeRTOS):** Khai thác tối đa sức mạnh lõi kép (Dual-core) của ESP32.
  - **Core 1:** Xử lý các tác vụ yêu cầu độ trễ cực thấp (Đọc cảm biến, Băm xung PWM, Keypad, Quản lý State Machine).
  - **Core 0:** Xử lý toàn bộ ngăn xếp mạng (Blynk, WebServer) để việc mất kết nối Internet không bao giờ làm gián đoạn hệ thống cứu hộ cục bộ.
- **🔗 Giao tiếp liên chip (Inter-MCU Communication):** Phân tách logic điều khiển (Master Node) và thị giác máy tính (Slave Node). Hai vi điều khiển giao tiếp với nhau qua UART tốc độ cao (115200 bps), truyền tải dữ liệu được mã hóa và đóng gói bằng chuẩn **JSON**.
- **☁️ Tích hợp Đa đám mây (Multi-Cloud Integration):**
  - **Blynk IoT:** Giám sát thông số đồ thị thời gian thực và rẽ nhánh điều khiển thủ công (Manual Override).
  - **AWS S3:** Mã hóa Base64 và đẩy hình ảnh hiện trường lên bộ lưu trữ đám mây của Amazon để lưu trữ dữ liệu an ninh.
  - **Telegram Bot API:** Đóng gói hình ảnh định dạng `multipart/form-data` gửi cảnh báo hình ảnh khẩn cấp trực tiếp về điện thoại người dùng.
- **⚙️ Cấu hình động (Dynamic Provisioning):** Tích hợp tính năng **Captive Portal**. Khi không có mạng, hệ thống tự phát WiFi Access Point và chuyển hướng người dùng đến giao diện Web nội bộ để thay đổi SSID, Password, API Token và lưu vĩnh viễn xuống EEPROM.
- **🛡️ Cơ chế bảo mật & Chống nhiễu:**
  - *Software Rate Limiter:* Thuật toán đếm trễ 300ms chống rung phím (debounce) phần mềm cho Keypad.
  - *Hysteresis Anti-Spike:* Bộ lọc trễ 3 giây (6 chu kỳ 500ms) yêu cầu tín hiệu gas phải duy trì vượt ngưỡng liên tục trước khi xác nhận báo động.
  - *Hardware Isolation:* Mạch nguồn 3 Adapter độc lập và Relay cách ly quang bảo vệ MCU khỏi dòng Flyback.

---

## 🏗️ System Architecture

Hệ thống được chia làm hai Node xử lý vật lý độc lập, liên kết qua UART:

### 1. Master (ESP32 WROOM-32)

Đóng vai trò là bộ não điều khiển trung tâm (Logic Controller).

- **Cảm biến (Inputs):** MQ-2 (Gas), Flame Sensor (IR Lửa), Keypad 4x4.
- **Chấp hành (Outputs):** Relay cách ly (Quạt hút, Bơm chữa cháy), Động cơ Servo (Chốt cửa), Buzzer & LED, Màn hình LCD I2C 20x4.
- **Nhiệm vụ:** Chạy Máy trạng thái (FSM) với 4 trạng thái (`SAFE`, `GAS_ONLY`, `FIRE_ONLY`, `EMERGENCY`). Quyết định logic cứu hộ và điều phối mạng lưới.

### 2. Slave (ESP32-CAM OV3660)

Đóng vai trò là hệ thống Thị giác máy tính (Computer Vision Node).

- **Nhiệm vụ:** Chạy kiến trúc Super Loop. Lắng nghe lệnh từ Master, chụp ảnh hiện trường, bật Flash và thực hiện các tác vụ HTTP/HTTPS nặng nề (đẩy ảnh lên AWS S3 và Telegram) để giảm tải Heap RAM cho Master Node.

---

## 📁 Directory Structure

Dự án được xây dựng trên **Visual Studio Code** với nền tảng **PlatformIO**, áp dụng triệt để nguyên lý Lập trình Hướng đối tượng (OOP):

```text
├── 📁 master-node
│   ├── 📁 include
│   │   ├── ⚡ ActuatorManager.h
│   │   ├── ⚡ Config.h
│   │   ├── 📄 README
│   │   ├── ⚡ SensorManager.h
│   │   ├── ⚡ UIManager.h
│   │   ├── ⚡ UartCommManager.h
│   │   └── ⚡ WifiConfigManager.h
│   ├── 📁 lib
│   │   └── 📄 README
│   ├── 📁 src
│   │   ├── ⚡ ActuatorManager.cpp
│   │   ├── ⚡ SensorManager.cpp
│   │   ├── ⚡ UIManager.cpp
│   │   ├── ⚡ UartCommManager.cpp
│   │   ├── ⚡ WifiConfigManager.cpp
│   │   └── ⚡ main.cpp
│   ├── 📁 test
│   │   └── 📄 README
│   ├── ⚙️ .gitignore
│   └── ⚙️ platformio.ini
├── 📁 slave-node
│   ├── 📁 include
│   │   ├── ⚡ CameraManager.h
│   │   ├── ⚡ CloudManager.h
│   │   ├── ⚡ Config.h
│   │   └── 📄 README
│   ├── 📁 lib
│   │   └── 📄 README
│   ├── 📁 src
│   │   ├── ⚡ CameraManager.cpp
│   │   ├── ⚡ CloudManager.cpp
│   │   └── ⚡ main.cpp
│   ├── 📁 test
│   │   └── 📄 README
│   ├── ⚙️ .gitignore
│   └── ⚙️ platformio.ini
├── ⚙️ .gitignore
└── 📝 README.md
```

# 🚀 Build & Run

## 1. Yêu cầu môi trường Visual Studio Code với Extension PlatformIO IDE.

Driver CP210x / CH340 cho việc nạp code qua cổng USB.

## 2. Các bước biên dịch Clone repository này về máy cục bộ.

Mở thư mục gốc của project bằng VSCode. PlatformIO sẽ tự động khởi tạo và tải các thư viện cần thiết (ArduinoJson, Blynk, ESP32Servo, v.v.).

Sử dụng môi trường env:esp32dev để nạp code cho bo mạch Master.

Sử dụng môi trường env:esp32cam để nạp code cho bo mạch Slave.

## 3. Cấu hình hệ thống lần đầu (First-time Setup) Cấp nguồn cho hệ thống. Ở lần khởi chạy đầu tiên (hoặc khi mất cấu hình), màn hình LCD sẽ báo No credentials! và cấp IP 192.168.4.1.

Sử dụng điện thoại kết nối vào mạng WiFi do hệ thống phát ra (Tên mặc định: ESP32_Config).

Trình duyệt sẽ tự động chuyển hướng (hoặc truy cập thủ công vào 192.168.4.1).

Nhập các thông số mạng thực tế và API Token (Blynk, Telegram). Nhấn Save. Hệ thống sẽ lưu vào EEPROM và tự động khởi động lại.

🔐 Thao tác sử dụng cục bộ (Local Operations) Báo động khẩn cấp: Nhấn phím C trên Keypad để kích hoạt cờ S.O.S, ép hệ thống vào trạng thái EMERGENCY.

Trích xuất hình ảnh: Nhấn phím B trên Keypad để yêu cầu Slave Node chụp ảnh tuần tra (Snapshot) ngay lập tức.

Tắt còi báo động: Khi có sự cố, nhập mã PIN 4 chữ số (Mặc định được khởi tạo khi EEPROM trống) sau đó nhấn # để tạm thời tắt còi hú (Silenced Mode).

Đổi mã PIN: Nhấn phím \*, chọn D: Change PIN từ Menu. Nhập mã PIN cũ, sau đó nhập mã PIN mới gồm 4 chữ số để ghi đè xuống EEPROM.

Bản quyền tài liệu và mã nguồn thuộc về nhóm sinh viên nghiên cứu.