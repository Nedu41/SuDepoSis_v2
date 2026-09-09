---
tags: [sorun-cozum, esp32, gpio, acil-lamba]
tarih: 2026-08-30
---

# Acil Durum Lambasi GPIO21 Cikissiz Sorunu

## Belirti

Acil Durum Lambasi (`ACIL_LAMBA_PIN`), web'den ac/kapa komutuyla `digitalWrite()` calisiyor gorunuyordu ama pimin kutudan disariya fiziksel bir cikisi hic yoktu - yani yazilim "calisiyor" dese de gercekte hicbir lamba yanmiyordu.

## Kok Neden

GPIO21, eskiden yedek aku sarj rolesi/MOSFET tetiklemesi icin kullaniliyordu (2026-08-24'te bu mantik tamamen kaldirildi, cunku Schulzz marka PWM solar sarj kontrolcusu isi artik donanimsal olarak kendi basina yapiyor). Ayni GPIO21 + ayni fiziksel MOSFET modulu, bosa cikmasin diye "Acil Durum Lambasi" icin yeniden kullanilmaya karar verildi ve koddaki yorum satirinda "MOSFET'in yuk tarafina artik sari RCA uzerinden 12V veriliyor" diye not dusulmustu.

Bu not hic uygulanmamis eski bir plandi. GPIO21'in kutudan disari cikan gercek bir fiziksel yolu (RCA/klemens) yoktu - MOSFET modulu baglanti bekliyordu ama hicbir zaman Sari RCA'ya kablolanmamisti.

Sari RCA (arka panel, 13'lu ic konnektor pin 4), ayri bir `SARI_RCA_PIN` (GPIO12) olarak zaten tanimliydi ama amaci "henuz belirlenmedi" notuyla bos duruyordu - kutudan disari zaten cikan bu pin, kullanilmayan Acil Lamba amaciyla eslenmemisti.

## Cozum

`ACIL_LAMBA_PIN`, **GPIO21'den GPIO12'ye** tasindi - disari zaten cikan Sari RCA hattina. Ayri `SARI_RCA_PIN` tanimi kaldirildi (artik ayni pin, tek amacla, `ACIL_LAMBA_PIN` adiyla anilıyor). GPIO21 tamamen bosta kaldi.

Donanimda dogrulandi: web arayuzunden `/api/acil-lamba?durum=1` cagrisiyla lamba fiziksel olarak yandi (Sari RCA cikisinda goruldu), `durum=0` ile sondu.

## Ders

Bir GPIO'nun yazilimda "calisiyor" gorunmesi (digitalWrite hatasiz donmesi), o pimin **fiziksel olarak disari cikip cikmadigini** kanitlamaz. "MOSFET'in yuk tarafina X veriliyor" gibi yorum satirlari, gercekten kablolanip sahada dogrulanana kadar plan/niyet olarak isaretlenmeli, gerceklesmis olgu gibi yazilmamali.

## Ilgili

- `esp32_master/include/config.h` (`ACIL_LAMBA_PIN` tanimi ve GPIO21/GPIO12 yorumlari)
- `esp32_master/src/main.cpp` (`acilLambaGuncelle()`, `handleAPI_AcilLamba()`)
- `esp32_master/docs/pinout.html` (GPIO21/GPIO12 satirlari)
