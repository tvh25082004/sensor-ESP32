#include "mqtt_client.h"
#include <WiFi.h>
#include "time.h"
#include "sensor.h"

static WiFiClient espClient;
static PubSubClient mqtt(espClient);

static const char *MQTT_BROKER = "172.11.85.208";
static const int MQTT_PORT = 1883;
static const char *MQTT_TOPIC = "sensor/esp32/data";
static const char *MQTT_CLIENT_ID = "esp32_sensor";

unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL = 30000;
bool firstSendDone = false;

String getTimestamp()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
        return "Unknown";
    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(buffer);
}

void MQTT_init()
{
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    Serial.print("MQTT broker: ");
    Serial.println(MQTT_BROKER);
}

void MQTT_reconnect()
{
    if (mqtt.connected())
        return;

    Serial.print("MQTT connecting...");
    if (mqtt.connect(MQTT_CLIENT_ID))
    {
        Serial.println(" OK");
    }
    else
    {
        Serial.print(" FAIL rc=");
        Serial.println(mqtt.state());
    }
}

void MQTT_publish()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    MQTT_reconnect();
    if (!mqtt.connected())
        return;

    String ts = getTimestamp();

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"device\":\"esp32\",\"time\":\"%s\","
             "\"temp\":%.2f,\"humi\":%.2f,"
             "\"co\":%.2f,\"nh3\":%.2f,\"dust\":%.2f}",
             ts.c_str(), temp, humi, co_ppm, nh3_ppm, dust);

    if (mqtt.publish(MQTT_TOPIC, payload))
        Serial.println("MQTT OK: " + String(payload));
    else
        Serial.println("MQTT publish FAIL");
}
