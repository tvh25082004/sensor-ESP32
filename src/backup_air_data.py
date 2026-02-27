import json
import os
import time
from datetime import datetime

import numpy as np
import pandas as pd
import requests
from influxdb_client import InfluxDBClient
from prophet import Prophet
from pyod.models.ecod import ECOD

# ==== Cấu hình InfluxDB (lấy từ biến môi trường) ====
INFLUX_URL   = os.getenv("INFLUX_URL",   "http://localhost:8086")
INFLUX_TOKEN = os.getenv("INFLUX_TOKEN", "my-super-secret-token")
INFLUX_ORG   = os.getenv("INFLUX_ORG",  "iot")
INFLUX_BUCKET= os.getenv("INFLUX_BUCKET","sensor_data")

# Cấu hình ESP32 HTTP để gửi alert
ESP32_HTTP    = os.getenv("ESP32_HTTP",   "http://192.168.1.17/alert")
BACKUP_FILE   = os.getenv("BACKUP_FILE",  "backup.json")
POLL_INTERVAL = int(os.getenv("POLL_INTERVAL", "600"))  # giây, mặc định 10 phút

# ==== Ngưỡng cảnh báo ====
THRESHOLDS = {
    "temp":     35.0,
    "humi_low": 30.0,
    "humi_high":90.0,
    "co":       12.5,
    "nh3":      20.0,
    "dust":     35.0   # µg/m3
}

# ==== Lấy dữ liệu từ InfluxDB ====
def get_influx_data(hours: int = 24) -> pd.DataFrame:
    """Query dữ liệu cảm biến từ InfluxDB trong khoảng `hours` giờ gần nhất."""
    client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
    query_api = client.query_api()

    flux = f'''
from(bucket: "{INFLUX_BUCKET}")
  |> range(start: -{hours}h)
  |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
  |> filter(fn: (r) => r["_field"] == "temp" or r["_field"] == "humi"
        or r["_field"] == "co" or r["_field"] == "nh3" or r["_field"] == "dust")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
  |> sort(columns: ["_time"])
'''
    try:
        tables = query_api.query_data_frame(flux)
        client.close()
        if tables is None or (isinstance(tables, list) and len(tables) == 0):
            return pd.DataFrame()
        if isinstance(tables, list):
            df = pd.concat(tables, ignore_index=True)
        else:
            df = tables
        # Giữ các cột cần thiết
        cols = [c for c in ["_time", "temp", "humi", "co", "nh3", "dust"] if c in df.columns]
        df = df[cols].rename(columns={"_time": "timestamp"})
        df["timestamp"] = pd.to_datetime(df["timestamp"])
        df.sort_values("timestamp", inplace=True)
        df.reset_index(drop=True, inplace=True)
        return df
    except Exception as e:
        print("⚠️ InfluxDB error:", e)
        client.close()
        return pd.DataFrame()


# ==== Backup dữ liệu ra file JSON ====
def backup_data(df: pd.DataFrame):
    try:
        if df.empty:
            data = {}
        else:
            df_copy = df.copy()
            df_copy["timestamp"] = df_copy["timestamp"].dt.strftime("%Y-%m-%d %H:%M:%S")
            data = df_copy.set_index("timestamp").to_dict(orient="index")
        data["_last_backup"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with open(BACKUP_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"✅ Backup thành công: {BACKUP_FILE} ({data['_last_backup']})")
    except Exception as e:
        print("⚠️ Lỗi backup:", e)


# ==== Phát hiện bất thường bằng ECOD + ngưỡng cố định ====
def detect_anomaly(df: pd.DataFrame):
    if df.shape[0] < 10:
        return [], None
    features = ["temp", "humi", "co", "nh3", "dust"]
    X = df[features].values
    clf = ECOD()
    clf.fit(X)
    is_anomaly = clf.labels_[-1]
    latest = df.iloc[-1]
    anomalies = []

    if latest["temp"] > THRESHOLDS["temp"]:
        anomalies.append(f"TEMP HIGH: {latest['temp']:.2f}°C")
    if latest["humi"] < THRESHOLDS["humi_low"] or latest["humi"] > THRESHOLDS["humi_high"]:
        anomalies.append(f"HUMI ABNORMAL: {latest['humi']:.2f}%")
    if latest["co"] > THRESHOLDS["co"]:
        anomalies.append(f"CO HIGH: {latest['co']:.2f} ppm")
    if latest["nh3"] > THRESHOLDS["nh3"]:
        anomalies.append(f"NH3 HIGH: {latest['nh3']:.2f} ppm")
    if latest["dust"] > THRESHOLDS["dust"]:
        anomalies.append(f"DUST HIGH: {latest['dust']:.2f} µg/m3")
    if is_anomaly == 1:
        anomalies.append("Anomaly detected by ECOD (multivariate)")

    return anomalies, latest


# ==== Dự báo 1-6 giờ + xu hướng bằng Prophet ====
def forecast_with_trend(df: pd.DataFrame, col: str):
    df_prophet = df[["timestamp", col]].rename(columns={"timestamp": "ds", col: "y"})
    df_prophet["y"] = np.log(df_prophet["y"] + 1)

    model = Prophet(
        daily_seasonality=True,
        weekly_seasonality=False,
        yearly_seasonality=False
    )
    model.fit(df_prophet)
    future = model.make_future_dataframe(periods=6, freq="h")
    fc_raw = model.predict(future)
    fc_raw["yhat"] = (np.exp(fc_raw["yhat"]) - 1).abs()
    fc = fc_raw[["ds", "yhat"]].tail(6)

    if fc["yhat"].iloc[-1] > fc["yhat"].iloc[0]:
        trend = "↑ increasing"
    elif fc["yhat"].iloc[-1] < fc["yhat"].iloc[0]:
        trend = "↓ decreasing"
    else:
        trend = "→ stable"
    return fc, trend


# ==== Gửi alert tới ESP32 qua HTTP ====
def send_alert(anomalies, latest):
    try:
        ts = latest["timestamp"].strftime("%Y-%m-%d %H:%M:%S") \
            if hasattr(latest["timestamp"], "strftime") else str(latest["timestamp"])
        payload = {
            "type": "anomaly",
            "time": ts,
            "anomalies": anomalies,
            "row": {
                "temp": float(latest["temp"]),
                "humi": float(latest["humi"]),
                "co":   float(latest["co"]),
                "nh3":  float(latest["nh3"]),
                "dust": float(latest["dust"]),
            }
        }
        r = requests.post(ESP32_HTTP, json=payload, timeout=5)
        print("📤 Sent alert to ESP32:", r.status_code)
    except Exception as e:
        print("⚠️ HTTP send_alert error:", e)


# ==== Main loop ====
def main():
    print(f"🚀 Air-data monitor started — InfluxDB: {INFLUX_URL}, bucket: {INFLUX_BUCKET}")
    while True:
        df = get_influx_data(hours=24)
        backup_data(df)

        if df.empty:
            print("ℹ️ Chưa có dữ liệu trong InfluxDB.")
            time.sleep(POLL_INTERVAL)
            continue

        print(f"📊 Tổng {len(df)} bản ghi. Mới nhất: {df.iloc[-1]['timestamp']}")

        anomalies, latest_row = detect_anomaly(df)
        if anomalies:
            ts = latest_row["timestamp"].strftime("%Y-%m-%d %H:%M:%S")
            print(f"⚠️ Anomalies detected at {ts}:")
            for a in anomalies:
                print(" -", a)
            send_alert(anomalies, latest_row)
        else:
            print(f"✅ Dữ liệu bình thường.")

        # Forecast + trend
        for col in ["temp", "humi", "co", "nh3", "dust"]:
            try:
                fc, trend = forecast_with_trend(df, col)
                print(f"📈 Forecast {col} (6h tới, trend: {trend}):")
                for t, y in zip(fc["ds"], fc["yhat"]):
                    print(f"   {t.strftime('%H:%M')} → {y:.2f}")
            except Exception as e:
                print(f"⚠️ Forecast error for {col}:", e)

        print(f"⏳ Chờ {POLL_INTERVAL // 60} phút...\n")
        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
