# 🏗️ SuDepoSis v2 — Sistem Mimarisi (Teknik Referans)

> Bu dosya, projenin **güncel** teknik mimarisini anlatır. Fiziksel pin şeması için her kartın
> `docs/pinout.html` dosyasına bak: [esp32_master/docs/pinout.html](esp32_master/docs/pinout.html),
> [esp8266_slave/docs/pinout.html](esp8266_slave/docs/pinout.html). Kurulum adımları için kök
> [README.md](README.md) yeterli — burası "nasıl çalışıyor" sorusuna cevap verir.

## 📊 Genel Yapı

Sistem iki bağımsız "zon"dan oluşur, ikisi de aynı ESP32-S3 üzerinden yönetilir:

- **Sudepo Zonu** — depo tarafı: su seviyesi, 2 kapı, alarm rölesi, nem/sulama rölesi, depo lambası.
  Donanımı ESP8266 + Arduino Nano üzerinde, ESP32'ye RS485 ile bağlı.
- **Konteyner Zonu ("Kalburum")** — ESP32-S3'ün bulunduğu konteynerdeki alarm/aydınlatma donanımı:
  IR kumanda alıcısı, PIR hareket sensörü, kapı reed switch, siren, lamba. Tamamen **ESP32'ye doğrudan
  bağlı**, RS485/ESP8266/Nano'dan bağımsız çalışır.

```
                    RS485 (9600 baud, MAX485, CAT5)
┌────────────────────────┐  ◄──────────────────►  ┌──────────────────────┐
│   ESP32-S3 "Kalburum"   │                        │   ESP8266 (12E)       │
│   RS485 Master           │                        │   RS485 Slave         │
│                          │                        │                       │
│ ✓ WiFi (AP+STA) / mDNS   │                        │ ✓ HC-SR04 seviye      │
│ ✓ Web Dashboard (:80)    │                        │ ✓ DS1307 RTC          │
│ ✓ MQTT Publisher         │                        │ ✓ Alarm/panik yönetimi│
│ ✓ BLE (telefon uygulaması)│                       │ ✓ Nem otomasyonu      │
│ ✓ Telegram bildirimleri  │                        │ ✓ Web paneli (:80, AP)│
│ ✓ Hava durumu/yağmur     │                        │ ✓ EEPROM ayarlar      │
│   tahmini sorgusu        │                        └──────────┬────────────┘
│ ✓ OTA (URL/dosya/Arduino)│                                    │ UART0 (9600 baud,
│ ✓ SPIFFS (kayıt/log CSV) │                                    │ 470Ω seri + 1kΩ pull-down)
│                          │                        ┌───────────▼────────────┐
│ ── Konteyner Zonu ──     │                        │  Arduino Nano           │
│ ✓ IR alıcı + kumanda     │                        │  (ATmega328P, WiFi yok) │
│ ✓ PIR2 + kapı reed       │                        │ ✓ 2x kapı sensörü       │
│ ✓ Siren + lamba rölesi   │                        │ ✓ Alarm rölesi (NC)     │
│ ✓ LED+buzzer (yerel uyarı)│                       │ ✓ Nem/sulama rölesi     │
└─────────────────────────┘                        │ ✓ PIR (Sudepo)          │
                                                     │ ✓ Depo lambası rölesi   │
                                                     │ ✓ Toprak nem (analog)   │
                                                     └─────────────────────────┘
```

Üç kart da tek repoda ayrı PlatformIO projeleri: her birinin kendi `platformio.ini`, `include/`,
`src/` klasörü var.

## 🔄 Veri Akışı

1. **ESP32 → ESP8266 (poll):** ESP32, `rs485_poll()` içinde her `RS485_UPDATE_INTERVAL` (600ms)'de
   bir `GET_STATUS` gönderir; gerçek RS485 erişimi bir FreeRTOS mutex (`RS485Kilit`) ile korunur ki
   BLE görevi (ayrı bir "nimble_host" task'ı) ile `loop()` aynı anda hatta dokunmasın.
2. **ESP8266 yanıtı:** `ESP8266:LEVEL=..,PCT=..,...` formatında tek satır — depo seviyesi, sıcaklık,
   kapı/röle/lamba/nem durumu, alarm modu/mute/pending, panik, tetikleyici bitmask'i hepsi bu satırda.
   Tam alan listesi: [esp8266_slave/README.md](esp8266_slave/README.md#rs485-durum-alanları).
3. **ESP32 parse + karar:** `parse_esp8266_data()` alanları kendi state'ine yazar; alarm/seviye
   hesaplamaları SADECE veri taze ise (`esp8266_online`, 10sn eşik) güncellenir — bağlantı koparsa
   eski değerler donmuş görünmez, `--`/pasif gösterilir.
4. **Web/BLE/MQTT/SSE yayını:** Aynı state, `/api/status` (JSON), `/events` (SSE, ~1sn'de bir push),
   BLE notify (2sn'de bir) ve MQTT (`sudeposis/*` topic'leri) üzerinden eş zamanlı sunulur — "server
   truth" modeli: hiçbir istemci (web/telefon) local state tutmaz, hep sunucudan gelen taze veriyi çizer.
5. **Buton komutları:** Web/BLE'den gelen bir SET komutu (`MASTER:SET_LAMBA=1` gibi) da aynı mutex
   üzerinden gönderilir, ACK bekler (max 3 deneme, 1000ms timeout) — komutlar idempotent (PANIC/
   ALARM_MUTE dahil, toggle değil explicit-set), bu yüzden art arda hızlı tıklamak/retry zararsız.
6. **Konteyner Zonu:** RS485'ten tamamen bağımsız, `loop()` içinde doğrudan okunur/yazılır — PIR/kapı
   okuma, IR kumanda işleme, siren/lamba kararı hep aynı döngüde, RS485 gecikmesi hiç etkilemez.

## 📡 İletişim Protokolleri

### RS485 (ESP32 ↔ ESP8266)

- **Hız:** 9600 baud, metin tabanlı, satır sonu `\n`.
- **Master → Slave:** `MASTER:<KOMUT>\n`, örn. `MASTER:SET_LAMBA=1`, `MASTER:SET_ALARM_MOD=3`,
  `MASTER:PANIC=1`, `MASTER:ALARM_MUTE=0`, `MASTER:SET_RAIN_SKIP=1`. Slave `ACK:<komut>` veya
  `NACK:<komut>` ile yanıtlar.
- **Slave → Master (periyodik durum):** `ESP8266:LEVEL=..,PCT=..,...\n` — ~1sn'de bir veya
  `GET_STATUS` isteğine yanıt olarak.
- Fiziksel katman: iki MAX485 modülü, CAT5 üzerinden A↔A/B↔B twisted pair. Pin detayları için
  her kartın `docs/pinout.html`'ine bak.

### UART0 (ESP8266 ↔ Nano)

- 9600 baud, HardwareSerial (ESP8266'nın USB/programlama UART0'ı ile aynı pinler — bu yüzden Nano
  bağlıyken ESP8266'yı flaşlamak sorunlu olabilir, gerekirse geçici olarak ayırın).
- Komut listesi: [nano_io/README.md](nano_io/README.md#komut-protokolü-esp8266--nano-9600-baud).

### BLE (Telefon Uygulaması — BLEDProject/Android)

- ESP32-S3, NimBLE ile bir GATT servisi yayınlar (`BLE_SERVICE_UUID`/`BLE_CHARACTERISTIC_UUID`,
  bkz `config.h`); telefon WiFi ağına hiç girmeden doğrudan lamba/alarm/kapı/panik komutlarını
  gönderip anlık durumu okuyabilir. 2sn'de bir notify ile durum push edilir.
- Android tarafı ayrı bir repo: `d:\Kodlar\Android\BLEDProject` (Jetpack Compose, foreground service
  ile ekran kapalıyken de bağlantı/alarm sesi/bildirim sürdürülür).

### MQTT (ESP32-S3 → Broker)

- Yerel Mosquitto (`127.0.0.1:1883`, `config.h` → `MQTT_BROKER`/`MQTT_PORT`).
- Yayın: `sudeposis/level`, `sudeposis/esp8266/status`, `sudeposis/nano/status`, `sudeposis/alarm`.
- Dinleme: `sudeposis/command`.

### Telegram (Alarm Bildirimleri)

- `TELEGRAM_BOT_TOKEN`/`TELEGRAM_CHAT_ID` (`secrets.h`) tanımlıysa, alarm tetiklendiğinde Telegram'a
  mesaj gönderilir. İnternet o an yoksa `TELEGRAM_RETRY_SURESI_MS` (2 dk) boyunca her döngüde tekrar
  denenir, sonra vazgeçilir (heap düşükken de atlanır — bkz. `BLE_SAFE_MIN_HEAP`).
- Web arayüzünden açık/kapalı anahtarlanabilir (`/api/telegram/ayar`).

## 🚨 Alarm Sistemi

### Sudepo Zonu — Alarm Modları (`ALARM_MOD_*`, esp8266_slave)

| Mod | Değer | Davranış |
|---|---|---|
| Sesli | 1 | Tetiklenince röle/siren hemen çalışır |
| Sessiz | 2 | Röle hiç çalışmaz, sadece web/SSE/Telegram bildirimi |
| Onaylı | 3 | Önce onay bekler ("Sesli" veya "Sessiz (Lamba)"); onaylanınca ilgili moda göre davranır |

Tetikleyiciler (bitmask, `ALARM_TRIGGER_*`): Kapı1, Kapı2, PIR, Su seviyesi düşük, Kaçak, Sensör hatası.

### Konteyner Zonu — Aynı Mod Numarasını Paylaşır (ESP32-local mantık)

Konteyner'in kendi PIR/kapı eskalasyonu, Sudepo'nun alarm modunu (`alarmStatus.mode`) referans alır
ama kendi `konteynerAlarmEtkin` anahtarıyla (Kontrol sekmesi) bağımsız olarak açılıp kapatılabilir.
Panik, her iki zonu da moddan bağımsız anında tetikler ("elle açılan anahtar" gibi davranır).

### Panik ve Susturma — Idempotent Protokol

`PANIC`/`ALARM_MUTE` komutları **toggle değil, explicit set** (`MASTER:PANIC=1/0`) — RS485 ACK kaybolup
komut retry ile tekrar gönderilse bile sonuç aynı kalır, çift-tetikleme riski yok.

## 🌙 Gece Enerji Politikası

Bahçede güneş enerjisi + akü ile çalışılır, gece üretim yok. Bu yüzden:

- Depo seviyesi ölçümü gece seyrekleşir (`NIGHT_MEASURE_INTERVAL` 900sn, gündüz `DAY_MEASURE_INTERVAL` 60sn).
- **Alarm/panik/susturma/nem kontrolü her zaman anlık çalışır** — gece dahil hiçbir gecikme kabul
  edilmez (asıl güvenlik ihtiyacı gece ortaya çıkar). RS485 dinleme, Nano poll'u, web sunucusu gece
  de tam hızda çalışır; gerçek deep-sleep kullanılmaz.
- Hava durumu sorgusu, bahçede sabit internet olmadığından, kullanıcı mobil hat/hotspot'a
  bağlandığında güncellenir; 7 günden eski tahmin fail-open olarak geçersiz sayılır.

## 💾 Veri Depolama

- **ESP32 (SPIFFS):** `/kayitlar.csv` (dolum kayıtları, max 500), `/settings.json`,
  `/monthly_archive.csv`, `/alarm_log.csv`.
- **ESP8266 (EEPROM + LittleFS):** Ayarlar/eşikler EEPROM'da (`EEPROM_MAGIC` ile versiyonlanır, WiFi
  bilgisi ayrı bir EEPROM bölgesinde bağımsız saklanır); kayıtlar/tüketim geçmişi LittleFS'te. Web
  arayüzü (`web/index.html`+`app.js`) artık LittleFS'ten değil, derleme zamanında firmware'e gömülür.
- **Nano:** Kalıcı ayar yok — röle polaritesi (`SET_RELAY_POLARITY`) EEPROM'da kalıcı tutulur.

## 🔌 Fiziksel Pin Şeması

Tam pinout tabloları (her GPIO'nun ne olduğu, hangi modüle bağlandığı, kablaj özeti) için:

- [esp32_master/docs/pinout.html](esp32_master/docs/pinout.html) — RS485 master pinleri + Konteyner
  Zonu (IR, PIR2, kapı reed, siren, lamba).
- [esp8266_slave/docs/pinout.html](esp8266_slave/docs/pinout.html) — RS485 slave pinleri, HC-SR04,
  DS1307 RTC, Nano UART bağlantısı ve Nano'nun kendi pin haritası.

## 🎯 OTA / Firmware Güncelleme

- **ArduinoOTA (ağ üzerinden, PlatformIO'dan):** `pio run -t upload`, WiFi ağındaysa USB gerekmez.
- **Web'den URL:** GitHub'daki `firmware/*.bin` linkini yapıştır (`GITHUB_FIRMWARE_URL`, `config.h`).
- **Web'den dosya:** Bilgisayardaki `.bin`'i doğrudan seç, GitHub'a ihtiyaç yok.
- Her iki kart da bu üç yöntemi destekler; şifreler `secrets.h` → `OTA_PASSWORD`.

## 📱 Android Uygulaması (BLEDProject)

Ayrı repo (`d:\Kodlar\Android\BLEDProject`), Jetpack Compose + BLE. ESP32-S3'ün BLE GATT servisine
bağlanıp lamba/alarm/kapı/panik komutlarını gönderir, anlık durumu (seviye, alarm, nem, panik) gösterir.
Ekran kapalıyken/uygulama arka plandayken de bağlantı ve alarm sesi/bildirimi sürsün diye bir
foreground service (`BleForegroundService`) BLE'yi sahiplenir.

---

**Not:** Bu dosyanın önceki sürümü (RS485 115200 baud, GPIO16/17/2, port 8080, SoftwareSerial vb.)
projenin çok erken bir prototip aşamasını anlatıyordu ve gerçek donanımla uyuşmuyordu — yukarıdaki
içerik güncel koddan (2026-08-11 itibarıyla) doğrulanarak yazıldı.
