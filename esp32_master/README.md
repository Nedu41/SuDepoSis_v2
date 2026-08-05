# esp32_master

Sistemin "beyni" — ESP32-S3, WiFi'ye bağlanır, web arayüzünü sunar, MQTT'ye yayın yapar, OTA
güncellemeyi destekler ve RS485 üzerinden `esp8266_slave` kartını sorgular/yönetir. Genel mimari
için [../README.md](../README.md) içine bak.

## Kurulum

```bash
cp include/secrets.h.example include/secrets.h
# secrets.h icine gercek WIFI_SSID / WIFI_PASSWORD / OTA_PASSWORD / AP_PASSWORD gir
pio run -t upload
```

- Board: ESP32-S3, `platform = espressif32`
- Varsayılan AP: SSID/şifre `secrets.h`'daki `AP_PASSWORD` (AP adı `kalburum`, bkz. `include/config.h`)
- mDNS: `kalburum.local`
- Web portu: 80

## RS485 (Master)

| Ayar | Değer |
|---|---|
| Baudrate | 9600 |
| RX | GPIO16 (UART1) |
| TX | GPIO17 (UART1) |
| DE/RE | GPIO2 |
| Poll aralığı | 600ms (`RS485_UPDATE_INTERVAL`) |

Slave'e komut göndermek `MASTER:<KOMUT>` formatında olur, örn: `MASTER:REQUEST_ESP8266`,
`MASTER:SET_RAIN_SKIP=1`, `MASTER:PANIC`. Tüm komut listesi `esp8266_slave/src/main.cpp`
içindeki `rs485KomutDinle()` fonksiyonunda.

## Web Uç Noktaları

| Endpoint | İş |
|---|---|
| `/` | Ana dashboard (koda gömülü HTML) |
| `/api/status` | JSON durum |
| `/events` | Server-Sent Events (canlı güncelleme) |
| `/api/ota?url=` | URL'den firmware güncelleme |
| `/update` (POST, multipart) | Dosyadan firmware güncelleme (.bin yükleme) |
| `/api/lamba`, `/api/moisture`, `/api/moisture/auto`, `/api/moisture/threshold` | Depo lambası / nem kontrolü |
| `/api/alarm`, `/api/alarm/mod`, `/api/alarm/mute`, `/api/alarm/onayla` | Alarm yönetimi |
| `/api/panic` | Panik butonu |
| `/api/kapi` | Kapı/röle testi |
| `/api/wifi`, `/api/wifi/scan` | STA WiFi ayarı |
| `/api/restart` | Kartı yeniden başlat |

## MQTT

Yerel bir Mosquitto broker'a (`secrets.h` dışında, `include/config.h`'daki `MQTT_BROKER`/`MQTT_PORT`,
varsayılan `127.0.0.1:1883`) bağlanır ve `sudeposis/level`, `sudeposis/esp8266/status`,
`sudeposis/nano/status`, `sudeposis/alarm` topic'lerine yayın yapar; `sudeposis/command` topic'ini dinler.
Kimlik doğrulama gerekiyorsa `secrets.h`'daki `MQTT_USER`/`MQTT_PASS` kullanılır.

## Hava Durumu / Yağmur Tahmini

Bahçede sabit internet olmadığından ([../README.md](../README.md#gece-enerji-politikası)), bu kart
kullanıcı bahçedeyken mobil hat/hotspot'a bağlandığında hava durumunu sorgular ve yarın yağmur
bekleniyorsa `esp8266_slave`'e RS485 üzerinden `MASTER:SET_RAIN_SKIP=1` gönderir (aksi halde `=0`).
Konum seçimi (il/ilçe) bu kartın kendi web sayfasından yapılır; ağır iş (HTTPS sorgu, konum çözümleme)
bilinçli olarak burada tutulur, `esp8266_slave` hafif kalsın diye (bkz. proje hafızası "keep ESP8266 light").

## Pin Özeti

| GPIO | İşlev |
|---|---|
| 16 | RS485 RX (UART1) |
| 17 | RS485 TX (UART1) |
| 2 | RS485 DE/RE |
