#ifndef MAIN_H
#define MAIN_H
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <WiFi.h>
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void setup()
{
    Serial.begin(115200);
    WiFi.begin(wifi_ssid);
}

#endif // MAIN_H
