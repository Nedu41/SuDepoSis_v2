---
tags: [sorun-cozum, esp32, mppt, web-api, json]
tarih: 2026-08-30
---

# ESP32 MPPT Pin Dokumani ve JSON Cevap Sertlestirme

## Belirti

- `esp32_master/include/config.h` icinde MPPT/RS232 hattinin guncel yazilim atamasi `RX=GPIO41`, `TX=GPIO40` idi.
- Buna karsin `esp32_master/src/main.cpp` icindeki web arayuzu pin tablosu ve `docs/pinout.html` / `docs/mppt-baglanti-kilavuzu.html` tablolarinin bir kismi hala eski/ters bilgi veriyordu: `R1OUT -> GPIO40`, `T1IN -> GPIO41`.
- Bazi API cevaplari `weatherDurum`, `mesaj`, `sonYedekZamanStr` ve RS485 `reply` alanlarini JSON icine ham string olarak basiyordu. RS485 hattindan veya hata metninden tırnak, ters slash, newline gibi karakterler gelirse web tarafinda `fetch(...).json()` parse hatasi verebilirdi.

## Kok Neden

- Sahadaki 2026-08-15 loopback testinden sonra kodda MPPT pinleri degistirilmis, fakat gomulu HTML ve Obsidian/HTML dokuman tablolarinin hepsi ayni anda guncellenmemis.
- JSON cevaplari elle string birlestirme ile uretiliyor. Mevcut `jsonKacir()` sadece `\` ve `"` karakterlerini kaciriyordu; newline/tab gibi kontrol karakterleri icin eksikti ve her endpoint'te kullanilmiyordu.

## Cozum

- `esp32_master/src/main.cpp` web arayuzu pin tablosu guncellendi:
  - `RX (UART2) = GPIO41`
  - `TX (UART2) = GPIO40`
  - `R1OUT -> GPIO41`
  - `T1IN -> GPIO40`
- `esp32_master/docs/pinout.html` ve `esp32_master/docs/mppt-baglanti-kilavuzu.html` icindeki MPPT/MAX3232 tablolarindaki ters satirlar ayni guncel atamayla hizalandi (bu adim ilk yazimda planlanmis ama commit edilmemis kalmisti - 2026-08-30 sonraki oturumda gercekten tamamlanip donanimda flaslandi).
- `jsonKacir()` genisletildi:
  - `\`, `"`, `\n`, `\r`, `\t` kaciriliyor.
  - Diger kontrol karakterleri boslukla temizleniyor.
- Asagidaki API cevaplarinda ham string yerine `jsonKacir()` kullanildi:
  - OTA sonuc mesaji
  - ESP8266 firmware yukleme zamani
  - Kayit yedek durumu
  - Weather durum/tarih alanlari
  - Telegram test mesaji
  - RS485 `reply`, `replyLow`, `replyHigh` alanlari

### Ek (2026-08-30, ayni gun ikinci oturum): jsonSendOk/jsonSendOkReply yardimcilari

`jsonKacir()` her endpoint'te elle cagirmak, tek bir cagriyi unutma riskini surdurmuyordu (bu notun kendisi zaten boyle bir unutmadan dogdu). `{"basarili":...,"mesaj":...}` ve `{"basarili":...,"mesaj":...,"reply":...}` kaliplarinin tekrarlandigi 11 `server.send()` cagrisi, iki ortak fonksiyona tasindi:

```cpp
void jsonSendOk(bool basarili, const String& mesaj);
void jsonSendOkReply(bool basarili, const String& mesaj, const String& reply);
```

Ayni oturumda `main.cpp` (6352 satir) icindeki `handleRoot()`'un ~1789 satirlik gomulu HTML/CSS/JS govdesi de `include/web_ui.h`'ye tasindi (PROGMEM+`send_P` davranisi degismedi, derleme sonrasi RAM/Flash boyutu birebir ayni cikti - saf tasima, davranis degisikligi yok). main.cpp ~4570 satira indi.

## Dogrulama

- `pio run -e esp32s3` basarili (her iki oturumda da).
- `pio run -e mppt_loopback_test -e mppt_tx_trace_test` basarili.
- `git diff --check -- src\main.cpp docs\pinout.html docs\mppt-baglanti-kilavuzu.html` hata vermedi.
- 2026-08-30 ikinci oturum: USB ile flaslandi, cihaz boot etti, web arayuzu (`/`) ve `/api/status` dogru donuyor, `jsonSendOk`/`jsonSendOkReply` kullanan endpoint'ler (`/api/telegram-test`, lamba/alarm/mod API'leri) test edildi.

## Ilgili

- [[WiFi STA Reconnect Eksikligi]]
- [[Fiziksel Acil Buton GPIO14 Touch Pin Sorunu]]
- `esp32_master/include/config.h`
- `esp32_master/src/main.cpp`
- `esp32_master/docs/pinout.html`
- `esp32_master/docs/mppt-baglanti-kilavuzu.html`
