// Bahçe Kapısı (araç girişi, 2 kanat) - Konteyner (ESP32) tarafı: fiziksel
// buton + RS485 komut gönderimi + HTTP API. Motor/röle mantığının kendisi
// ESP8266/Sudepo tarafında (bkz esp8266_slave/src/bahce_kapisi.cpp), burada
// sadece komut iletilir. Bkz proje hafızası project_bahce_kapisi_motor_gelecek_ozellik.
#ifndef BAHCE_KAPISI_H
#define BAHCE_KAPISI_H

#include <Arduino.h>

void bahceKapiButonPoll();
bool bahceKapiKomutGonder(const char* aksiyon, String& reply);
void handleAPI_BahceKapi();

#endif
