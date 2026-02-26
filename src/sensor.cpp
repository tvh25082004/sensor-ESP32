#include "sensor.h"

// ==== DHT22 ====
#define DHTPIN 16
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
float temp = 0, humi = 0, co_ppm = 0, nh3_ppm = 0, dust = 0;

// ==== MQ135 ====  đo NH3 phải nhỏ hơn 0.1
const int MQ135_PIN = 34;  // Chân ADC đọc MQ-135
const float RL = 10000.0;  // Điện trở tải (10kΩ)
const float VCC = 5.0;     // Điện áp nuôi cảm biến
const float ADC_REF = 3.3; // Điện áp tham chiếu ADC ESP32
const int ADC_MAX = 4095;  // 12-bit ADC
float R0 = 10000.0;        // Giá trị R0 (hiệu chuẩn trong không khí sạch)

// ==== Đường cong theo datasheet (log-log) ====
// log10(ppm) = (log10(Rs/R0) - b) / m
// hoặc ppm = 10^((log10(Rs/R0) - b) / m)
const float M_NH3 = -0.48; // Hệ số slope NH3 (xấp xỉ)
const float B_NH3 = 0.35;  // intercept NH3

int samples = 10;

// ==== MQ7 ====
#define MQ7_PIN 32
#define RL_7 10000
#define R0_7 10000

// ==== GP2Y ====
#define GP2Y_ADC 33
#define GP2Y_LED 13
int samplingTime = 280;
int deltaTime = 40;
int sleepTime = 9680;

float voMeasured = 0;
float calcVoltage = 0;

// ===========================
// ===== IMPLEMENTATION ======
// ===========================

// ==== DHT22 ====
void DHT22_init()
{
    dht.begin();
}

void DHT22_run()
{
    humi = dht.readHumidity();
    temp = dht.readTemperature();
}

// ==== MQ135 ====
float readVoltage()
{
    uint32_t sum = 0;
    for (int i = 0; i < samples; i++)
    {
        sum += analogRead(MQ135_PIN);
        delay(10);
    }
    float adc = (float)sum / samples;
    return (adc / ADC_MAX) * ADC_REF;
}

float calcRs(float vout)
{
    if (vout <= 0.0001)
        return 1e6;
    return RL * (VCC - vout) / vout;
}

float RsRo_to_ppm(float rs_ro, float m, float b)
{
    if (rs_ro <= 0)
        return -1;
    float log_ppm = (log10(rs_ro) - b) / m;
    return pow(10, log_ppm);
}

void MQ135_Init()
{
    analogSetPinAttenuation(MQ135_PIN, ADC_11db);
    pinMode(MQ135_PIN, INPUT);
}

void MQ135_run()
{
    float vout = readVoltage();
    float Rs = calcRs(vout);
    float ratio = Rs / R0;
    nh3_ppm = RsRo_to_ppm(ratio, M_NH3, B_NH3);
}

// ==== MQ7 ====
float getMQ7Resistance(int adcValue)
{
    float sensorVoltage = ((float)adcValue / 4095.0) * 3.3;
    float rs = (3.3 - sensorVoltage) * RL_7 / sensorVoltage;
    return rs;
}

float getCOppm(float rs)
{
    float ratio = rs / R0_7;
    float m = -0.77;
    float b = 1.699;
    float log_ppm = (log10(ratio) - b) / m;
    return pow(10, log_ppm);
}

void MQ7_Init()
{
    analogSetPinAttenuation(MQ7_PIN, ADC_11db);
    pinMode(MQ7_PIN, INPUT);
}

void MQ7_run()
{
    int adc7 = analogRead(MQ7_PIN);
    float Rs7 = getMQ7Resistance(adc7);
    co_ppm = getCOppm(Rs7);
}

// ==== GP2Y ====
void GP2Y_setup()
{
    pinMode(GP2Y_LED, OUTPUT);
}

float Run_GP2Y()
{
    float sumDust = 0;

    for (int i = 0; i < 100; i++)
    {
        digitalWrite(GP2Y_LED, LOW);
        delayMicroseconds(samplingTime);
        voMeasured = analogRead(GP2Y_ADC);
        delayMicroseconds(deltaTime);
        digitalWrite(GP2Y_LED, HIGH);
        delayMicroseconds(sleepTime);

        calcVoltage = voMeasured * (5.0 / 1024.0);
        float dustDensity = 0.17 * calcVoltage - 0.1;

        sumDust += dustDensity;
        delay(100);
    }

    sumDust /= 10.0;
    float avgDust_mg = sumDust / 100.0;
    float avgDust_ug = avgDust_mg * 1000.0;

    dust = abs(avgDust_ug);
    return dust;
}
