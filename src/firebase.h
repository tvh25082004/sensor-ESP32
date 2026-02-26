#ifndef FIREBASE_H
#define FIREBASE_H

#include <Arduino.h>

// Biến thời gian gửi dữ liệu
extern unsigned long lastSend;
extern const unsigned long SEND_INTERVAL;
extern bool firstSendDone;

// Khai báo hàm
void sendToFirebase();
String getTimestamp();
#endif
