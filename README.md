# 💧 SuDepoSis v2 - Küresel Su Deposu Yönetim Sistemi

## 🏗️ Mimari

```
┌─────────────────────────────┐
│   ESP32-S3 (Ana Sunucu)     │  ← Konteynerde (20m aşağı)
│  - Web Arayüzü              │
│  - Merkez Veri Tabanı       │
│  - RS485 Master             │
└────────────┬────────────────┘
             │ RS485 (CAT5)
    ┌────────┴────────┐
    │                 │
┌───▼──────┐    ┌──────▼─────┐
│ ESP8266   │    │ Arduino     │
│(Üstte)    │    │ Nano        │
│ Sensörler │    │ I/O Control │
└────┬──────┘    └──────┬─────┘
     │                   │
  HC-SR04            Kapı + Röle
  RTC, etc.
```

## 🚀 Başlangıç Kurulumu

### 1️⃣ **Donanım Bağlantıları** (Onaylandı ✓)

| Cihaz | Pin | Bağlantı | İşlev |
|-------|-----|----------|-------|
| **ESP32-S3** | GPIO16 | RS485 RX | |
| | GPIO17 | RS485 TX | |
| | GPIO2 | RS485 DE/RE | |
| **ESP8266** | D7 (GPIO13) | HC-SR04 ECHO | Ultrasonik |
| | D5 (GPIO14) | HC-SR04 TRIG | |
| | D10 (GPIO1) | Nano RX | Seri |
| | D9 (GPIO3) | Nano TX | |
| | D0 (GPIO16) | MAX485 DI | RS485 |
| | D7 (GPIO13) | MAX485 RO | |
| | D4 (GPIO2) | MAX485 DE/RE | |
| **Arduino Nano** | D10 | Kapı 1 (Input) | |
| | D11 | Kapı 2 (Input) | |
| | D9 | Röle (Output/NC) | Alarm |
| | D0 | ESP8266 RX | Seri |
| | D1 | ESP8266 TX | |

### 2️⃣ **Yazılım Kurulumu**

#### ESP32-S3 Master

```bash
cd esp32_master
platformio run -e esp32s3 --target upload
# Veya VS Code içinde: Ctrl+Alt+U
```

**Bağlantı:**
- COM: ESP32 USB
- Hız: 460800 baud

**Ağ:**
- IP: 192.168.4.x (DHCP)
- Web: http://192.168.4.x
- MQTT: localhost:1883

#### ESP8266 Slave

```bash
cd esp8266_slave
platformio run -e esp8266-12e --target upload
```

**Bağlantı:**
- COM: CH340 (USB-Serial) COM4
- Hız: 460800 baud
- Web: http://192.168.4.x:8080 (lokal status)

#### Arduino Nano

Arduino IDE ile yükleyin:
```
Dosya → Örnekler → SuDepoSis_v2 → nano_io → main.ino
```

**Ayarlar:**
- Board: Arduino Nano
- Processor: ATmega328P
- COM: Nano's COM
- Hız: 115200 baud

---

## 🔌 RS485 Haberleşmesi

### Protokol

**Baudrate:** 115200
**Update:** 500ms (2 Hz)
**Format:** Metin CSV

#### ESP8266 → ESP32-S3

```
ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day\n
```

#### Arduino Nano → ESP8266 (SoftwareSerial 9600)

```
Komut: GET_STATUS
Yanıt: D0=0,D1=1,RELE=0\n
```

### Test Etme

ESP32-S3 Serial Monitor (115200):
```
[RS485] Initialized
[RS485 TX] MASTER:REQUEST_ESP8266
[RS485 RX] ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day
```

---

## 🌐 Web API

### ESP32-S3 Dashboard

**URL:** `http://[ESP32_IP]:80`

**Canlı Durum:** Her 5 saniyede güncellenir

### JSON API

```bash
# Sistem durumu
curl http://[ESP32_IP]/api/status
```

**Yanıt:**
```json
{
  "level_cm": 45.2,
  "level_percent": 72.0,
  "level_liters": 2150.0,
  "temperature": 26.0,
  "night_mode": false,
  "nano": {
    "door1": false,
    "door2": false,
    "relay": false
  },
  "alarm": {
    "leak": false,
    "low_level": false,
    "door": false
  }
}
```

---

## 📡 MQTT Yayıncılık

### Broker Konfigürasyonu

**Lokal Mosquitto:**
```bash
# Windows: Mosquitto'yu kurun
# Topic: sudeposis/level
# QoS: 1
```

### Mesaj Formatı

```json
{
  "timestamp": 1234567890,
  "level_cm": 45.2,
  "level_percent": 72.0,
  "level_liters": 2150.0,
  "temperature": 26.0,
  "night_mode": false,
  "nano": {
    "door1": false,
    "door2": true,
    "relay": true
  },
  "alarm": {
    "leak": false,
    "low_level": false,
    "door": true
  }
}
```

---

## 🔋 Sensör Kalibrasyonu

### HC-SR04 Mesafe

Ölçüm: 5 ortalaması (gürültü filtresi)
Aralık: 2-400 cm

### Depo Özellikleri

Dosya: `esp32_master/include/config.h`

```cpp
#define TANK_EMPTY_CM 190.0      // Boş durumda mesafe
#define TANK_FULL_CM 15.0        // Dolu durumda mesafe
#define TANK_CAPACITY_LITERS 3000.0
#define TANK_HORIZONTAL 1        // 1 = Yatay silindir
```

**Kalibre Etme:**
1. Depo boş → `TANK_EMPTY_CM` ölçüm
2. Depo dolu → `TANK_FULL_CM` ölçüm
3. Kod güncelle ve yükle

---

## ⚠️ Alarm Sistemi

### Seviyeler

- **Düşük Seviye:** `< ALARM_LEVEL_PERCENT` (15%)
- **Su Kaçağı:** 60+ dakika kesintisiz düşüş
- **Kapı Açık:** D0 veya D1 HIGH
- **Röle Tetiklemesi:** Kapı açık → NC röle aktif

### Röle (NC - Normally Closed)

```
HIGH (Devre açık) → Pasif
LOW (Devre kapalı) → Tetiklenmiş
```

**Arduino Nano:**
```cpp
if (door1_open || door2_open) {
  digitalWrite(RELAY_PIN, LOW);   // Tetikle
} else {
  digitalWrite(RELAY_PIN, HIGH);  // Kapat
}
```

---

## 📊 Veri Kayıtları

SPIFFS (ESP32-S3):
- `/kayitlar.csv` - Su dolum geçmişi
- `/monthly_archive.csv` - Aylık arşiv
- `/alarm_log.csv` - Alarm logları

---

## 🔧 Sorun Giderme

### ESP8266 Nano ile iletişim yok

1. SoftwareSerial pinlerini kontrol et (D9, D10)
2. 9600 baud hızını doğrula
3. Nano şeritleme konektörünü kontrol et

```cpp
// Test
swSerial.println("PING");
// Beklenen yanıt: "PONG"
```

### RS485 Mesaj alınmıyor

1. DE/RE pini kontrol et (GPIO2)
2. CAT5 kablonun sıkı bağlı olduğundan emin ol
3. 115200 baudrate doğru mu?

```bash
# Serial monitörde kontrol et:
[RS485] Initialized
[RS485 TX] Master request...
[RS485 RX] Response received
```

### Web sayfası açılmıyor

1. ESP32-S3 IP adresini bul: `Serial monitor → "IP:"`
2. Aynı ağda mısın?
3. Firewall kontrol et

---

## 📝 Geliştirme Notları

- **Veri güncelleme:** 500ms (2 Hz) - kasma yok
- **Web refresh:** 5 saniyede bir
- **OTA güncellemesi:** WiFi üzerinden (planlanıyor)
- **Aylık rapor:** CSV export

---

## 📞 Destek & Hata Raporları

Serial Monitor (115200):
```
[SYSTEM] Setup complete!
[RS485] Initialized
[WEB] Server started on port 80
[MQTT] Connected!
```

---

**SuDepoSis v2.0** © 2026
