#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// SuDepoSis v2 - ESP8266 Ölçüm Slave Konfigürasyonu
// Güncellenmiş: v12 arayüz + RS485 + Nano + Lamba
// ============================================================

// ===== RS485 Ayarları (ESP32-S3 Master ile haberleşme) =====
// Daha güvenilir haberleşme için esp8266 SoftwareSerial ile 9600 baud kullan.
#define RS485_BAUDRATE 9600
#define RS485_TX_PIN D0       // GPIO16 - MAX485 DI
#define RS485_RX_PIN D7       // GPIO13 - MAX485 RO
#define RS485_DE_PIN D3       // GPIO0 - MAX485 DE/RE (boot: LOW, jumper 3V3 for upload)
#define RS485_TIMEOUT_MS 1000
#define RS485_SEND_INTERVAL 1000  // 1 saniyede bir master'a veri gönder

// ===== HC-SR04 Ultrasonik Sensör =====
#define TRIG_PIN D5           // GPIO14
#define ECHO_PIN D6           // GPIO12
#define HCSR04_SAMPLES 3
#define HCSR04_UPDATE_INTERVAL 2000  // 2 saniye

// ===== Nano UART0 Haberleşmesi (HardwareSerial) =====
// ESP8266 D9(GPIO3)=UART0 RX ← Nano TX
// ESP8266 D10(GPIO1)=UART0 TX → Nano RX
// Arada 470Ω direnç, ESP D10-GND arası 1KΩ direnç
#define NANO_BAUDRATE 9600
// NOT: Kapi/PIR/role gibi alarm tetikleyicileri sadece bu sorguyla taze kalir.
// 1000ms iken bir tetiklenmenin fark edilmesi (+ ESP32'ye ulasmasi) toplamda
// saniyelerce surebiliyordu. 300ms'e cekildi - Nano yaniti tipik olarak
// ~10-20ms surdugu icin bus/CPU yukunu onemli olcude artirmiyor.
#define NANO_POLL_INTERVAL 300

// ===== Depo Lambası (Nano kontrolü) =====
#define LAMBA_PIN D13          // xxx - Depo iç lamba rölesi (Nano tetikler)

// ===== PIR Sensörü =====
// Artik ayri PIN_READ komutuyla degil, Nano'nun GET_STATUS yanitindaki
// "PIR=" alaniyla dogrudan okunuyor (bkz nano_io PIR_PIN, config.h).

// ===== RTC I2C (DS1307) =====
#define RTC_SDA D2            // GPIO4
#define RTC_SCL D1            // GPIO5

// ===== Gizli Bilgiler (WiFi/OTA sifreleri) =====
// secrets.h git disinda tutulur (.gitignore). Ilk kurulumda
// secrets.h.example dosyasini secrets.h olarak kopyalayip
// gercek degerlerini gir.
#include "secrets.h"

// ===== WiFi Ayarları =====
#define WIFI_AP_SSID "SuDepo"
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_HIDDEN false
#define WIFI_AP_MAX_CLIENTS 4
// MDNS_HOSTNAME artık WIFI_AP_SSID'den türetiliyor (src/main.cpp içinde mdnsHostname())

// ===== Zaman Ayarları =====
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 6

// ===== Yagmur Tahmini (ESP32-S3 master'dan RS485 ile gelir) =====
// Master, SET_RAIN_SKIP=1/0 komutuyla "yarin yagmur bekleniyor" bilgisini
// bildirir. Bahcede sabit internet olmadigindan (sadece kullanici oradayken
// mobil hat/hotspot ile) master bu bilgiyi seyrek guncelleyebilir. Bu yuzden
// bayrak asiri bayatlarsa (master uzun sure guncelleme gonderemezse) sulama
// engellenmeye devam etmesin diye bir gecerlilik suresi tanimlanir - bitkinin
// susuz kalmasi, gereksiz sulamadan daha kotu bir risktir (fail-open).
#define RAIN_SKIP_STALE_MS (48UL * 3600UL * 1000UL)  // 48 saat

// ===== Depo Kalibrasyonu (Varsayılan) =====
#define TANK_EMPTY_CM 90.0
#define TANK_FULL_CM 10.0
#define TANK_CAPACITY_LITERS 3000.0
#define TANK_HORIZONTAL 1  // 1=Yatay Silindirik, 0=Dikey/Dikdörtgen

// ===== EEPROM =====
// DİKKAT: Ayarlar struct'ı ~100 byte. 128 byte sınırda;
// struct'a yeni alan eklenirse taşma riski var.
// 512 byte, ESP8266 EEPROM (4096 max) için güvenli ve geleceğe dönük.
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xA547  // alarmOutputSesli/alarmOutputSessiz eklendi (eski: 0xA546)

// Alarm CIKISLARI (siren/role, lamba) - mod basina bitmask, ALARM_TRIGGER_*
// (girdi/sensor) bitmask'inden bagimsiz ayri bir kume.
#define ALARM_OUTPUT_SIREN 1
#define ALARM_OUTPUT_LAMBA 2

// WiFi (STA) bilgileri artik ayri bir EEPROM bolgesinde, kendi magic'i ile
// saklanir. Boylece Ayarlar struct'ina alan eklenip EEPROM_MAGIC degistiginde
// (struct duzeni degistigi icin varsayilanlara donulmesi gerekir) kayitli
// WiFi agi/sifre SILINMEZ - iki bolge birbirinden bagimsiz surumlenir.
#define WIFI_EEPROM_ADDR 256
#define WIFI_EEPROM_MAGIC 0x5731  // 'W1'

// ===== Alarm Tetikleyicileri (Bitmask) =====
#define ALARM_TRIGGER_KAPI1      0x01  // Bit 0: Sol kapı
#define ALARM_TRIGGER_KAPI2      0x02  // Bit 1: Sağ kapı
#define ALARM_TRIGGER_PIR        0x04  // Bit 2: PIR sensörü (hareket)
#define ALARM_TRIGGER_SU_SEVIYE  0x08  // Bit 3: Su seviyesi düşük
#define ALARM_TRIGGER_KACAK      0x10  // Bit 4: Kaçak algılama
#define ALARM_TRIGGER_SENSOR     0x20  // Bit 5: Sensor hatası

// ===== Alarm Modları =====
#define ALARM_MOD_SESLI   1  // Tetiklenince siren/röle hemen çalışır
#define ALARM_MOD_SESSIZ  2  // Röle hiç çalışmaz, sadece web/SSE bildirimi gösterilir
#define ALARM_MOD_ONAYLI  3  // Tetiklenince önce onay bekler; onaylanınca sesli gibi davranır

// ===== Alarm Eşikleri (Varsayılan) =====
#define ALARM_THRESHOLD_PERCENT 15.0
#define MIN_FILL_LITERS 20.0
#define LEAK_THRESHOLD_MINUTES 60

// ===== Kayitlar (Dolum Gecmisi) =====
#define MAX_KAYIT_SAYISI 10  // Sadece en son bu kadar kayit tutulur, eskiler otomatik silinir

// ===== Ölçüm Aralıkları (saniye) =====
#define DAY_MEASURE_INTERVAL 60
#define NIGHT_MEASURE_INTERVAL 900

// ===== Debug =====
#define DEBUG_SERIAL 0

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H