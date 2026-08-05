# 📋 SuDepoSis v2 - Dosya Yapısı

```
SuDepoSis_v2/
├── README.md                          # 📖 Ana Dokümantasyon
├── KURULUM.md                         # 🚀 Kurulum Kılavuzu
├── SISTEM_MIMARISI.md                 # 🏗️ Teknik Mimari
├── DEBUG_KLAVUZU.md                   # 🔧 Test & Sorun Giderme
│
├── esp32_master/                      # 🟦 ESP32-S3 Ana Sunucu
│   ├── platformio.ini                 # Proje ayarları
│   ├── include/
│   │   └── config.h                   # Sabit tanımlamalar
│   ├── src/
│   │   └── main.cpp                   # Ana program (950 satır)
│   └── data/
│       └── (Web UI - statik dosyalar)
│
├── esp8266_slave/                     # 🟧 ESP8266 Ölçüm Slave
│   ├── platformio.ini                 # Proje ayarları
│   ├── include/
│   │   └── config.h                   # Sabit tanımlamalar
│   ├── src/
│   │   └── main.cpp                   # Ana program (450 satır)
│   └── include/
│       └── (Başlık dosyaları)
│
└── nano_io/                           # 🔷 Arduino Nano I/O
    ├── platformio.ini                 # Proje ayarları
    ├── include/
    │   └── config.h                   # Pin tanımları ve sabitler
    └── src/
        └── main.cpp                   # Ana program (C++ - 195 satır)
```

---

## 📊 Kod İstatistikleri

| Dosya | Satır | Açıklama |
|-------|-------|----------|
| esp32_master/src/main.cpp | ~950 | UART1 + Web + MQTT + RS485 |
| esp8266_slave/src/main.cpp | ~450 | HC-SR04 + SoftwareSerial + RS485 |
| nano_io/src/main.cpp | ~195 | I/O kontrol + Seri (C++) |
| **TOPLAM KOD** | **~1650** | |

---

## 🎯 Dosya İçerikleri

### ESP32-S3 Master

**main.cpp içerir:**
- ✅ RS485 UART1 driver (Master)
- ✅ Web sunucusu (JSON API + HTML Dashboard)
- ✅ MQTT client (Mosquitto)
- ✅ Veri parse & alarm (RS485 mesajları)
- ✅ WiFi bağlantısı
- ✅ Real-time status updates

**config.h içerir:**
- WiFi SSID/Password
- MQTT broker adres
- RS485 pin tanımları (GPIO16, 17, 2)
- Alarm eşikleri
- Depo kalibrasyonu

### ESP8266 Slave

**main.cpp içerir:**
- ✅ HC-SR04 sensör (5 ortalaması)
- ✅ SoftwareSerial Nano haberleşmesi (9600)
- ✅ RS485 Slave (115200)
- ✅ RTC I2C başlatması
- ✅ Local web server (8080)
- ✅ Veri topla ve gönder

**config.h içerir:**
- Pin tanımları (D0-D10)
- HC-SR04 örnekleme
- Nano serial ayarları
- Depo kalibrasyonu

### Arduino Nano

**main.cpp içerir:**
- ✅ Kapı sensörü okuma (D10, D11, debounce)
- ✅ Röle kontrol (D9, NC lojik)
- ✅ Seri komut parser
- ✅ Durum gönder
- ✅ LED status

---

## 🔌 Bağlantı Özeti

### RS485 Network

```
Master: ESP32-S3 UART1
├── GPIO16 (RX) → MAX485 RO
├── GPIO17 (TX) → MAX485 DI
└── GPIO2 (DE/RE) → MAX485 DE/RE

Slave: ESP8266
├── D7 / GPIO13 (RX) → MAX485 RO
├── D0 / GPIO16 (TX) → MAX485 DI
└── D4 / GPIO2 (DE/RE) → MAX485 DE/RE

Link: CAT5 Kablo (A↔A, B↔B) - 20 meter
```

### SoftwareSerial (ESP8266 ↔ Nano)

```
ESP8266:
├── D10 / GPIO1 ← Nano D0 (RX)
├── D9 / GPIO3 → Nano D1 (TX)
└── Hız: 9600 baud

Nano:
├── D0 ← ESP8266 D10 (RX)
├── D1 → ESP8266 D9 (TX)
└── Hız: 9600 baud
```

### Sensörler

```
HC-SR04 (ESP8266):
├── D5 (GPIO14) → TRIG
└── D6 (GPIO12) → ECHO

Kapı Sensörleri (Nano):
├── D10 → Sol Kapı (HIGH=OPEN)
├── D11 → Sağ Kapı (HIGH=OPEN)
└── D9 → Röle (LOW=ACTIVE, NC)

RTC I2C (ESP8266):
├── D1 (GPIO5) → SCL
└── D2 (GPIO4) → SDA
```

---

## ✨ Özellikler Özeti

### Merkez (ESP32-S3)

- 🌐 Web Dashboard (http://IP:80)
- 📡 MQTT Yayıncılık (sudeposis/level)
- 📊 CSV Veri Depolama (SPIFFS)
- 🚨 Merkez Alarm Yönetimi
- 🔄 RS485 Master (500ms update)
- 🌙 Gece Modu (22:00-06:00)

### Ölçüm Slave (ESP8266)

- 📏 HC-SR04 Ultrasonik (5 ort.)
- 🎛️ Nano SoftwareSerial haberleşme
- 📡 RS485 Slave (115200)
- 🌐 Local Web Server (8080)
- 🕐 RTC Bağlı
- 💾 İç Cache (offline)

### I/O Kontrol (Nano)

- 🚪 2x Kapı Sensörü (Debounce)
- 🔔 NC Röle Kontrolü
- 💡 Status LED
- 📡 Seri Haberleşme (9600)
- ⚡ Hafif & Hızlı

---

## 🚀 Başlama Adımları

1. **Dosyaları İndir**
   ```
   d:\Kodlar\SuDepoSis_v2\ (Hazır)
   ```

2. **VS Code + PlatformIO Kur**
   ```
   Extensions: PlatformIO IDE
   ```

3. **Her Cihazı Yükle**
   ```
   ESP32-S3: Ctrl+Alt+U
   ESP8266: Ctrl+Alt+U
   Arduino Nano: IDE Ctrl+U
   ```

4. **Serial Monitor Test**
   ```
   115200 baud - Logları kontrol et
   ```

5. **Web Dashboard Aç**
   ```
   http://192.168.4.x
   ```

6. **MQTT Subscribe** (Optional)
   ```
   mosquitto_sub -h localhost -t "sudeposis/#"
   ```

---

## 📝 Sonraki Adımlar

- [ ] OTA güncelleme (WiFi üzerinden)
- [ ] Bulut sync (optional)
- [ ] SMS/Email alertları
- [ ] Mobil app (companion)
- [ ] Depo pump otomasyonu

---

**Sistem Tamamlandı ve Hazır!** 🎯

Sorular?
- DEBUG_KLAVUZU.md → Test prosedürü
- SISTEM_MIMARISI.md → Teknik detaylar
- KURULUM.md → Kurulum adımları

