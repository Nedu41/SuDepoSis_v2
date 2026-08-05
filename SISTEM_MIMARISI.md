# 🏗️ SuDepoSis v2 - Sistem Mimarisi

## 📊 Genel Yapı

```
                    ┌─────────────────────────────┐
                    │   ESP32-S3 S (Ana Sunucu)   │
                    │  Konum: Konteyner 20m aşağı│
                    │                             │
                    │ ✓ UART1 RS485 Master        │
                    │   (GPIO16 RX, GPIO17 TX)    │
                    │ ✓ Web Dashboard (port 80)   │
                    │ ✓ MQTT Publisher            │
                    │ ✓ Veri Hub (CSV/SPIFFS)     │
                    │ ✓ Alarm Karar Merkezi       │
                    │ ✓ OTA Güncellemeler         │
                    └────────────┬────────────────┘
                                 │
                    RS485 (CAT5 Kablo - 20m)
                    Baudrate: 115200 baud
                    Message: CSV Format
                                 │
                ┌────────────────┼─────────────────┐
                │                │                 │
      ┌─────────▼──────┐  ┌───────▼────────┐  ┌────▼──────┐
      │ ESP8266 12E    │  │  Arduino Nano  │  │ MAX485#2   │
      │                │  │                │  │ (Yedek)    │
      │ ✓ HC-SR04      │  │ ✓ 2x Kapı      │  │            │
      │ ✓ RTC         │  │ ✓ 1x Röle (NC) │  │ (Standby)  │
      │ ✓ Seri Nano    │  │ ✓ Status LED   │  │            │
      │   (9600 baud)  │  │                │  │            │
      │ ✓ RS485 Slave  │  └────────────────┘  └────────────┘
      │ ✓ Web Local    │
      │   (port 8080)  │
      └────────────────┘
```

---

## 🔄 Veri Akışı (Per Cycle)

```
1. ESP32-S3 (master) RS485'ten talep gönderir (500ms)
   └─ "MASTER:REQUEST_ESP8266"

2. ESP8266 yanıt verir (timeout: 1000ms)
   ├─ HC-SR04 ölçüm alır (5 ortalaması)
   ├─ Nano'ya SoftwareSerial'den veri ister
   ├─ RS485'ten cevap gönderir
   └─ "ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day"

3. ESP32-S3 mesajı parse eder
   ├─ Seviye, sıcaklık, modu güncelle
   ├─ Yüzde ve litre hesapla
   ├─ Alarm kontrol et
   └─ MQTT'ye yayımla (5s başına)

4. Web Dashboard güncelle (canlı)
   └─ Browser refresh her 5 saniye

5. Döngü tekrar (500ms sonra)
```

---

## 📡 İletişim Protokolleri

### RS485 (ESP32 ↔ ESP8266/Nano)

```
Baudrate: 115200
Format: Metin CSV
Timeout: 1000ms

Master → Slave:
  "MASTER:REQUEST_ESP8266\n"
  "MASTER:REQUEST_NANO\n"

Slave → Master:
  "ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day\n"
  "NANO:D0=0,D1=1,RELE=0\n"
```

### SoftwareSerial (ESP8266 ↔ Nano)

```
Baudrate: 9600
Format: Metin CSV

Request: "GET_STATUS\n"
Response: "D0=0,D1=1,RELE=0\n"

Commands:
  - GET_STATUS → Kapı + Röle durumu
  - RELAY_ON → Röle tetikle
  - RELAY_OFF → Röle kapat
  - PING → Bağlantı testi
```

### MQTT (ESP32-S3 → Broker)

```
Broker: 127.0.0.1:1883
Topic: sudeposis/level
QoS: 1
Interval: 5 saniye

Payload: JSON
{
  "level_cm": 45.2,
  "level_percent": 72.0,
  "level_liters": 2150.0,
  "temperature": 26.0,
  "night_mode": false,
  "nano": {
    "door1": false,
    "door2": true,
    "relay": true
  }
}
```

---

## 🔋 Sensör Detayları

### HC-SR04 Ultrasonik

```
Tetik (TRIG): Düşük-yüksek pulse (10µs)
Yankı (ECHO): Dönen ses süresi
Çalışma: 2-400 cm
Ölçüm: 5 ortalaması (gürültü filtresi)
Update: 2 saniye
```

**Formül:**
```
Mesafe (cm) = (Echo_Time_µs * 0.0343) / 2
```

### RTC DS1307

```
Protokol: I2C
SDA: D2 (GPIO4)
SCL: D1 (GPIO5)
Fonksiyon: Gece modu tetikleme
```

### Kapı Sensörleri (Nano)

```
D10: Sol Kapı (Input, PULLUP)
D11: Sağ Kapı (Input, PULLUP)

Açık: HIGH (Magnetik switch açık)
Kapalı: LOW (Magnetik switch kapalı)
Debounce: 50ms
```

### Röle (NC - Normally Closed)

```
D9: Röle kontrolü (Output)
Tip: Normalde Kapalı (NC)

HIGH (devre açık): Röle pasif
LOW (devre kapalı): Röle tetiklenmiş (uyarı)

Tetikleme: Kapı açık durumunda
```

---

## 💾 Veri Depolama

### SPIFFS (ESP32-S3)

```
Dosyalar:
├── /kayitlar.csv
│   Format: Tarih, Kişi, Litre, Ücret, Kaynak
│   Max: 500 kayıt
│
├── /settings.json
│   Ayarlar, eşikler
│
├── /monthly_archive.csv
│   Aylık arşivler (tekil veriler)
│
└── /alarm_log.csv
    Alarm olayları, zamanlar
```

### CSV Format

```
kayitlar.csv:
2026-07-26T14:30:45,Tanker,3000.0,1200.00,Tanker

alarm_log.csv:
2026-07-26T14:30:45,LOW_LEVEL,15.0%
2026-07-26T14:35:00,DOOR_OPEN,Sol Kapı
```

---

## ⚠️ Alarm Sistemi

### Alarm Türleri

| Alarm | Eşik | Tetik | Aksiyon |
|-------|------|-------|---------|
| Düşük Seviye | < 15% | Ölçüm | MQTT yayımla |
| Su Kaçağı | 60+ dak düşüş | Trend | MQTT + Log |
| Kapı Açık | D0/D1 = HIGH | Anlık | Röle tetikle |
| Dönem Hatası | > 1s | Zaman aşımı | Log + Retry |

### Röle Tetikleme Mantığı

```cpp
if (door1_open || door2_open) {
  // NC röle: LOW = aktif (uyarı)
  digitalWrite(RELAY_PIN, LOW);
  mqttClient.publish("sudeposis/alarm", "DOOR_OPEN");
} else {
  digitalWrite(RELAY_PIN, HIGH);
}
```

---

## 🌙 Gece Modu

```
Başlama: 22:00 (NIGHT_START_HOUR)
Bitme: 06:00 (NIGHT_END_HOUR)

Gündüz:
  - Ölçüm Aralığı: 60 saniye
  - RS485 Update: 500ms

Gece:
  - Ölçüm Aralığı: 900 saniye (15 dak)
  - RS485 Update: 500ms
  - Enerji tasarrufu
```

---

## 🎯 Başarı Kriterleri

✅ **Donanım Testi:**
- RS485: 115200 baud, mesaj alış-verişi ✓
- HC-SR04: 2-400 cm ölçüm ✓
- SoftwareSerial: 9600 baud, Nano iletişim ✓
- Röle: NC fonksiyonu ✓

✅ **Yazılım Testi:**
- ESP32-S3 Web: http://[IP]:80 ✓
- ESP8266 Local: http://[IP]:8080 ✓
- MQTT: sudeposis/level yayımı ✓
- Alarm: Kapı/seviye tetikleme ✓

✅ **Entegrasyon:**
- Tüm 3 cihaz senkronize
- Veri merkezileştirildi (ESP32)
- CSV arşiv çalışıyor

---

## 📈 Performans

| Metrik | Değer | Not |
|--------|-------|-----|
| RS485 Gecikme | ~100ms | İnsan hissetmez |
| Web Refresh | 5 saniye | Canlı görünüm |
| MQTT Publish | 5 saniye | Çoğu IoT için yeterli |
| HC-SR04 Ölçüm | 100ms | 5 ortalaması |
| Nano Poll | 1 saniye | Kapı hassasiyeti |
| CPU Kullanım | ~10% | Rahat |
| Güç Tüketimi | ~500mA | Standby |

---

## 🔄 Sistem Başlatma Sırası

```
1. Arduino Nano boot (LED blink)
   └─ Kapı sensörleri hazır, röle kapalı

2. ESP8266 boot (serial log)
   └─ HC-SR04 kalibre, RTC kontrol
   └─ Nano ile SoftwareSerial test
   └─ RS485 slave hazır

3. ESP32-S3 boot (serial log)
   └─ RS485 master hazır
   └─ WiFi bağlantı
   └─ MQTT bağlantı
   └─ Web sunucusu açık
   └─ Canlı data polling başla

4. Dashboard açılabilir
   └─ Veri canlı görülür
```

---

## 🚀 Ölçeklenebilirlik

Gelecek için:
- [ ] 2. ESP8266 slave (başka sensörler)
- [ ] Weatherstation (yağmur, sıcaklık)
- [ ] Depo pump kontrolü (otomatik dolum)
- [ ] SMS/Email alertlar
- [ ] Cloud sync (optional)

---

**Sistem Tasarım Tamam!** 🎯

