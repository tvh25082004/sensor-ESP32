#include "WiFi_Config.h"

// ==== Real time
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// ==== WiFi mặc định ====
const char *ssid = "Open WorkShop 2.4GH";
const char *password = "0000*0000";

// Access Point
const char *apSSID = "ESP32_Config";
const char *apPASS = "";

// Global server + prefs
WebServer server(80);
Preferences prefs;

// Biến trạng thái WiFi
bool wifiConnected = false;

// ==== AI Alert Server ====
String receivedAlerts[10];
int receivedAlertCount = 0;

// ==== HTML cấu hình WiFi ====
String htmlPage()
{
  String page =
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>ESP32 WiFi Config</title></head><body>"
      "<h2>Thiết lập WiFi</h2>"
      "<form action='/save' method='post'>"
      "SSID:<br><input name='ssid' required><br><br>"
      "Mật khẩu:<br><input name='pass' type='password'><br><br>"
      "<input type='submit' value='Lưu & Kết nối'>"
      "</form></body></html>";
  return page;
}

// ==== Lưu WiFi mới ====
void handleSave()
{
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  server.send(200, "text/html", "<h3>Đã lưu! ESP32 đang reboot...</h3>");
  delay(1500);
  ESP.restart();
}

// ==== WiFi Init - Không chặn chương trình ====
void Wifi_init()
{
  Serial.println("🚀 Khởi động WiFi...");

  // 1. Thử WiFi mặc định
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int retry = 0;

  while (WiFi.status() != WL_CONNECTED && retry < 10)
  {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    Serial.println("\n✅ Connected to default WiFi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("🕐 Đang đồng bộ thời gian NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    return;
  }

  // 2. Thử WiFi đã lưu
  prefs.begin("wifi", true);
  String saved_ssid = prefs.getString("ssid", "");
  String saved_pass = prefs.getString("pass", "");
  prefs.end();

  if (saved_ssid != "")
  {
    Serial.println("\n🔄 Thử kết nối WiFi đã lưu...");
    WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
    retry = 0;

    while (WiFi.status() != WL_CONNECTED && retry < 10)
    {
      delay(500);
      Serial.print(".");
      retry++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      wifiConnected = true;
      Serial.println("\n✅ Connected to saved WiFi!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("🕐 Đang đồng bộ thời gian NTP...");
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      return;
    }
  }

  // 3. Không kết nối được - Tiếp tục chạy offline
  wifiConnected = false;
  Serial.println("\n⚠️ Không kết nối được WiFi - Chạy ở chế độ OFFLINE");
  Serial.println("📡 Hệ thống vẫn hoạt động bình thường (không có tính năng online)");

  // Không mở AP, chỉ chạy cảm biến và hiển thị
  WiFi.mode(WIFI_OFF);
}

void AI_Start()
{
  // Chỉ khởi động server nếu WiFi đã kết nối
  if (!wifiConnected)
  {
    Serial.println("⚠️ AI Alert Server không khả dụng (WiFi offline)");
    return;
  }

  server.on("/alert", HTTP_POST, []()
            {
    if (!server.hasArg("plain"))
    {
      server.send(400, "text/plain", "Body missing");
      return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")))
    {
      server.send(400, "text/plain", "JSON Error");
      return;
    }

    JsonArray anomalies = doc["anomalies"];

    for (JsonVariant v : anomalies)
    {
      String alert = v.as<const char *>();
      Serial.println("⚠️ Alert: " + alert);

      if (receivedAlertCount < 10)
        receivedAlerts[receivedAlertCount++] = alert;
      else
      {
        for (int i = 1; i < 10; i++)
          receivedAlerts[i - 1] = receivedAlerts[i];
        receivedAlerts[9] = alert;
      }
    }

    server.send(200, "text/plain", "OK"); });

  server.begin();
  Serial.println("✅ AI Alert Server đã khởi động!");
}

// Hàm kiểm tra trạng thái WiFi
bool isWifiConnected()
{
  return wifiConnected && (WiFi.status() == WL_CONNECTED);
}