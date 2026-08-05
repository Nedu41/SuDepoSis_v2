# 🔧 Debug & Test Kılavuzu - SuDepoSis v2

## 🧪 Test Prosedürü

### Aşama 1: Donanım Bağlantıları Kontrol

**Kontrol Listesi:**

- [ ] ESP32-S3 USB'ye bağlı
- [ ] ESP8266 USB'ye bağlı  
- [ ] Arduino Nano USB'ye bağlı
- [ ] RS485 kablolama (A↔A, B↔B)
- [ ] SoftwareSerial bağlantıları (RX/TX)
- [ ] Güç kaynakları bağlı
- [ ] Tüm GND'ler ortak

---

### Aşama 2: Arduino Nano Testi

**1. Yükle:**
```
Arduino IDE → Ctrl+U
Board: Arduino Nano
Processor: ATmega328P
```

**2. Serial Monitor (115200):**
```
[NANO] Initialized
[NANO] Door1(D10), Door2(D11), Relay(D9)
```

**3. Kapı Test:**
- Kapı sensörü kapalı: "✓"
- Kapı sensörü açık: Röle LED yanacak

**4. Röle Test (manual):**
```
Serial Monitor'a yaz:
RELAY_ON
(LED yanmalı)

RELAY_OFF  
(LED sönmeli)
```

---

### Aşama 3: ESP8266 Testi

**1. Yükle:**
```
PlatformIO → esp8266_slave → Upload
```

**2. Serial Monitor (115200):**
```
[SYSTEM] Setup complete!
[RS485] Initialized
[WEB] Server started on port 8080
```

**3. HC-SR04 Test:**
```
[HCSR04] Measuring...
Distance: 45.2 cm
```

**4. Nano Haberleşme:**
```
[Nano] Response: D0=0,D1=1,RELE=0
```

**5. Web Local (tarayıcı):**
```
http://192.168.4.x:8080
```

---

### Aşama 4: ESP32-S3 Testi

**1. Yükle:**
```
PlatformIO → esp32s3 → Upload
```

**2. Serial Monitor (115200):**
```
========================================
🟢 SuDepoSis v2 - ESP32-S3 Master
========================================
[RS485] Initializing UART1...
[RS485] UART1 ready at 115200 baud
[WiFi] Connected! IP: 192.168.4.x
[WEB] Server started on port 80
[MQTT] Broker: 127.0.0.1
```

**3. RS485 Polling:**
```
[RS485 TX] MASTER:REQUEST_ESP8266
[RS485 RX] ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day
```

**4. Web Dashboard:**
```
http://192.168.4.x
```

Görmeli:
- Depo seviyesi (%)
- Mesafe (cm)
- Litre
- Kapı durumları
- Röle durumu

---

## 🐛 Hata Giderme

### Problem: "ESP8266 yanıt vermiyor"

**Kontrol Listesi:**

1. RS485 kablolama
```
ESP32 GPIO17 (TX) → MAX485 DI
ESP32 GPIO16 (RX) → MAX485 RO
ESP8266 D0 (TX) → MAX485 DI
ESP8266 D7 (RX) → MAX485 RO
A ← → A (MAX485'ler arası)
B ← → B (MAX485'ler arası)
```

2. DE/RE pini kontrol
```cpp
// ESP32
digitalWrite(RS485_DE_PIN, HIGH);  // TX
delay(100);
// Mesaj gönder
Serial1.print("...");
delay(100);
digitalWrite(RS485_DE_PIN, LOW);   // RX
```

3. Baudrate (115200)
```
❌ Yanlış: 9600
✅ Doğru: 115200
```

4. Serial Monitor logları
```
[RS485 TX] ...
[RS485 RX] ...
```

---

### Problem: "Nano yanıt vermiyor"

**Kontrol Listesi:**

1. SoftwareSerial pinleri (ESP8266)
```
D9 (GPIO3) → Nano TX → D1
D10 (GPIO1) → Nano RX → D0
9600 baud
```

2. Test komutu
```
ESP8266 Serial Monitor'a:
[Nano] Response: ...

Görünmüyor mu?
- Pin bağlantılarını kontrol et
- Nano reset butonu bas
```

3. Direnç kontrol
```
ESP8266 D10 → 470Ω → Nano D0
ESP8266 D9 → 470Ω → Nano D1
GND → 1K → Nano D0 (level shifter)
```

---

### Problem: "HC-SR04 mesafe yanlış"

**Kontrol Listesi:**

1. Pin bağlantıları
```
D5 (GPIO14) → TRIG
D6 (GPIO12) → ECHO
```

2. Kalibrasyonu kontrol et
```cpp
#define TANK_EMPTY_CM 190.0
#define TANK_FULL_CM 15.0

Ölç: Empty mesafe → Empty cm
Ölç: Full mesafe → Full cm
Güncelleştir ve yükle
```

3. Gürültü (noise)
```
5 ortalaması kullanıyoruz
Tek ölçüm hata gösterebilir
```

---

### Problem: "Web dashboard açılmıyor"

**Kontrol Listesi:**

1. IP adresi
```
ESP32-S3 Serial Monitor'dan oku:
[WiFi] Connected! IP: 192.168.4.x
```

2. Ağ bağlantısı
```
Aynı WiFi ağında mısın?
Ping testi: ping 192.168.4.x
```

3. Firewall
```
Test amaçlı: Windows Defender → Firewall OFF
Sonra açılırsa, kuralı ekle
```

4. Port
```
HTTP: 80
HTTPS: 443 (kullanmıyoruz)

Başka servis port 80'i işgal ediyor mu?
netstat -an | findstr :80
```

---

## 🧬 Debug Mesajları

### Seri Port İçişleri Takip

**ESP32-S3 - Main Loop:**
```cpp
[RS485 TX] MASTER:REQUEST_ESP8266
[RS485 RX] ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day
[Sensor Data Updated]
[MQTT] Published to sudeposis/level
```

**ESP8266 - Main Loop:**
```cpp
[HC-SR04] Sampling 5 times...
[Nano] Response: D0=0,D1=1,RELE=0
[RS485 TX] ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day
```

**Arduino Nano:**
```cpp
[Nano] Initialized
[IO] Door1 changed: OPEN
[RELAY] ACTIVATED
[CMD] Received: GET_STATUS
D0=1,D1=0,RELE=1
```

---

## 📊 Canlı Monitör

### MQTT Kontrol (Mosquitto)

```bash
# Terminal açıp:
mosquitto_sub -h 127.0.0.1 -p 1883 -t "sudeposis/#" -v
```

Bekle (5 saniye):
```
sudeposis/level {"level_cm":45.2,"level_percent":72.0,...}
```

---

## 🔌 Bağlantı Test Script

### Python Test (Optional)

```python
import serial
import json

# ESP32-S3
esp32 = serial.Serial('COM3', 115200, timeout=1)

# RS485 testi
print("[TEST] Sending RS485 request...")
esp32.write(b"TEST\n")

# Yanıt oku
response = esp32.readline().decode()
print(f"[TEST] Response: {response}")

esp32.close()
```

---

## ✅ Başarılı Test Belirtileri

| Test | Status | Sonuç |
|------|--------|-------|
| Arduino Nano Boot | ✓ | Serial mesajı görülüyor |
| Nano Kapı Testi | ✓ | Kapı açınca röle ON |
| ESP8266 Boot | ✓ | RS485 hazır |
| ESP8266 HC-SR04 | ✓ | Mesafe 0-400 cm |
| ESP8266 Nano Haberleşme | ✓ | D0/D1/RELE durumu alınıyor |
| ESP32-S3 Boot | ✓ | WiFi + MQTT bağlı |
| ESP32-S3 RS485 | ✓ | Master request → Slave response |
| Web Dashboard | ✓ | Canlı veri görülüyor |
| MQTT Publish | ✓ | JSON mesaj alınıyor |
| Alarm Tetikleme | ✓ | Kapı açınca röle tetikleniyor |

---

## 🚀 Sistem Tamam Signal

```
🟢 Tüm testler PASS
🟢 Web dashboard yanıt veriyor
🟢 MQTT mesajlar geliyor
🟢 Alarm sistemi çalışıyor
🟢 Veri merkezileştirildi

→ SİSTEM BAŞLAMAYA HAZIR ✓
```

---

**Debug Kılavuz Tamamlandı!** 🎯

