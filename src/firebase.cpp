#include "firebase.h"
#include <HTTPClient.h>
#include "time.h"
#include "sensor.h" // để lấy temp, humi, co_ppm, nh3_ppm, dust

// ==== Firebase ====
const char *firebase_host =
    "https://nckh-clkk-default-rtdb.asia-southeast1.firebasedatabase.app/";

String firebase_path = "sensor_data.json";

// ==== Thời gian gửi dữ liệu ====
unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 600000; // 10 phút
bool firstSendDone = false;

// ==== Timestamp ====
String getTimestamp()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
        return "Unknown";

    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

void sendToFirebase()
{
    String timestamp = getTimestamp();
    String recordID = timestamp;
    recordID.replace(" ", "_");
    recordID.replace(":", "-");

    // ==== Tạo JSON ====
    String jsonData = "{";
    jsonData += "\"time\":\"" + timestamp + "\",";
    jsonData += "\"temp\":" + String(temp, 2) + ",";
    jsonData += "\"humi\":" + String(humi, 2) + ",";
    jsonData += "\"co\":" + String(co_ppm, 2) + ",";
    jsonData += "\"nh3\":" + String(nh3_ppm, 2) + ",";
    jsonData += "\"dust\":" + String(dust, 2);
    jsonData += "}";

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        String url = String(firebase_host) + "sensor_data/" + recordID + ".json";

        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        int code = http.PUT(jsonData);

        if (code > 0)
            Serial.println("📤 Firebase OK: " + String(code));
        else
            Serial.println("❌ Firebase Err: " + http.errorToString(code));

        Serial.println(jsonData);
        http.end();
    }
}
