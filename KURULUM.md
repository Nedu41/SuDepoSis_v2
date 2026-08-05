# Kurulum Kılavuzu - SuDepoSis v2

## 🎯 Hızlı Başlangıç

### Adım 1: PlatformIO Kurulumu

VS Code'da:
1. Extensions (Ctrl+Shift+X)
2. "PlatformIO IDE" ara ve kur
3. VS Code'u yeniden başlat

### Adım 2: Proje Klonum

```bash
# Veya proje dosyalarını kopyala
cd d:\Kodlar\SuDepoSis_v2
```

### Adım 3: Cihazları Hazırlama

#### ESP32-S3

1. USB-C kabloyla bağla
2. Device Manager'da COM portunu kontrol et
3. PlatformIO: esp32_master seç
4. Upload: `PIO Upload` (Ctrl+Alt+U)

#### ESP8266

1. Micro USB kabloyla bağla
2. COM portunu not et
3. PlatformIO: esp8266_slave seç
4. Upload

#### Arduino Nano

1. Arduino IDE kur: https://www.arduino.cc/en/software
2. Nano main.ino açıp Yükle (Ctrl+U)

### Adım 4: Seri Monitor Test

**ESP32-S3:**
```
[SYSTEM] Setup complete!
[RS485] Initialized
[WEB] Server started on port 80
```

**ESP8266:**
```
[SYSTEM] Setup complete!
[RS485] Initialized
[WEB] Server started on port 8080
```

**Arduino Nano:**
```
[NANO] Initialized
```

### Adım 5: Web Paneli Erişim

Browser açıp:
```
http://192.168.4.x
```

---

## 🔌 RS485 Haberleşme Testi

### Test 1: Master Komut Gönder

ESP32-S3 Serial Monitor'da:
```
[RS485 TX] MASTER:REQUEST_ESP8266
```

ESP8266'den yanıt görmelisin:
```
[RS485 RX] ESP8266:LEVEL=45.2,TEMP=26.0,MODE=day
```

### Test 2: Nano Komut

ESP8266 Serial Monitor'da:
```
[Nano] Response: D0=0,D1=1,RELE=0
```

---

## 🌐 MQTT Test

### Mosquitto Kurulum (Windows)

1. Download: https://mosquitto.org/download/
2. Install
3. Services'te başlat: `mosquitto` service

### MQTT Subscribe

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t "sudeposis/#"
```

### Mesaj Kontrol

ESP32-S3'ün her 5 saniyede mesaj göndermesi görmelisin.

---

## 📊 Pin Konfigürasyonu (Hatırlatıcı)

### ESP32-S3
- GPIO16: RS485 RX
- GPIO17: RS485 TX
- GPIO2: RS485 DE/RE

### ESP8266
- D5 (GPIO14): HC-SR04 TRIG
- D6 (GPIO12): HC-SR04 ECHO
- D0 (GPIO16): RS485 TX
- D7 (GPIO13): RS485 RX
- D4 (GPIO2): RS485 DE/RE
- D9 (GPIO3): Nano TX
- D10 (GPIO1): Nano RX

### Arduino Nano
- D10: Kapı 1 (Input, PULLUP)
- D11: Kapı 2 (Input, PULLUP)
- D9: Röle (Output, NC)
- D0: ESP8266 RX
- D1: ESP8266 TX

---

## 🚨 Olası Hatalar & Çözümleri

### "Serial port not found"
→ USB kablonun doğru bağlı olduğundan emin ol
→ Device Manager'dan COM portunu kontrol et

### "RS485 mesaj alamıyor"
→ Kablolama kontrol et (A+ ve B-)
→ Baudrate 115200 mı?
→ DE/RE pini kontrol et

### "Web sayfası açılmıyor"
→ ESP32-S3 IP adresini kontrol et
→ Firewall disable et (test için)

### "Nano yanıt vermiyor"
→ SoftwareSerial pinleri doğru mu?
→ 9600 baud hızı doğru mu?

---

## ✅ Başarılı Kurulum Belirtileri

- [ ] ESP32-S3 Serial Monitor'da "OK"
- [ ] ESP8266 Serial Monitor'da "OK"
- [ ] Arduino Nano upload başarılı
- [ ] RS485 mesajlar görülüyor
- [ ] Web dashboard açılıyor
- [ ] Kapı sensörleri yanıt veriyor
- [ ] Röle tetikleniyor

---

**Hazırsan başla!** 🚀
