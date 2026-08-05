# SuDepoSis v2 — Güneş Enerjili Bahçe Su Deposu Yönetim Sistemi

Tarladaki bahçede, güneş enerjisiyle (3.2kW 24V MPPT, 4x545W panel, 2x12V 100A jel akü) çalışan
su deposu seviye takibi, sızdırmazlık/hırsızlık alarmı ve toprak nemine göre otomatik sulama sistemi.
Gece enerji kısıtlı olduğundan firmware bu kısıtı gözetecek şekilde tasarlanmıştır.

## Mimari

```
                    RS485 (9600 baud, CAT5)
┌──────────────────┐  ◄──────────────────►  ┌───────────────────┐
│   ESP32-S3        │                        │   ESP8266 (12E)    │
│   "master"         │                        │   "slave"           │
│  - WiFi/MQTT/OTA   │                        │  - HC-SR04 seviye   │
│  - Web arayuzu     │                        │  - Alarm/panik      │
│  - (Yakinda) hava  │                        │  - Nem otomasyonu   │
│    durumu sorgusu  │                        │  - Web paneli (AP)  │
└──────────────────┘                        └─────────┬─────────┘
                                                         │ UART0 (9600)
                                                  ┌───────▼─────────┐
                                                  │  Arduino Nano     │
                                                  │  - Kapi sensorleri │
                                                  │  - Alarm rolesi    │
                                                  │  - Nem/sulama rol. │
                                                  │  - PIR + genel GPIO│
                                                  └─────────────────┘
```

Üç kart da bu tek repoda ayrı PlatformIO projeleri olarak durur — her birinin kendi `platformio.ini`, `include/`, `src/` klasörü var. Ayrıntılar için her klasördeki kendi `README.md`'sine bak:

- [`esp32_master/`](esp32_master/README.md) — ESP32-S3, sistemin "beyni": WiFi, MQTT, OTA, web arayüzü, RS485 master.
- [`esp8266_slave/`](esp8266_slave/README.md) — ESP8266, sensör + alarm/panik + sulama otomasyonu, RS485 slave.
- [`nano_io/`](nano_io/README.md) — Arduino Nano, fiziksel I/O (kapı, röle, PIR).

VS Code'da hepsini birden açmak için kök dizindeki `sudeposis_v2.code-workspace` dosyasını kullan.

## Kurulum

### 1. Gizli bilgiler (secrets.h)

WiFi/OTA/MQTT şifreleri git'e girmez, her kartın kendi `include/secrets.h` dosyasında tutulur.
İlk kurulumda:

```bash
cp esp32_master/include/secrets.h.example esp32_master/include/secrets.h
cp esp8266_slave/include/secrets.h.example esp8266_slave/include/secrets.h
# secrets.h dosyalarini gercek WiFi/OTA sifreleriyle duzenle
```

`nano_io` kartının WiFi'si olmadığı için secrets.h'a ihtiyacı yok.

### 2. Derleme ve yükleme (PlatformIO)

```bash
cd esp32_master  && pio run -t upload   # ESP32-S3
cd esp8266_slave && pio run -t upload   # ESP8266
cd nano_io       && pio run -t upload   # Arduino Nano
```

Her klasörün `platformio.ini`'sinde `upload_port`/`monitor_port` COM portu tanımlı — kendi
bilgisayarındaki port numarasına göre güncellemen gerekebilir.

### 3. Firmware güncelleme (OTA)

Reflaş için USB'ye gerek yok, iki yöntem var (her ikisi de esp32_master ve esp8266_slave'de):

- **URL'den güncelleme:** Kartın web sayfasındaki "OTA Güncelleme" kutusuna bir `.bin` dosyasının
  linkini yapıştır (örn. GitHub Releases'e yüklenmiş bir firmware.bin).
- **Dosyadan yükleme:** Aynı kutudan bilgisayarındaki `.bin` dosyasını doğrudan seçip yükleyebilirsin
  (URL barındırmaya gerek yok).
- **ArduinoOTA (VS Code/PlatformIO'dan ağ üzerinden):** Kart WiFi ağındayken `pio run -t upload`
  otomatik olarak ağ üzerinden (OTA şifresiyle) yükler; USB bağlı olmasa da çalışır.

## RS485 Protokolü (ESP32-S3 ↔ ESP8266)

- **Hız:** 9600 baud, metin tabanlı, satır sonu `\n`.
- **Master'dan slave'e komutlar:** `MASTER:<KOMUT>` formatında, örn. `MASTER:SET_MOISTURE_AUTO=1`,
  `MASTER:SET_RAIN_SKIP=1`, `MASTER:PANIC`. Slave `ACK:<komut>` veya `NACK:<komut>` ile yanıtlar.
- **Slave'den master'a periyodik durum:** `ESP8266:LEVEL=..,PCT=..,LITRE=..,MODE=day|night,...\n`
  şeklinde ~1 saniyede bir (veya master'ın `MASTER:REQUEST_ESP8266` isteğine yanıt olarak) gönderilir.
  Tüm alanlar için [esp8266_slave/README.md](esp8266_slave/README.md#rs485-durum-alanlari) içine bak.

## Öne çıkan özellikler

- **Depo seviyesi:** HC-SR04 ultrasonik sensör, yatay silindirik/dikey depo desteği, litre/yüzde hesaplama.
- **Alarm sistemi:** Kapı, PIR (hareket), su seviyesi düşük, kaçak tespiti — 3 mod (sesli/sessiz/onaylı),
  susturma, panik butonu. Gece de dahil her zaman anlık çalışır — bkz. "Gece enerji politikası" aşağıda.
- **Toprak nemine göre otomatik sulama:** Eşik bazlı (düşük/yüksek nem), gece kısıtlaması yok (nem her an
  aynı hassasiyetle kontrol edilir).
- **Yağmur tahminine göre sulama atlama:** ESP8266, master'dan `SET_RAIN_SKIP=1` geldiğinde otomatik
  sulamanın yeni bir döngü başlatmasını engeller (bkz. `esp8266_slave/src/main.cpp` `applyMoistureControl()`).
  Hava durumu sorgusunun kendisi ESP32-S3 master'da yapılır — bkz. [esp32_master/README.md](esp32_master/README.md#hava-durumu--yagmur-tahmini).
- **MQTT:** ESP32-S3, yerel bir Mosquitto broker'a (`127.0.0.1:1883`) `sudeposis/level` vb. topic'lere yayın yapar.
- **Web arayüzü:** Her iki WiFi'li kartın kendi web paneli var (ESP8266: LittleFS'ten servis edilen
  `data/index.html`+`app.js`; ESP32: koda gömülü HTML).

## Gece enerji politikası

Sistem gece (varsayılan 22:00–06:00) güneş üretimi olmadan, sadece akü ile çalışır. Bu yüzden:

- Depo seviyesi ölçümü gece seyrekleşir (15 dk, gündüz 1 dk).
- **Alarm/panik/susturma her zaman anlık çalışır** — gece dahil hiçbir gecikme kabul edilmez, çünkü asıl
  güvenlik ihtiyacı gece ortaya çıkar. Bu yüzden RS485 dinleme, Nano poll'u ve web sunucusu gece de tam
  hızda çalışmaya devam eder; gerçek deep-sleep kullanılmaz.
- Bahçede sabit internet yok — ESP32-S3, kullanıcı bahçedeyken açtığı mobil hat/hotspot üzerinden
  internete çıkabildiğinde hava durumunu günceller; bu yüzden yağmur tahmini "en son ne zaman
  güncellendiyse o kadar taze" kabul edilir ve 48 saatten eskiyse otomatik geçersiz sayılır (bitkinin
  susuz kalmaması, gereksiz sulamadan önceliklidir).

## Güvenlik notu

WiFi/OTA/MQTT şifreleri `secrets.h` dosyalarında tutulur ve git'e girmez (bkz. `.gitignore`).
Bu dosyaları asla commit etme veya paylaşma.
