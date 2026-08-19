---
tags: [sorun-cozum, sudepo, web]
---

# Nem Otomatik/Manuel Mod Kalıcı Kalmıyordu

## Belirti

Kullanıcı Sudepo web arayüzünde nem sensörü modunu (Otomatik/Manuel) değiştiriyor, ama daha sonra "hafızada kalmıyor" gibi görünüyordu - mod sanki kendiliğinden eski haline dönüyordu.

## Kök neden

EEPROM kaydı aslında HER ZAMAN doğru çalışıyordu (hem `/nem/mod` endpoint'i hem Kalburum'dan gelen `SET_MOISTURE_AUTO=` komutu `ayarlariKaydet()` çağırıyordu). Gerçek hata web arayüzündeydi: "Ayarlar" sekmesindeki `<select id=moistureAuto>` dropdown, ayrı bir "Otomatik/Manuel" butonuyla (`moistureModeToggle()`) değiştirildiğinde GÜNCELLENMİYORDU - sadece buton metni ve durum yazısı güncelleniyordu, select'in kendi değeri eski/donuk kalıyordu. Sayfadaki `#sekme-ayarlar`/`#sekme-alarm` içindeki HERHANGİ bir alan değiştiğinde otomatik tetiklenen genel "Kaydet" (`ayarKaydet()`), TÜM formu (bu donuk select dahil) sunucuya gönderiyordu - bu da nem modunu SESSİZCE eski haline geri döndürüyordu.

## Çözüm

`guncelle(d)` fonksiyonuna, her durum güncellemesinde (`d.moistureAuto`) `#moistureAuto` select'inin değerini de senkronize eden bir satır eklendi (input odaklıyken üzerine yazmıyor). Ayrıca `moistureModeToggle()` de kendi içinde select'i anında günceller, bir sonraki poll'u beklemeden.

## İlgili

- `esp8266_slave/web/app.js` - `guncelle()`, `moistureModeToggle()`
- Bu, projede daha önce de görülen bir bug sınıfı: bir ayarın BİRDEN FAZLA yoldan (özel buton + genel form) değiştirilebilmesi, biri diğerinin DOM durumunu senkron tutmazsa sessiz veri kaybına yol açar.
