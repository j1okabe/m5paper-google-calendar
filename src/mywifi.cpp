#include "mywifi.h"

#include <WiFi.h>
#include <Arduino.h>

bool MYWIFI::connect(const char *ssid, const char *password, int timeout) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    for (int attempt = 1; attempt <= 2; attempt++) {
        Serial.printf("connecting to WiFi: %s (attempt %d)\n", ssid, attempt);
        WiFi.disconnect(true, true);
        delay(500);
        WiFi.begin(ssid, password);

        const unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
            const unsigned long elapsed = millis() - start;
            if (elapsed / 1000 > (unsigned long)timeout) {
                Serial.printf("\nWiFi connect timeout, status=%d\n", WiFi.status());
                break;
            }
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nconnected: ip=%s rssi=%d\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.RSSI());
            return true;
        }

        delay(1000);
    }

    WiFi.disconnect(true, true);
    return false;
}

void MYWIFI::disconnect() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}
