
#include "WiFi_Config.h"
#include "sensor.h"
#include "display.h"
#include "mqtt_client.h"

void setup()
{
  Serial.begin(9600);
  Wifi_init();
  AI_Start();
  MQTT_init();
  MQ135_Init();
  MQ7_Init();
  GP2Y_setup();
  DHT22_init();
  TFT_init();
}

void loop()
{
  if (isWifiConnected())
    server.handleClient();

  if (!firstSendDone || millis() - lastSend > SEND_INTERVAL)
  {
    DHT22_run();
    MQ135_run();
    MQ7_run();
    dust = Run_GP2Y();
    drawScreen();
    MQTT_publish();
    lastSend = millis();
    firstSendDone = true;
  }
  delay(100);
}
