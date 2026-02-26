## Hệ thống giám sát chất lượng không khí với ESP32, Firebase và AI

Project này triển khai một hệ thống đo và giám sát chất lượng không khí theo thời gian thực dùng ESP32, lưu dữ liệu lên Firebase Realtime Database và một service Python bên ngoài để phân tích bất thường + dự báo, sau đó đẩy cảnh báo ngược lại về ESP32 hiển thị trên màn TFT.

---

## 1. Kiến trúc tổng thể

- **ESP32 (PlatformIO / Arduino)**:
  - Đọc cảm biến: DHT22 (nhiệt độ, độ ẩm), MQ135 (NH3), MQ7 (CO), GP2Y (bụi).
  - Hiển thị real-time lên TFT qua `TFT_eSPI`.
  - Gửi dữ liệu định kỳ lên Firebase bằng REST API.
  - Nhận cảnh báo bất thường từ service AI (Python) qua HTTP `/alert`, hiển thị cảnh báo trên TFT.
- **Firebase Realtime Database**:
  - Lưu log dữ liệu cảm biến theo timestamp (sensor_data).
- **Python AI Service (`backup_air_data.py`)**:
  - Chạy trên PC/server, định kỳ đọc dữ liệu từ Firebase.
  - Backup dữ liệu ra file `backup.json`.
  - Dùng **ECOD (PyOD)** để phát hiện bất thường đa biến và ngưỡng cố định để cảnh báo.
  - Dùng **Prophet** để dự báo 6 giờ tới cho từng biến (temp, humi, co, nh3, dust) và xác định xu hướng.
  - Gửi danh sách cảnh báo về ESP32 qua HTTP POST.

### 1.1. Sơ đồ kiến trúc (Mermaid)

```mermaid
flowchart LR
    subgraph Edge_Device[ESP32 + TFT]
        SENSORS[DHT22<br/>MQ135<br/>MQ7<br/>GP2Y]
        MCU[ESP32 Firmware<br/>(PlatformIO / Arduino)]
        TFT[TFT_eSPI Display]
    end

    subgraph Cloud[Firebase]
        DB[(Realtime Database<br/>sensor_data)]
    end

    subgraph AI_Service[Python AI Service]
        PY[backup_air_data.py<br/>ECOD + Prophet]
        BK[backup.json]
    end

    SENSORS --> MCU
    MCU --> TFT
    MCU -- PUT JSON --> DB
    DB -- HTTP GET --> PY
    PY --> BK
    PY -- HTTP POST /alert --> MCU

    classDef edge fill:#003f5c,stroke:#fff,color:#fff;
    classDef cloud fill:#58508d,stroke:#fff,color:#fff;
    classDef ai fill:#bc5090,stroke:#fff,color:#fff;

    class Edge_Device edge;
    class Cloud cloud;
    class AI_Service ai;
```

---

## 2. Luồng chạy chi tiết firmware ESP32

Các file chính trong `src/`:
- `main.cpp`
- `Wifi_Config.h / Wifi_Config.cpp`
- `sensor.h / sensor.cpp`
- `display.h / display.cpp`
- `firebase.h / firebase.cpp`
- `backup_air_data.py` (service Python bên ngoài ESP32)

### 2.1. `setup()` và `loop()` – file `main.cpp`

```cpp
void setup()
{
  Serial.begin(9600);
  Wifi_init();
  AI_Start();
  MQ135_Init();
  MQ7_Init();
  GP2Y_setup();
  DHT22_init();
  TFT_init();
}

void loop()
{
  server.handleClient(); 

  if (!firstSendDone || millis() - lastSend > SEND_INTERVAL)
  {
    DHT22_run();
    MQ135_run();
    MQ7_run();
    dust = Run_GP2Y();
    drawScreen();
    sendToFirebase();
    lastSend = millis();
    firstSendDone = true;
  }
  delay(100);
}
```

- **setup()**:
  - Khởi tạo UART debug.
  - Gọi `Wifi_init()` để kết nối WiFi (ưu tiên WiFi mặc định, sau đó WiFi đã lưu, nếu không được thì chạy OFFLINE).
  - Gọi `AI_Start()` để khởi động HTTP server nhận cảnh báo AI (chỉ hoạt động khi WiFi đã kết nối).
  - Khởi tạo các cảm biến: `MQ135_Init()`, `MQ7_Init()`, `GP2Y_setup()`, `DHT22_init()`.
  - Khởi tạo màn hình TFT: `TFT_init()`.
- **loop()**:
  - `server.handleClient()` xử lý HTTP request (đặc biệt là `/alert` từ Python AI).
  - Theo chu kỳ `SEND_INTERVAL = 600000 ms` (10 phút) hoặc ngay lần đầu:
    - Đọc dữ liệu cảm biến: `DHT22_run()`, `MQ135_run()`, `MQ7_run()`, `Run_GP2Y()`.
    - Vẽ lại giao diện TFT: `drawScreen()`.
    - Gửi bản ghi mới lên Firebase: `sendToFirebase()`.

### 2.2. Luồng WiFi + AI Alert – `Wifi_Config.cpp / Wifi_Config.h`

- **Cấu hình thời gian**:
  - Dùng NTP: `pool.ntp.org`, GMT+7.
- **WiFi mặc định và Access Point**:
  - `ssid = "Open WorkShop 2.4GH"`
  - `password = "0000*0000"`
  - Có khai báo AP `ESP32_Config` nhưng trong mã hiện tại **không bật AP**, chỉ dùng STA.

#### 2.2.1. Hàm `Wifi_init()`

- Thứ tự thử kết nối:
  1. **WiFi mặc định** (`ssid`, `password`), retry tối đa 10 lần.
  2. Nếu thất bại: đọc `ssid`, `pass` từ `Preferences` namespace `"wifi"` và thử kết nối lại 10 lần.
  3. Nếu vẫn thất bại:
     - Đặt `wifiConnected = false`.
     - Log: chạy ở chế độ OFFLINE.
     - Tắt WiFi: `WiFi.mode(WIFI_OFF)`.
- Khi kết nối thành công:
  - `wifiConnected = true`.
  - In ra IP.
  - Gọi `configTime(...)` để đồng bộ NTP (phục vụ `getTimestamp()` trong Firebase).

#### 2.2.2. API nhận cảnh báo AI – `AI_Start()`

- Chỉ chạy nếu `wifiConnected == true`, ngược lại log: *AI Alert Server không khả dụng (WiFi offline)*.
- Đăng ký route:
  - `server.on("/alert", HTTP_POST, handler...)`
  - Body JSON (truyền từ Python) được parse bằng `ArduinoJson`.
  - Lấy `JsonArray anomalies = doc["anomalies"];`
  - Mỗi phần tử trong `anomalies` được đưa vào mảng vòng `receivedAlerts[10]` (tối đa 10 dòng gần nhất).
  - Trả HTTP `200 OK` nếu parse thành công.
- `server.begin()` và in log "AI Alert Server đã khởi động!".

**Biến trạng thái quan trọng**:
- `bool wifiConnected` – lưu trạng thái WiFi.
- `String receivedAlerts[10]`, `int receivedAlertCount` – buffer cảnh báo AI hiển thị trên TFT.
- Hàm tiện ích: `bool isWifiConnected()` kiểm tra lại trạng thái runtime (`wifiConnected` && `WiFi.status()`).

### 2.3. Đo cảm biến – `sensor.cpp / sensor.h`

Các chân kết nối (theo mã):
- **DHT22**: `DHTPIN 16`
- **MQ135 (NH3)**: `MQ135_PIN 34` (ADC, attenuation 11 dB)
- **MQ7 (CO)**: `MQ7_PIN 32`
- **GP2Y (bụi)**:
  - ADC: `GP2Y_ADC 33`
  - LED: `GP2Y_LED 13`

Các biến toàn cục (dùng chung giữa sensor, display, firebase):
- `float temp, humi, co_ppm, nh3_ppm, dust;`

#### 2.3.1. DHT22
- `DHT22_init()`:
  - `dht.begin();`
- `DHT22_run()`:
  - `humi = dht.readHumidity();`
  - `temp = dht.readTemperature();`

#### 2.3.2. MQ135 – ước lượng NH3

- Đọc ADC nhiều mẫu, lấy trung bình:
  - `analogRead(MQ135_PIN)` lặp `samples = 10` lần.
  - Chuyển sang điện áp: \( V_{out} = \frac{ADC}{4095} \times 3.3V \).
- Tính điện trở cảm biến:
  - \( R_s = R_L \times \frac{V_{CC} - V_{out}}{V_{out}} \), với `RL = 10kΩ`, `VCC = 5V`.
- Chuyển Rs/R0 sang ppm theo đường cong log-log (xấp xỉ từ datasheet):
  - Dùng hệ số:
    - `M_NH3 = -0.48`
    - `B_NH3 = 0.35`
  - Công thức:
    - \( \log_{10}(\text{ppm}) = \frac{\log_{10}(Rs/R0) - B}{M} \)
    - \( \text{ppm} = 10^{\left( \frac{\log_{10}(Rs/R0) - B}{M} \right)} \)
- Kết quả lưu vào `nh3_ppm`.

#### 2.3.3. MQ7 – ước lượng CO

- Điện trở Rs từ ADC:
  - \( V = \frac{ADC}{4095} \times 3.3V \)
  - \( R_s = (3.3 - V) \times \frac{R_L}{V} \), với `RL_7 = 10kΩ`.
- Chuyển Rs/R0 sang ppm:
  - Hệ số:
    - `m = -0.77`
    - `b = 1.699`
  - \( \log_{10}(\text{ppm}) = \frac{\log_{10}(Rs/R0) - b}{m} \)
- Kết quả lưu vào `co_ppm`.

#### 2.3.4. GP2Y – bụi

- Trong vòng lặp 100 lần:
  - Điều khiển IR LED:
    - `digitalWrite(GP2Y_LED, LOW);` – bật LED.
    - Delay micro giây để ổn định, đọc ADC `voMeasured = analogRead(GP2Y_ADC);`
    - `digitalWrite(GP2Y_LED, HIGH);` – tắt LED, nghỉ.
  - Chuyển điện áp (hiện đang dùng hệ số 5.0/1024.0 – tương đương thang đo ban đầu của GP2Y):
    - `calcVoltage = voMeasured * (5.0 / 1024.0);`
  - Chuyển sang mật độ bụi:
    - `dustDensity = 0.17 * calcVoltage - 0.1;` (mô hình tuyến tính từ datasheet).
  - Cộng dồn `sumDust`.
- Sau vòng lặp:
  - Bình quân, đổi ra mg/m³ rồi µg/m³, lấy giá trị tuyệt đối cho `dust`.

### 2.4. Hiển thị TFT – `display.cpp / display.h`

#### 2.4.1. Khởi tạo

- `TFT_eSPI tft;`
- `TFT_init()`:
  - `tft.init();`
  - `tft.setRotation(1);` (xoay ngang).

#### 2.4.2. Ngưỡng cảnh báo hiển thị

- `TEMP_HIGH = 35.0 °C`
- `HUMI_LOW = 30.0 %`
- `HUMI_HIGH = 90.0 %`
- `CO_HIGH = 12.5 ppm`
- `NH3_HIGH = 20.0 ppm`
- `DUST_HIGH = 35.0 µg/m³` (dùng ngưỡng trong Python).

- Hàm `collectWarnings(...)`:
  - Dựa trên các ngưỡng trên, sinh ra danh sách chuỗi cảnh báo:
    - `"HIGH TEMP!"`, `"ABN HUMI!"`, `"HIGH CO!"`, `"HIGH NH3!"`.

#### 2.4.3. Vẽ màn hình – `drawScreen()`

- Tính kích thước, chia màn hình thành 4 ô (2x2).
- Tập hợp cảnh báo:
  - `warnList` từ giá trị cảm biến.
  - `receivedAlerts` từ AI.
- Nền:
  - Nếu có bất cứ cảnh báo nào (`warnCount > 0` hoặc `receivedAlertCount > 0`) → nền **đỏ** (`TFT_RED`).
  - Ngược lại → nền **xanh** (`TFT_BLUE`) và in chữ "OK".
- Nội dung:
  - Góc trên trái: `TEMP`, `HUMI`.
  - Góc dưới: `CO`, `NH3`, `Dust`.
  - Nửa bên phải: danh sách cảnh báo:
    - Cảnh báo local (`warns[i]`).
    - Cảnh báo AI (`receivedAlerts[i]`).

### 2.5. Gửi dữ liệu lên Firebase – `firebase.cpp / firebase.h`

- **Biến thời gian**:
  - `unsigned long lastSend;`
  - `const unsigned long SEND_INTERVAL = 600000;` (10 phút).
  - `bool firstSendDone;`
- **Cấu hình Firebase**:
  - `firebase_host = "https://nckh-clkk-default-rtdb.asia-southeast1.firebasedatabase.app/";`
  - Đường dẫn chính: `sensor_data/<recordID>.json`.
- **Timestamp** – `getTimestamp()`:
  - Dùng `getLocalTime()` (thời gian đã sync NTP).
  - Format: `YYYY-MM-DD HH:MM:SS`.
- **`sendToFirebase()`**:
  - Sinh `recordID` từ `timestamp` (thay `space` bằng `_`, `:` bằng `-`).
  - Build JSON:
    - `"time"` – chuỗi timestamp.
    - `"temp"`, `"humi"`, `"co"`, `"nh3"`, `"dust"` – số thực 2 chữ số sau dấu phẩy.
  - Khi `WiFi.status() == WL_CONNECTED`:
    - Dùng `HTTPClient`, gửi `PUT` lên URL:
      - `firebase_host + "sensor_data/" + recordID + ".json"`.
    - In mã phản hồi (`code`) và JSON ra Serial.

---

## 3. Luồng chạy Python AI Service – `backup_air_data.py`

Script này **không chạy trên ESP32**, mà chạy trên PC/server, dùng `requests`, `pandas`, `prophet`, `pyod (ECOD)`, `numpy`.

### 3.1. Cấu hình chính

- `FIREBASE_URL = "<host>/sensor_data.json"`
- `ESP32_HTTP = "http://<ESP_IP>/alert"` – cần sửa IP thực tế của ESP32.
- `BACKUP_FILE = "backup.json"` – file backup local.
- `POLL_INTERVAL = 600` (10 phút).
- Ngưỡng cố định `THRESHOLDS` giống với ngưỡng trong ESP:
  - `temp`, `humi_low`, `humi_high`, `co`, `nh3`, `dust`.

### 3.2. Luồng chính `main()`

Pseudo-flow:

```mermaid
flowchart TD
    A[Start main()] --> B[get_firebase_data()]
    B --> C[backup_firebase(df)]
    C --> D{df empty?}
    D -- Yes --> E[Print 'No data yet' & sleep 10 min] --> B
    D -- No --> F[detect_anomaly(df)]
    F --> G{anomalies?}
    G -- Yes --> H[print anomalies] --> I[send_alert(anomalies, latest_row)]
    G -- No --> J[print 'Data normal']
    I --> K[Forecast & trend per metric]
    J --> K
    K --> L[sleep 10 min] --> B
```

### 3.3. Các bước chính

- **Lấy dữ liệu từ Firebase** – `get_firebase_data()`:
  - GET `FIREBASE_URL`.
  - Bỏ qua key `_last_backup` nếu có.
  - Parse `time` về `datetime`, build `DataFrame` có cột `timestamp`, `temp`, `humi`, `co`, `nh3`, `dust`.
  - Sort theo thời gian tăng dần.
- **Backup** – `backup_firebase(df)`:
  - Nếu `df.empty` → backup `{}` + `_last_backup`.
  - Nếu có dữ liệu:
    - Chuyển `timestamp` sang string `"%Y-%m-%d %H:%M:%S"`.
    - Set `timestamp` làm index, export sang dict rồi ghi `backup.json`.
- **Phát hiện bất thường** – `detect_anomaly(df)`:
  - Yêu cầu tối thiểu 10 record.
  - Dùng các cột `["temp", "humi", "co", "nh3", "dust"]` làm feature.
  - Train `ECOD()` trên toàn bộ dữ liệu.
  - `is_anomaly = clf.labels_[-1]` – xem bản ghi mới nhất có bị đánh dấu bất thường đa biến không.
  - Áp thêm ngưỡng cố định (giống ESP) để thêm cảnh báo cụ thể từng biến.
  - Trả về:
    - `anomalies` – danh sách chuỗi cảnh báo.
    - `latest` – bản ghi mới nhất.
- **Dự báo 1–6 giờ + xu hướng** – `forecast_with_trend(df, col)`:
  - Chuẩn bị dữ liệu Prophet:
    - Cột `ds` = `timestamp`.
    - Cột `y` = log-transform `np.log(y + 1)` để ổn định mô hình.
  - Fit Prophet với `daily_seasonality=True`.
  - Tạo future 6h (`periods=6, freq='h'`).
  - Dự báo → `yhat`, convert ngược: `exp(yhat) - 1` và lấy giá trị dương.
  - Lấy 6 điểm cuối cùng để in forecast.
  - Xác định xu hướng:
    - `↑ increasing` nếu `yhat cuối > yhat đầu`.
    - `↓ decreasing` nếu `yhat cuối < yhat đầu`.
    - `→ stable` nếu gần như bằng nhau.
- **Gửi cảnh báo về ESP32** – `send_alert(anomalies, latest)`:
  - Payload dạng:
    ```json
    {
      "type": "anomaly",
      "time": "...",
      "anomalies": ["...", "..."],
      "row": {
        "temp": ...,
        "humi": ...,
        "co": ...,
        "nh3": ...,
        "dust": ...
      }
    }
    ```
  - Gửi POST `ESP32_HTTP` với `json=payload`.
  - ESP32 nhận trong `AI_Start()` và push vào `receivedAlerts[]` để hiển thị.

---

## 4. Hướng dẫn build & run

### 4.1. ESP32 firmware (PlatformIO)

- **Yêu cầu**:
  - PlatformIO (VSCode / CLI).
  - Board: `nodemcu-32s` (ESP32).
- **Cấu hình chính** – `platformio.ini`:
  - `platform = espressif32`
  - `board = nodemcu-32s`
  - `framework = arduino`
  - Thư viện:
    - `DHT sensor library`
    - `TFT_eSPI`
    - `PubSubClient`
    - `ArduinoJson`
- **Build & Upload**:
  - Mở project trong PlatformIO.
  - Chọn environment `env:nodemcu-32s`.
  - `Build` → `Upload` firmware lên ESP32.
- **Cần chỉnh**:
  - Thông số WiFi mặc định trong `Wifi_Config.cpp` nếu bạn không dùng `Open WorkShop 2.4GH`.
  - Hoặc cấu hình WiFi đã lưu bằng `Preferences` (phần này đang lưu/đọc key `"wifi"` – `ssid`, `pass`).

### 4.2. Python AI service

- Tạo virtualenv (hoặc dùng `airenv` đang có).
- Cài đặt package (tối thiểu):
  - `requests`, `pandas`, `numpy`, `prophet`, `pyod`.
- Chỉnh:
  - `FIREBASE_URL` trùng với Firebase của bạn.
  - `ESP32_HTTP` = `http://<IP thực tế của ESP32>/alert`.
- Chạy:
  ```bash
  python src/backup_air_data.py
  ```
  Script sẽ:
  - Vừa dự đoán, vừa phát hiện bất thường.
  - Ghi log ra console.
  - Gửi HTTP POST tới ESP32 khi phát hiện bất thường.

---

## 5. Tóm tắt luồng dữ liệu end-to-end

1. ESP32 khởi động:
   - Kết nối WiFi (nếu được) và sync NTP.
   - Khởi tạo HTTP server `/alert`, cảm biến, TFT.
2. Mỗi 10 phút:
   - Đọc DHT22, MQ135, MQ7, GP2Y.
   - Cập nhật màn hình TFT (màu nền xanh/đỏ + cảnh báo local/AI).
   - Gửi mẫu mới lên Firebase (một JSON / một timestamp).
3. Python service:
   - Kéo toàn bộ `sensor_data` từ Firebase.
   - Backup ra `backup.json` (kèm `_last_backup` và timestamp).
   - Dùng **ngưỡng cố định + ECOD** để phát hiện bất thường.
   - Dùng **Prophet** để dự báo 6h tới và đánh giá xu hướng.
   - Nếu bất thường:
     - Gửi danh sách `anomalies` về ESP32 qua `/alert`.
4. ESP32 nhận cảnh báo:
   - Parse JSON.
   - Lưu vào `receivedAlerts[10]`.
   - Lần vẽ màn hình tiếp theo, hiển thị cảnh báo AI ở nửa phải, chuyển nền sang đỏ nếu có cảnh báo.

Như vậy toàn bộ pipeline từ cảm biến → ESP32 → Firebase → Python AI → ESP32 được đóng vòng khép kín, cho phép vừa giám sát real-time tại chỗ, vừa phân tích sâu và cảnh báo thông minh dựa trên lịch sử dữ liệu.
