---
tags: [sorun-cozum, konteyner, gpio, panik]
---

# Fiziksel Acil Buton GPIO14 Touch Pin Sorunu

## Belirti

Konteyner ESP32'deki fiziksel Acil Durum Butonu (web panik butonuyla paralel çalışması gereken) kararsızdı: bazen çalışıyor, çoğu zaman `digitalRead()` hep LOW takılı kalıyordu. En kafa karıştıranı: **elle boş pime dokunmak bile** (gerçek bir GND bağlantısı olmadan) butonu tetikliyordu - "dokunmatik buton gibi" davranıyordu.

## Denenen ama işe yaramayan teoriler

- Kablo/switch'te gerçek kısa devre arama
- Pull-up/pull-down ayarları (zaten doğruydu)
- ADC2/WiFi mux "stuck" workaround'u (her okumadan önce `pinMode()` tekrar)
- `rtc_gpio_deinit()` ile RTC/touch mux'tan çıkarma

Üçü de sonucu değiştirmedi - pin hâlâ kararsızdı.

## Kök neden

**GPIO14, ESP32-S3'te TOUCH14 kanalı** (TOUCH1-14 = GPIO1-14, resmi Espressif dokümantasyonuyla doğrulandı). Sadece zayıf dahili pull-up (~45kΩ) ile kullanıldığında, pime veya ona giden kabloya dokunmak gerçek bir devre tamamlamadan bile kapasitif kuplaj yoluyla pini LOW'a çekebiliyor - güvenlik-kritik bir buton için kabul edilemez.

## Çözüm

`ACIL_BUTON_PIN`, **GPIO14'ten GPIO15'e** taşındı (dokunma/ADC özelliği olmayan düz bir GPIO). Sahada birden fazla temiz basış-bırakış döngüsüyle doğrulandı.

## Ders

ESP32-S3'te **GPIO1-14 aralığının tamamı dokunmaya duyarlı (TOUCH1-14)**. Bu aralıktaki hiçbir pin, güvenlik-kritik (panik butonu, acil durum anahtarı gibi) bir dijital giriş için kullanılmamalı - özellikle kablo uzun/tam ekranlanmamışsa. Yeni bir buton/anahtar eklerken GPIO15, GPIO19, GPIO20, GPIO43, GPIO44 gibi dokunma özelliği olmayan pinler tercih edilmeli (GPIO26-37 flash/PSRAM için ayrılmış, N16R8/Octal PSRAM modüllerinde GPIO33-37 modülün İÇİNDE PSRAM'e dahili bağlı ve dışarı hiç çıkmıyor - kullanılmamalı, bkz "GPIO35 Octal PSRAM Sorunu" notu; GPIO47/48 bu kartta PSRAM'in 1.8V rayından besleniyor ve GPIO48 kartın üzerindeki RGB LED'e ait - dış devre sürmek için güvenilmez).

## İlgili

- Bkz. hafıza: GPIO14 Touch Pin Sorunu → GPIO15
- `esp32_master/include/config.h` "Konteyner Donanimi" pin planlaması yorumu (serbest/yasak pin listesi güncel tutulur)
