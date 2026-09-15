# esp8266_slave

ESP8266 (NodeMCU 12E) — depo seviyesi ölçümü, alarm/panik yönetimi ve toprak nemine göre otomatik
sulama. RS485 üzerinden `esp32_master`'a rapor verir, UART0 üzerinden `nano_io`'yu (kapı/röle/PIR)
yönetir. Genel mimari için [../README.md](../README.md) içine bak.

## Kurulum

```bash
cp include/secrets.h.example include/secrets.h
# secrets.h icine gercek WIFI_AP_PASSWORD / OTA_PASSWORD gir
pio run -t upload
```

Tek komut yeterli — web arayüzü (`web/index.html` + `web/app.js`) her derlemede otomatik olarak
firmware'in içine gömülür (`scripts/gen_web_content.py` → `include/web_content.h`), ayrı bir
`uploadfs` adımı yok. LittleFS artık yalnızca çalışma zamanı verisi (kayıtlar, tüketim geçmişi,
WiFi bilgisi) için kullanılıyor, bu yüzden firmware güncellemeleri onu hiç etkilemiyor.

- Board: ESP8266 12E (NodeMCU v2), LittleFS dosya sistemi (yalnızca çalışma zamanı verisi için)
- Varsayılan AP: SSID `SuDepo` (şifre `secrets.h`'da), her zaman açık
- Web portu: 80 (firmware'e gömülü `web/index.html` + `web/app.js`)
- mDNS: `sudepo.local`

## Pin Haritası

| Pin | GPIO | İşlev |
|---|---|---|
| D0 | 16 | RS485 TX (MAX485 DI) |
| D7 | 13 | RS485 RX (MAX485 RO) |
| D3 | 0 | RS485 DE/RE (upload sırasında 3V3'e jumper gerekir) |
| D5 | 14 | HC-SR04 TRIG |
| D6 | 12 | HC-SR04 ECHO |
| D9 | 3 | UART0 RX ← Nano TX |
| D10 | 1 | UART0 TX → Nano RX |
| D1 | 5 | RTC (DS1307) SCL |
| D2 | 4 | RTC (DS1307) SDA |
| D13 | — | Depo lambası rölesi (Nano üzerinden) |
| A0 | ADC0 | Toprak nem sensörü |

Tam pin şeması için [docs/pinout.html](docs/pinout.html).

## RS485 Durum Alanları

Slave, master'a ~1 saniyede bir (veya `MASTER:REQUEST_ESP8266` isteğine yanıt olarak) şu satırı gönderir:

```
ESP8266:LEVEL=..,PCT=..,LITRE=..,TEMP=..,MODE=day|night,K1=..,K2=..,R=..,LAMBA=..,ALARM=..,
ERR=..,RTC=..,LEAK=..,LEAK_DK=..,FILL=..,MOISTURE_RAW=..,MOISTURE_PCT=..,MOISTURE_OUTPUT=..,
MOISTURE_AUTO=..,MOISTURE_LOW=..,MOISTURE_HIGH=..,ALARM_MOD=..,ALARM_MUTE=..,ALARM_PENDING=..,
PANIC=..,TRIG_MASK=..
```

| Alan | Anlamı |
|---|---|
| `LEVEL`/`PCT`/`LITRE` | Depo seviyesi (cm/yüzde/litre) |
| `MODE` | `day`/`night` (gece modu) |
| `K1`/`K2` | Kapı 1/2 açık mı |
| `R` | Alarm rölesi fiziksel durumu |
| `LAMBA` | Depo lambası açık mı |
| `ALARM` | Alarm rölesi aktif mi (ayar) |
| `ERR` | Sensör hatası |
| `RTC` | RTC saati geçerli mi |
| `LEAK`/`LEAK_DK` | Kaçak alarmı / kaç dakikadır sürüyor |
| `FILL` | Dolum devam ediyor mu |
| `MOISTURE_RAW`/`MOISTURE_PCT` | Toprak nem ham/yüzde değeri |
| `MOISTURE_OUTPUT`/`MOISTURE_AUTO`/`MOISTURE_LOW`/`MOISTURE_HIGH` | Sulama çıkışı ve otomasyon ayarları |
| `ALARM_MOD`/`ALARM_MUTE`/`ALARM_PENDING` | Alarm modu (sesli/sessiz/onaylı), susturma, onay bekliyor |
| `PANIC` | Panik modu aktif mi |
| `TRIG_MASK` | O an alarmı tetikleyen sensör(ler) bitmask'i |

Master'dan gelen komutlar (`MASTER:<KOMUT>`) için `src/main.cpp` içindeki `rs485KomutDinle()`'ye bak
— `SET_MOISTURE_AUTO=`, `SET_RAIN_SKIP=`, `SET_ALARM_MOD=`, `PANIC`, `ALARM_MUTE` vb.

## Gece Enerji Politikası

Bkz. [../README.md](../README.md#gece-enerji-politikası) — özetle: alarm/panik/susturma gece de anlık
çalışır, sadece depo ölçüm sıklığı geceleri düşürülür (`NIGHT_MEASURE_INTERVAL`, config.h).

## Nano İletişimi

UART0, 9600 baud. Protokol ve komut listesi için [../nano_io/README.md](../nano_io/README.md) içine bak.
