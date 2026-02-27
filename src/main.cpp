
#include "WiFi_Config.h"
#include "sensor.h"
#include "display.h"
#include "firebase.h"

// ==== Setup ====
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

// ==== Loop ====
void loop()
{
  server.handleClient(); 

  // Gửi dữ liệu định kỳ
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
  delay(100); // delay ngắn để tránh chiếm CPU quá nhiều
}
