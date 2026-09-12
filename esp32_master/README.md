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
| RX | GPIO37 (UART1) |
| TX | GPIO38 (UART1) |
| DE/RE | GPIO39 |
| Poll aralığı | 600ms (`RS485_UPDATE_INTERVAL`) |

Slave'e komut göndermek `MASTER:<KOMUT>` formatında olur, örn: `MASTER:SET_LAMBA=1`,
`MASTER:SET_RAIN_SKIP=1`, `MASTER:PANIC=1`. Tüm komut listesi `esp8266_slave/src/main.cpp`
içindeki `rs485KomutDinle()` fonksiyonunda. Gerçek RS485 erişimi bir FreeRTOS mutex (`RS485Kilit`)
ile korunur — BLE görevi ile `loop()` (web/poll) aynı anda hatta asla dokunmaz.

## Konteyner Zonu (Kalburum'un Kendi Alarm/Aydınlatma Donanımı)

ESP32-S3'ün bulunduğu konteynerdeki alarm donanımı — RS485/ESP8266/Nano'dan tamamen bağımsız,
doğrudan ESP32'ye bağlı. Kendi PIR'ı (HC-SR505), kapı reed switch'i, IR kumanda alıcısı, sireni ve
lambası var; "Sudepo Zonu"ndaki (depo tarafı) alarm ile aynı Alarm Modu numarasını (Sesli/Sessiz/
Onaylı) referans alır ama `konteynerAlarmEtkin` ile bağımsız açılıp kapatılabilir. Panik, moddan
bağımsız her iki zonu da anında tetikler. Pin detayları için [docs/pinout.html](docs/pinout.html).

## Bahçe Kapısı (Araç Girişi)

Motor/röle mantığı (R413D08 üzerinden) `esp8266_slave` tarafında; bu kart sadece fiziksel butonu
okur ve RS485 komutu iletir. Fiziksel buton GPIO47 (`BAHCE_KAPI_BUTON_PIN`, INPUT_PULLUP,
aktif-LOW): tek basış Kapı 1'i (sol kanat) açar, `BAHCE_KAPI_CIFT_BASIS_PENCERE_MS` içinde çift
basış iki kanadı da açar; hareket halindeyken durdurur, ikisi de açıkken kapatır (bkz
`src/bahce_kapisi.cpp`). Web API: `/api/bahce_kapi?durum=ac|kapat|dur`. Gerçek kapı durumu RS485
`GET_STATUS` yanıtındaki `BAHCE1`/`BAHCE2` alanından okunur, burada varsayımsal atama yapılmaz.

## BLE (Telefon Uygulaması)

NimBLE üzerinden bir GATT servisi yayınlanır (`BLE_SERVICE_UUID`/`BLE_CHARACTERISTIC_UUID`,
`include/config.h`) — eşleşmiş Android uygulaması (ayrı repo, `BLEDProject`) WiFi ağına hiç girmeden
lamba/alarm/kapı/panik komutlarını gönderip anlık durumu okuyabilir (2sn'de bir notify).

## Telegram Bildirimleri

`secrets.h` içindeki `TELEGRAM_BOT_TOKEN`/`TELEGRAM_CHAT_ID` tanımlıysa, alarm tetiklendiğinde
Telegram'a mesaj gönderilir (internet yoksa 2 dakika boyunca tekrar denenir). Web arayüzünden
açık/kapalı anahtarlanabilir (`/api/telegram/ayar`).

## Web Uç Noktaları

| Endpoint | İş |
|---|---|
| `/` | Ana dashboard (koda gömülü HTML) |
| `/api/status` | JSON durum |
| `/events` | Server-Sent Events (canlı güncelleme) |
| `/api/ota?url=` | URL'den firmware güncelleme |
| `/update` (POST, multipart) | Dosyadan firmware güncelleme (.bin yükleme) |
| `/api/lamba`, `/api/moisture`, `/api/moisture/auto`, `/api/moisture/threshold` | Sudepo Zonu lambası / nem kontrolü |
| `/api/konteyner/lamba`, `/api/konteyner/alarm`, `/api/konteyner/pir_ayar`, `/api/konteyner/swan_ayar` | Konteyner Zonu lambası / alarm anahtarı / PIR/Swan PIR ayarı |
| `/api/konteyner/sensor_aktif`, `/api/konteyner/mq6_test`, `/api/konteyner/mq6_manuel`, `/api/konteyner/gaz_ayar`, `/api/konteyner/duman_ayar` | MQ6 (gaz) / GP2Y10 (duman) sensör etkin anahtarı, kalibrasyon, eşik ayarı |
| `/api/konteyner/siren_ayar`, `/api/konteyner/siren_ayar/kaydet`, `/api/konteyner/mod_senaryo/kaydet` | Konteyner siren zamanlaması / Mod Senaryoları |
| `/api/bahce_kapi?durum=ac\|kapat\|dur` | Bahçe kapısı (araç girişi) kontrolü |
| `/api/batarya/ayar`, `/api/ana-guc-esik`, `/api/adaptor-esik` | Yedek akü / ana güç erken-uyarı / laptop adaptörü kesme eşikleri |
| `/api/acil-lamba` | Acil durum lambası (GPIO12) kontrolü |
| `/api/alarm`, `/api/alarm/mod`, `/api/alarm/mute`, `/api/alarm/onayla`, `/api/alarm/onayla_lamba`, `/api/alarm/log`, `/api/alarm/log/tam` | Alarm yönetimi (Sudepo) ve alarm logu |
| `/api/panic` | Panik butonu (her iki zonu da tetikler) |
| `/api/kapi` | Kapı/röle testi |
| `/api/ir/liste`, `/api/ir/ogren_baslat`, `/api/ir/ogren_durum`, `/api/ir/kaydet`, `/api/ir/sil` | Konteyner IR kumanda öğrenme/eşleme |
| `/api/weather`, `/api/weather/check` | Hava durumu / yağmur tahmini |
| `/api/telegram/test`, `/api/telegram/ayar` | Telegram bildirimleri |
| `/api/wifi`, `/api/wifi/scan`, `/api/wifi/gecmis`, `/api/wifi/gecmis_bagla`, `/api/wifi/gecmis_sil` | STA WiFi ayarı / geçmiş ağlar |
| `/api/sudepo_ayarlar`, `/api/sudepo_ayarlar/kaydet` | Sudepo ayarlarına (ESP8266) köprü |
| `/api/kayit/yedekle`, `/api/kayit/geri_yukle`, `/api/kayit/yedek_durum`, `/api/kayit/liste_goster` | Dolum kayıtları (kayitlar.csv) yedekleme |
| `/api/ayarlar/yedekle`, `/api/ayarlar/geri_yukle_cihaz`, `/api/ayarlar/geri_yukle_dosya`, `/api/ayarlar/fabrika` | Tüm ayarları yedekleme/geri yükleme/fabrika ayarları |
| `/firmware/upload`, `/firmware/esp8266.bin`, `/api/firmware/durum` | ESP8266 firmware'ini yerel depoda barındırma |
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
| 37 | RS485 RX (UART1) |
| 38 | RS485 TX (UART1) |
| 39 | RS485 DE/RE |
| 40, 41 | MPPT RS232 (UART2, TX/RX) |
| 4 | Konteyner IR alıcı |
| 5 | Konteyner LED + buzzer (yerel uyarı) |
| 17 | Konteyner PIR2 (HC-SR505) |
| 13 | Swan Quad PET PIR (NC/COM) |
| 7 | Konteyner kapı reed switch |
| 8 | Konteyner siren rölesi |
| 9 | Konteyner lamba rölesi |
| 15 | Fiziksel Acil Durum Butonu |
| 47 | Bahçe Kapısı Butonu |
| 35 | Bahçe Kapısı Zil Hoparlörü (pasif, henüz bağlanmadı) |
| 12 | Acil Durum Lambası (MOSFET) |
| 21 | Laptop Adaptörü Kesme MOSFET |
| 2 | Yedek Akü ADC (ADC1) |
| 10 | MQ6 (gaz) analog giriş (ADC1) |
| 16 | MQ6 güç kontrolü (IRF520 SIG) |
| 6 | GP2Y10 (duman/toz) analog giriş (ADC1) |
| 18 | GP2Y10 LED kontrol (doğrudan Pin3) |
| 36, 42 | AHT10 SDA/SCL (I2C) |

Tam pinout (kablaj, modül notları) için [docs/pinout.html](docs/pinout.html); MPPT şarj kontrolcü
bağlantısı için adım adım görsel kılavuz: [docs/mppt-baglanti-kilavuzu.html](docs/mppt-baglanti-kilavuzu.html);
yedek akü (3x12V paralel, şarj+deşarj devresi) bağlantısı için: [docs/yedek-aku-baglanti-kilavuzu.html](docs/yedek-aku-baglanti-kilavuzu.html);
kutunun (Dream DM500S gövdesi) arka panel bağlantıları (RJ45/DC güç/SCART/RCA/DB9) için:
[docs/kutu-arka-panel.html](docs/kutu-arka-panel.html).
