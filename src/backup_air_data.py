import json
import os
import time
from datetime import datetime

import numpy as np
import pandas as pd
import requests
from prophet import Prophet
from pyod.models.ecod import ECOD

# ==== Cấu hình (lấy từ biến môi trường, có default) ====
FIREBASE_URL = os.getenv(
    "FIREBASE_URL",
    "https://nckh-clkk-default-rtdb.asia-southeast1.firebasedatabase.app/sensor_data.json",
)
ESP32_HTTP = os.getenv("ESP32_HTTP", "http://192.168.1.17/alert")  # IP ESP32 thực tế
BACKUP_FILE = os.getenv("BACKUP_FILE", "backup.json")
POLL_INTERVAL = int(os.getenv("POLL_INTERVAL", "600"))  # giây, mặc định 10 phút

# ==== Ngưỡng cảnh báo ====
THRESHOLDS = {
    "temp": 35.0,
    "humi_low": 30.0,
    "humi_high": 90.0,
    "co": 12.5,
    "nh3": 20.0,
    "dust": 35.0  # µg/m3
}

# ==== Backup Firebase ====
def backup_firebase(df):
    try:
        if df.empty:
            data = {}
        else:
            df_copy = df.copy()
            # Chuyển timestamp sang str trước khi backup
            df_copy["timestamp"] = df_copy["timestamp"].dt.strftime("%Y-%m-%d %H:%M:%S")
            data = df_copy.set_index("timestamp").to_dict(orient="index")
        data["_last_backup"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with open(BACKUP_FILE, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"✅ Backup thành công: {BACKUP_FILE} ({data['_last_backup']})")
    except Exception as e:
        print("⚠️ Lỗi backup:", e)

# ==== Lấy dữ liệu từ Firebase ====
def get_firebase_data():
    try:
        r = requests.get(FIREBASE_URL, timeout=10)
        data = r.json()
        if not data:
            return pd.DataFrame()
        rows = []
        for key, val in data.items():
            if key == "_last_backup":
                continue
            row = val.copy()
            row["timestamp"] = datetime.strptime(val["time"], "%Y-%m-%d %H:%M:%S")
            rows.append(row)
        df = pd.DataFrame(rows)
        df.sort_values("timestamp", inplace=True)
        df.reset_index(drop=True, inplace=True)
        return df
    except Exception as e:
        print("⚠️ Firebase error:", e)
        return pd.DataFrame()

# ==== Phát hiện bất thường ====
def detect_anomaly(df):
    if df.shape[0] < 10:
        return [], None
    features = ["temp", "humi", "co", "nh3", "dust"]
    X = df[features].values
    clf = ECOD()
    clf.fit(X)
    is_anomaly = clf.labels_[-1]
    latest = df.iloc[-1]
    anomalies = []

    # Ngưỡng cố định
    if latest["temp"] > THRESHOLDS["temp"]:
        anomalies.append(f"TEMP HIGH: {latest['temp']}°C")
    if latest["humi"] < THRESHOLDS["humi_low"] or latest["humi"] > THRESHOLDS["humi_high"]:
        anomalies.append(f"HUMI ABNORMAL: {latest['humi']}%")
    if latest["co"] > THRESHOLDS["co"]:
        anomalies.append(f"CO HIGH: {latest['co']} ppm")
    if latest["nh3"] > THRESHOLDS["nh3"]:
        anomalies.append(f"NH3 HIGH: {latest['nh3']} ppm")
    if latest["dust"] > THRESHOLDS["dust"]:
        anomalies.append(f"DUST HIGH: {latest['dust']} µg/m3")

    # Kiểm tra ECOD
    if is_anomaly == 1:
        anomalies.append("Anomaly detected by ECOD (multivariate)")

    return anomalies, latest

# ==== Dự báo 1-6 giờ + xu hướng ====
def forecast_with_trend(df, col):
    df_prophet = df[["timestamp", col]].rename(columns={"timestamp": "ds", col: "y"})
    
    # === Log-transform để ổn định mô hình Prophet ===
    df_prophet["y"] = np.log(df_prophet["y"] + 1)

    model = Prophet(
        daily_seasonality=True,
        weekly_seasonality=False,
        yearly_seasonality=False
    )
    model.fit(df_prophet)

    future = model.make_future_dataframe(periods=6, freq='h')
    fc_raw = model.predict(future)

    # === Chuyển dự báo về giá trị thực ===
    fc_raw["yhat"] = np.exp(fc_raw["yhat"]) - 1
    fc_raw["yhat"] = fc_raw["yhat"].abs()  
    fc = fc_raw[["ds", "yhat"]].tail(6)

    # Nhận xét xu hướng
    if fc["yhat"].iloc[-1] > fc["yhat"].iloc[0]:
        trend = "↑ increasing"
    elif fc["yhat"].iloc[-1] < fc["yhat"].iloc[0]:
        trend = "↓ decreasing"
    else:
        trend = "→ stable"

    return fc, trend


# ==== Gửi alert tới ESP32 ====
def send_alert(anomalies, latest):
    try:
        payload = {
            "type": "anomaly",
            "time": latest["time"],
            "anomalies": anomalies,
            "row": {
                "temp": latest["temp"],
                "humi": latest["humi"],
                "co": latest["co"],
                "nh3": latest["nh3"],
                "dust": latest["dust"]
            }
        }
        r = requests.post(ESP32_HTTP, json=payload, timeout=5)
        print("📤 Sent alert to ESP32:", r.status_code)
    except Exception as e:
        print("⚠️ HTTP error:", e)

# ==== Main loop ====
def main():
    while True:
        df = get_firebase_data()
        backup_firebase(df)

        if df.empty:
            print("ℹ️ No data yet.")
            time.sleep(POLL_INTERVAL)
            continue

        anomalies, latest_row = detect_anomaly(df)
        if anomalies:
            print(f"⚠️ Anomalies detected at {latest_row['time']}:")
            for a in anomalies:
                print(" -", a)
            send_alert(anomalies, latest_row)
        else:
            print(f"✅ Data normal at {latest_row['time']}")

        # Forecast + trend
        for col in ["temp", "humi", "co", "nh3", "dust"]:
            try:
                fc, trend = forecast_with_trend(df, col)
                print(f"📈 Forecast for {col} (next 6h, trend: {trend}):")
                for t, y in zip(fc["ds"], fc["yhat"]):
                    print(f"   {t.strftime('%H:%M')} → {y:.2f}")
            except Exception as e:
                print(f"⚠️ Forecast error for {col}:", e)

        print(f"⏳ Waiting {POLL_INTERVAL//60} min...\n")
        time.sleep(POLL_INTERVAL)

if __name__ == "__main__":
    main()
