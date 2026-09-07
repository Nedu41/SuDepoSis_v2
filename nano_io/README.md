# nano_io

Arduino Nano — `esp8266_slave` kartının erişemediği fiziksel I/O'yu (kapı sensörleri, alarm rölesi,
sulama rölesi, PIR, depo lambası) yönetir. WiFi'si yok, internete çıkmaz, sadece UART0 üzerinden
`esp8266_slave` ile konuşur.

## Kurulum

```bash
pio run -t upload
```

- Board: Arduino Nano (ATmega328P)
- Bağlantı: USB-Serial, 9600 baud (hem yükleme hem çalışma zamanı haberleşmesi)
- Secrets.h gerekmez (WiFi yok)

## Pin Haritası (v2 planı)

| Pin | İşlev |
|---|---|
| D0/D1 | ESP8266 seri haberleşme (UART0) |
| D2 | Kapı 1 sensörü (INPUT_PULLUP) |
| D3 | Kapı 2 sensörü (INPUT_PULLUP) |
| D4 | Alarm rölesi (OUTPUT, NC — **LOW = aktif**) |
| D5 | Nem/sulama rölesi (OUTPUT) |
| D6 | PIR hareket sensörü (INPUT, HIGH = hareket) — ESP8266 `PIN_READ:6` ile okur |
| D7 | Bahçe Kapısı 1 (araç girişi) tam açık limit switch (INPUT_PULLUP) — 2026-09-07, henüz sahaya bağlanmadı |
| D9 | Bahçe Kapısı 2 tam açık limit switch (INPUT_PULLUP) — 2026-09-07, henüz sahaya bağlanmadı |
| D10 | Zil butonu (INPUT_PULLUP) — basılınca D12 buzzer'da ding-dong çalar |
| D11 | Yedek GPIO |
| D12 | Pasif buzzer (`TONE_PLAY`/`TONE_STOP` ile) — açılış melodisi, PIR ön-uyarı bipi, zil |
| D13 | Depo iç lamba rölesi (OUTPUT) |
| A0 | Toprak nem sensörü (analog) |
| A1 (=15) | Bahçe Kapısı 1 motor akım sensörü (ACS712 5A, `ANALOG_READ` ile) — henüz sahaya bağlanmadı |
| A2 (=16) | Bahçe Kapısı 2 motor akım sensörü (ACS712 5A, `ANALOG_READ` ile) — henüz sahaya bağlanmadı |
| A3–A5 | Yedek GPIO |
| A6–A7 | Yedek (sadece analog input) |

Tam pin şeması için [../esp8266_slave/docs/pinout.html](../esp8266_slave/docs/pinout.html).

> **Röle polaritesi:** Alarm rölesi NC (Normally Closed) tipte, `RELAY_ACTIVE` = LOW olarak
> tanımlıdır (`include/config.h`). Fiziksel polarite gerekirse reflaş etmeden
> `SET_RELAY_POLARITY:0`/`:1` komutuyla EEPROM'a kalıcı değiştirilebilir.

## Komut Protokolü (ESP8266 → Nano, 9600 baud)

| Komut | Yanıt | Açıklama |
|---|---|---|
| `GET_STATUS` | `D0=..,D1=..,RELE=..,LAMBA=..,MOISTURE=..,MOISTURE_RAW=..,MOISTURE_PCT=..` | Tüm durumu okur |
| `LAMBA_ON` / `LAMBA_OFF` | `ACK:LAMBA_ON`/`ACK:LAMBA_OFF` | Depo lambası |
| `RELAY_ON` / `RELAY_OFF` | `ACK:RELAY_ON`/`ACK:RELAY_OFF` | Alarm rölesi |
| `MOISTURE_ON` / `MOISTURE_OFF` | `ACK:MOISTURE_ON`/`ACK:MOISTURE_OFF` | Sulama rölesi |
| `SET_RELAY_POLARITY:0` / `:1` | `ACK:SET_RELAY_POLARITY` | Röle aktif seviyesini kalıcı değiştir (LOW/HIGH) |
| `PIN_MODE:<pin>,<OUTPUT\|INPUT\|INPUT_PULLUP>` | — | Yedek pin modu ayarla |
| `PIN_WRITE:<pin>,<0\|1>` | — | Yedek pine yaz |
| `PIN_READ:<pin>` | `PIN:<pin>=<0\|1>` | Yedek pin oku (PIR burada okunur) |
| `PIN_READ_ALL` | tüm pinler | Toplu okuma |
| `ANALOG_READ:<pin>` | `ANALOG:<pin>=<0-1023>` | Analog pin oku (A0-A5/14-19) — 2026-09-07 eklendi, akım sensörü gibi analog çıkışlar için |
| `TONE_PLAY:<pin>,<hz>,<ms>` | `ACK:TONE_PLAY` | Pasif buzzer'da nota çal (D12) |
| `TONE_STOP:<pin>` | `ACK:TONE_STOP` | Çalan notayı erken kes |

Tüm komutlar `ACK:<komut>` ile onaylanır; ESP8266 tarafı 3 deneme sonrası ACK gelmezse vazgeçip
durumu yeniden sorgular (bkz. `esp8266_slave/src/main.cpp` `nanoPoll()`).
