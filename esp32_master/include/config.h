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
// NOT: ESP32-S3'te UART pinleri (klasik ESP32'nin aksine) sabit degil - GPIO
// matrisi uzerinden Serial1.begin() cagrisinda hangi pin verilirse o kullanilir
// (bkz main.cpp). Bu yuzden RX/TX/DE pinleri, karttaki fiziksel kablolamaya en
// uygun herhangi bir SERBEST GPIO'ya (asagidaki Konteyner Donanimi bolumundeki
// listeye bkz) tasinabilir - donanimsal/yazilimsal bir kisit yok. Kartin sag-
// orta bolgesine denk gelen GPIO37/38/39 secildi (kullanicinin tercihi).
#define RS485_BAUDRATE 9600
#define RS485_RX_PIN 37      // GPIO37 - MAX485 RO (Alici cikisi -> ESP32 RX)
#define RS485_TX_PIN 38      // GPIO38 - MAX485 DI (Verici girisi <- ESP32 TX)
#define RS485_DE_PIN 39      // GPIO39 - MAX485 DE/RE (Verici Enable)
#define RS485_UART_NUM 1     // UART1

// ===== MPPT (Modbus RTU) Ayarlari - "victor NML 3200-24" sarj kontrolcu =====
// UYARI: Asagidaki register adresi/olcek DOGRULANMAMIS - cihazin gercek
// modeli/protokolu netlesmedigi icin EPEVER Tracer-klonu cihazlarda yaygin
// olan tipik bir Modbus haritasi TAHMIN olarak kondu. Faz 1'de Serial
// Monitor'daki [MPPT] logundaki voltaj degeri gercek bir multimetre
// olcumuyle karsilastirilip DOGRULANMADAN gercek deger olarak GUVENME.
// Yanlissa SADECE bu blok duzeltilir, baska hicbir yeri degistirmeye
// gerek yok (mpptPoll() vs. bu define'lari kullanir, register/olcegi
// hardcode etmez).
// Mevcut ESP8266 RS485 hattindan (UART1, GPIO37/38/39) TAMAMEN AYRI bir
// bus/protokol - ayni hatta baglanamaz, ikinci bir MAX485 modulu gerekir.
#define MPPT_RS485_RX_PIN 10    // GPIO10 - ikinci MAX485 RO -> ESP32 RX (UART2)
#define MPPT_RS485_TX_PIN 11    // GPIO11 - ikinci MAX485 DI <- ESP32 TX (UART2)
#define MPPT_RS485_DE_PIN 12    // GPIO12 - ikinci MAX485 DE/RE
#define MPPT_UART_NUM 2         // UART2
#define MPPT_BAUDRATE 9600      // DOGRULA - cihazin kendi ayari farkli olabilir
#define MPPT_SLAVE_ID 1         // DOGRULA - Modbus slave adresi
// Register bloklari - EPEVER B-series/Tracer "real-time data" haritasindan
// (yaygin dokumante edilmis, aggsoft/ESPHome/Home Assistant entegrasyonlarinda
// da ayni adresler kullanilir - onceki 0x331A tahmininden daha guvenilir
// bir kaynak). 0x3100-0x310F ARALIKSIZ TEK OKUMAYLA (16 register) alinir.
#define MPPT_REG_BLOCK_START 0x3100
#define MPPT_REG_BLOCK_COUNT 16
#define MPPT_REG_OFS_PV_VOLTAGE 0        // 0x3100, x0.01V
#define MPPT_REG_OFS_PV_CURRENT 1        // 0x3101, x0.01A
#define MPPT_REG_OFS_PV_POWER_L 2        // 0x3102 (32-bit L, x0.01W)
#define MPPT_REG_OFS_PV_POWER_H 3        // 0x3103 (32-bit H)
#define MPPT_REG_OFS_BATTERY_VOLTAGE 4   // 0x3104 "sarj cikis voltaji" = pratikte aku voltaji, x0.01V
#define MPPT_REG_OFS_LOAD_VOLTAGE 12     // 0x310C, x0.01V
#define MPPT_REG_OFS_LOAD_CURRENT 13     // 0x310D, x0.01A
#define MPPT_REG_OFS_LOAD_POWER_L 14     // 0x310E (32-bit L, x0.01W)
#define MPPT_REG_OFS_LOAD_POWER_H 15     // 0x310F (32-bit H)
#define MPPT_REG_BATTERY_SOC 0x311A      // Aku doluluk yuzdesi (%), MPPT'nin kendi tahmini
#define MPPT_REG_SCALE 0.01f             // DOGRULA - ham_deger * olcek = Volt/Amper/Watt
#define MPPT_POLL_INTERVAL_MS 5000
#define MPPT_STALE_MS 15000

// ===== Batarya Kapasitesi (kalan kullanim suresi tahmini icin) =====
// 2x12V 100Ah jel aku SERI baglanip 24V, 100Ah'lik tek bir hat olusturuyor
// (README - "2x12V 100A jel aku"). DOGRULA - farkliysa sadece burasi degisir.
#define BATTERY_CAPACITY_AH 100.0f
#define BATTERY_NOMINAL_VOLTAGE 24.0f
#define BATTERY_CAPACITY_WH (BATTERY_CAPACITY_AH * BATTERY_NOMINAL_VOLTAGE)

// ===== Konteyner Donanimi (IR alici, alarm LED, PIR) =====
// ESP32-S3-DevKitC-1 (N8, PSRAM yok) pin planlamasi - ileride yeni eklenti
// eklerken cakisma olmasin diye:
//   KULLANILAN:    GPIO37, GPIO38, GPIO39 (RS485 - ESP8266'ya)
//                  GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9 (asagida - IR/LED+Buzzer/PIR/Reed/Siren/Lamba)
//                  GPIO10, GPIO11, GPIO12 (yukarida - MPPT Modbus RS485, ikinci MAX485)
//   ASLA KULLANMA: GPIO0, GPIO3, GPIO45, GPIO46 (strapping/boot pinleri)
//                  GPIO26-32 (bu karttaki Quad Flash icin ayrilmis)
//                  GPIO1, GPIO3 (UART0 - USB debug seri portu, bkz platformio.ini)
//   SERBEST (gelecekteki eklentiler icin): GPIO2, 13, 14, 15, 16, 17, 18, 21, 33, 34, 36, 40, 41, 42, 47, 48
#define IR_RECV_PIN 4     // GPIO4 - IR alici modulunun OUT/sinyal ucu (VCC/GND dogrudan besleme)
#define ALARM_LED_PIN 5   // GPIO5 - Kirmizi LED (+ seri direnc) VE buzzer PARALEL bagli, ayni sinyali paylasir (pin tasarrufu - ikisinin akimi GPIO limitinin altinda kalir) - kucuk/yerel sesli-gorsel isaret
#define PIR2_PIN 6        // GPIO6 - Konteynerdaki PIR hareket sensorunun OUT ucu
#define KAPI_REED_PIN 7   // GPIO7 - Konteyner kapisi reed (manyetik) switch, digital (INPUT_PULLUP)
// Siren/Lamba: ALARM_LED_PIN'den AYRI, gercek role modulu uzerinden calisan
// donanim (Sudepo Zonu'ndaki Nano "Alarm Rolesi"/"Depo Ici Lamba Rolesi" ile
// ayni role). Varsayilan HIGH=aktif - role modulunuz aktif-LOW ise (kablolama
// tersse) main.cpp'de alarmLedGuncelle() icindeki digitalWrite satirlarini
// ters cevirmeniz yeterli (reed switch'teki gibi tek satirlik duzeltme).
#define KONTEYNER_SIREN_PIN 8   // GPIO8 - Alarm sireni rolesi (Sesli/Onayli+Sesli-onay'da aktif)
#define KONTEYNER_LAMBA_PIN 9   // GPIO9 - Uyari lambasi/flasoru rolesi (siren ile birlikte VEYA Onayli+"Sessiz(Lamba)" onayinda tek basina aktif)

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

// ===== Hava Durumu / Yagmur Tahmini (sabit konum, internet varken cekilir) =====
// Konum secimi/geocode YOK - bahce sabit, koordinat burada tanimli. Kalburum'un
// kendi RTC'si olmadigi icin "ne zaman cekildi" ESP8266'nin DS1307 RTC'sinden
// (mevcut GET_ZAMAN RS485 komutuyla) okunan gercek takvim gunune gore izlenir -
// NTP/epoch senkronizasyonuna gerek kalmaz.
#define GARDEN_LATITUDE 40.833907
#define GARDEN_LONGITUDE 29.730263
#define WEATHER_FORECAST_API "https://api.open-meteo.com/v1/forecast"
#define WEATHER_FORECAST_DAYS 7
#define WEATHER_RAIN_THRESHOLD_MM 1.0f          // Yarin bu kadar mm+ beklenirse sulama atlanir
#define WEATHER_STALE_DAYS 7                     // Bu kadar gunden eski tahmin gormezden gelinir (fail-open)
#define WEATHER_CHECK_INTERVAL_MS (30UL * 60UL * 1000UL)  // Guncellik/oneri bu araliklarla yeniden hesaplanir

// ===== OTA (GitHub'dan guncelleme) =====
// Sabit "en son surum" linki: repoya push edilen .bin dosyasina isaret eder,
// versiyon karsilastirmasi yapilmaz - "Guncelle" tusuna basildiginda dogrudan
// bu adresten indirilip yazilir. Yeni bir firmware yayinlamak icin:
//   1) pio run (esp32_master) ile derle
//   2) .pio/build/esp32s3/firmware.bin dosyasini esp32_master/firmware/esp32.bin'e kopyala
//   3) main dalina push et - link otomatik guncel halini gosterir (raw.githubusercontent.com HEAD'i takip eder)
#define GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/Nedu41/SuDepoSis_v2/main/esp32_master/firmware/esp32.bin"

// ===== BLE (Telefon Uygulamasi) =====
// Telefondaki eslesmis uygulama, WiFi agina hic girmeden dogrudan BLE ile
// Kontrol sekmesindeki komutlari (lamba/alarm/kapi/panik) gonderip anlik
// durumu okuyabilsin diye. UUID'ler ve cihaz adi test edilen ornek koddan
// (zaten calisiyordu) aynen tasindi - telefon tarafi degismemeli.
#define ENABLE_BLE 1
#define BLE_DEVICE_NAME "ESP32S3_Yonetici"
#define BLE_SERVICE_UUID        "4faac001-82ab-4dc1-9106-97217895d03a"
#define BLE_CHARACTERISTIC_UUID "3a200001-526b-4e01-9fa6-07217895d03a"
#define BLE_NOTIFY_INTERVAL_MS 2000
// Agir TLS (HTTPS - hava durumu/Telegram) istekleri bu esigin altinda heap
// varken atlanir; dusuk heap'te BLE/WiFi coexistence altinda cokme/reset
// (ve bunun sonucu "kirli" BLE kopmasi) riskini azaltir - bkz bleKomutIsle
// yanindaki BLE bolumu notu.
#define BLE_SAFE_MIN_HEAP 40000

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
