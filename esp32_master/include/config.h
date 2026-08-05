#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// SuDepoSis v2 - Ana Konfigürasyon
// ============================================================

// ===== Gizli Bilgiler (WiFi/OTA/MQTT sifreleri) =====
// secrets.h git disinda tutulur (.gitignore). Ilk kurulumda
// secrets.h.example dosyasini secrets.h olarak kopyalayip
// gercek degerlerini gir.
#include "secrets.h"

// ===== WiFi Ayarları =====
#define WIFI_STATIC_IP_OCTET_1 192
#define WIFI_STATIC_IP_OCTET_2 168
#define WIFI_STATIC_IP_OCTET_3 1
#define WIFI_STATIC_IP_OCTET_4 50
#define WIFI_GATEWAY_OCTET_1 192
#define WIFI_GATEWAY_OCTET_2 168
#define WIFI_GATEWAY_OCTET_3 1
#define WIFI_GATEWAY_OCTET_4 1
#define WIFI_SUBNET_OCTET_1 255
#define WIFI_SUBNET_OCTET_2 255
#define WIFI_SUBNET_OCTET_3 255
#define WIFI_SUBNET_OCTET_4 0
#define WIFI_DNS1_OCTET_1 8
#define WIFI_DNS1_OCTET_2 8
#define WIFI_DNS1_OCTET_3 8
#define WIFI_DNS1_OCTET_4 8
#define WIFI_DNS2_OCTET_1 1
#define WIFI_DNS2_OCTET_2 1
#define WIFI_DNS2_OCTET_3 1
#define WIFI_DNS2_OCTET_4 1
#define MDNS_NAME "kalburum"

// ===== OTA Ayarları =====
#define OTA_HOSTNAME "kalburum"

// ===== AP Mod Ayarları =====
#define AP_SSID "kalburum"
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

// ===== MQTT Ayarları =====
#define MQTT_BROKER "127.0.0.1"  // Lokal Mosquitto broker
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "sudeposis-master"

// MQTT Topics
#define MQTT_TOPIC_LEVEL "sudeposis/level"
#define MQTT_TOPIC_ESP8266 "sudeposis/esp8266/status"
#define MQTT_TOPIC_NANO "sudeposis/nano/status"
#define MQTT_TOPIC_ALARM "sudeposis/alarm"
#define MQTT_TOPIC_COMMAND "sudeposis/command"

// ===== RS485 Ayarları =====
// ESP8266 slave SoftwareSerial için 9600 baud seçildi.
#define RS485_BAUDRATE 9600
#define RS485_RX_PIN 16      // GPIO16 (UART1)
#define RS485_TX_PIN 17      // GPIO17 (UART1)
#define RS485_DE_PIN 2       // GPIO2 (Verici Enable)
#define RS485_UART_NUM 1     // UART1

// NOT: ESP8266 status satiri (alarm mod/mute/pending alanlari eklendikten
// sonra) ~270 karaktere ulasti. 9600 baud'da bu ~280ms surer - eski
// 200ms/500ms degerleri mesaji ortadan kesiyor, "anlik degerlerin sifirlanip
// duzelmesi" buradan kaynaklaniyordu. RS485_TIMEOUT_MS bu yuzden buyutuldu.
// RS485_UPDATE_INTERVAL ilk seferde 1000ms'e cikarilmisti ama bu, tam okuma
// artik guvenilir calisirken (~300-350ms/dongu) gereksiz yere yavas kaldi -
// 600ms hem gecikmeyi geri toparliyor hem de donguler arasinda cakismaya
// yetecek pay (~250ms) birakiyor.
#define RS485_TIMEOUT_MS 400
#define RS485_UPDATE_INTERVAL 600

// ===== Depo Ayarları =====
#define TANK_EMPTY_CM 190.0
#define TANK_FULL_CM 15.0
#define TANK_CAPACITY_LITERS 3000.0
#define TANK_HORIZONTAL 1    // 1 = Yatay silindir, 0 = Dikey

// ===== Alarm Eşikleri =====
#define ALARM_LEVEL_PERCENT 15.0
#define ALARM_LEAK_THRESHOLD_MINUTES 60
#define ALARM_ROLE_ACTIVE 1  // NC röle

// ===== Zaman Ayarları =====
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 6
#define DAY_MEASUREMENT_INTERVAL 60    // saniye
#define NIGHT_MEASUREMENT_INTERVAL 900 // saniye

// ===== SPIFFS Dosyaları =====
#define SPIFFS_SIZE 8192  // 8KB SPIFFS
#define FILE_KAYITLAR "/kayitlar.csv"
#define FILE_SETTINGS "/settings.json"
#define FILE_MONTHLY_ARCHIVE "/monthly_archive.csv"
#define FILE_ALARM_LOG "/alarm_log.csv"

// ===== Web Sunucu =====
#define WEB_PORT 80
#define WEB_UPDATE_INTERVAL 10000  // 10s live update

// ===== Device IDs (RS485 Protocol) =====
#define DEVICE_ID_ESP8266 0x01
#define DEVICE_ID_NANO 0x02
#define DEVICE_ID_MASTER 0x00

// ===== Özellikler =====
#define ENABLE_MQTT 1
#define ENABLE_OTA 1
#define ENABLE_SPIFFS 1
#define DEBUG_SERIAL 1

// ===== Serial Debug =====
#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#endif // CONFIG_H
