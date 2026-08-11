---
tags: [sorun-cozum, arayuz]
---

# JS Değişken Çakışması Tüm Sayfayı Kilitledi

## Belirti

Swan PIR alanı eklendikten sonra: ana sayfa geliyor ama sekmeler dahil hiçbir şey çalışmıyor.

## Kök neden

`renderUI()` içinde Swan PIR durumunu yazan yeni satırda `const kzs` kullanıldı - ama bu isim zaten siren durumu için (`const kzs=$('#kz-siren')...`) tanımlıydı. `const` ile aynı ismin iki kez tanımlanması JS syntax hatası verip **tüm sayfa script'ini** (sadece o satırı değil) çalışmaz hale getirdi.

## Çözüm

Çakışan değişken `kzsw` olarak yeniden adlandırıldı. Ayrıca bundan sonra HTML içine gömülü JS'i değiştirdikten sonra derlemeden önce `node -e "new Function(js)"` ile hızlı bir syntax kontrolü yapılmaya başlandı.

## İlgili

- Bu tarz gömülü-HTML-içi-JS projelerinde değişken isim çakışması derleme zamanında (C++ tarafı sorunsuz derlenir) yakalanmaz, sadece tarayıcıda ortaya çıkar - OTA sonrası mutlaka tarayıcıda gerçek test şart.
