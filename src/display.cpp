#include "display.h"

// ==== TFT ====
TFT_eSPI tft = TFT_eSPI();

#define TEMP_HIGH 35.0
#define HUMI_LOW 30.0
#define HUMI_HIGH 90.0
#define CO_HIGH 12.5
#define NH3_HIGH 20.0
#define DUST_HIGH 35.0

// Tính cảnh báo
int collectWarnings(String warnList[], int maxItems)
{
    int count = 0;
    if (temp > TEMP_HIGH && count < maxItems)
        warnList[count++] = "HIGH TEMP!";
    if ((humi < HUMI_LOW || humi > HUMI_HIGH) && count < maxItems)
        warnList[count++] = "ABN HUMI!";
    if (co_ppm > CO_HIGH && count < maxItems)
        warnList[count++] = "HIGH CO!";
    if (nh3_ppm > NH3_HIGH && count < maxItems)
        warnList[count++] = "HIGH NH3!";
    return count;
}

void TFT_init()
{
    tft.init();
    tft.setRotation(1);
}

void drawScreen()
{
    int w = tft.width();
    int h = tft.height();
    int boxW = w / 2;
    int boxH = h / 2;

    String warns[4];
    int warnCount = collectWarnings(warns, 4);

    bool warning = (warnCount > 0) || (receivedAlertCount > 0);
    uint16_t bg = warning ? TFT_RED : TFT_BLUE;

    tft.fillScreen(bg);
    tft.drawLine(boxW, 0, boxW, h, TFT_WHITE);
    tft.drawLine(0, boxH, w, boxH, TFT_WHITE);
    tft.drawRect(0, 0, w, h, TFT_WHITE);

    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextSize(2);

    char buf[32];

    // TEMP
    sprintf(buf, "%.1f C", temp);
    tft.setCursor(20, 30);
    tft.print("TEMP ");
    tft.print(buf);

    // HUMI
    sprintf(buf, "%.1f %%", humi);
    tft.setCursor(20, 80);
    tft.print("HUMI ");
    tft.print(buf);

    // Alerts
    tft.setTextColor(TFT_YELLOW, bg);
    int x = boxW + 10;
    int y = 20;

    for (int i = 0; i < warnCount; i++)
    {
        tft.setCursor(x, y + i * 25);
        tft.print(warns[i]);
    }

    for (int i = 0; i < receivedAlertCount; i++)
    {
        tft.setCursor(x, y + (warnCount + i) * 25);
        tft.print(receivedAlerts[i]);
    }

    if (!warning)
    {
        tft.setTextColor(TFT_WHITE, bg);
        tft.setCursor(boxW + 30, boxH / 2 - 10);
        tft.print("OK");
    }

    // CO
    sprintf(buf, "%.2f ppm", co_ppm);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setCursor(20, boxH + 20);
    tft.print("CO ");
    tft.print(buf);

    // NH3
    sprintf(buf, "%.2f ppm", nh3_ppm);
    tft.setCursor(boxW + 15, boxH + 40);
    tft.print("NH3 ");
    tft.print(buf);

    // Dust
    sprintf(buf, "%.1f ug/m3", dust);
    tft.setCursor(20, boxH + 60);
    tft.print("Dust ");
    tft.setCursor(20, boxH + 90);
    tft.print(buf);
}
