---
tags: [karar, konteyner, donanim]
---

# Swan Quad PET PIR Entegrasyonu

## Durum

Kabul edildi (yazılım hazır, donanım henüz kurulmadı)

## Bağlam

Kullanıcının elinde ticari bir alarm PIR dedektörü (Swan Quad, pet-immune) var, bunu Konteyner (Kalburum) zonuna eklemek istedi. Terminal yapısı: 1/2 = ±12V besleme, 3/6 = EOL (hat sonu direnci, denetimsiz kullanımda gerekmiyor), 4/5 = tamper (kurcalama, bu turda eklenmedi), 7/8 = röle kontağı (NC/COM).

## Karar

Röle kontağı (7-8), reed switch (kapı) ile AYNI mantıkla `SWAN_PIR_PIN` (GPIO15, `INPUT_PULLUP`) üzerinden okunuyor. Kapı gibi ANINDA eskale ediyor - PIR2'nin (HC-SR505) 10-60sn'lik yazılım onay süresine ihtiyacı yok, kendi donanımsal debounce/pet-immunity'si zaten var. 12V besleme siren/lamba ile aynı harici hattan verilecek, ESP32'nin 3.3V/5V çıkışından DEĞİL. Tamper devresi (4-5) bu turda eklenmedi.

## Gerekçe

Reed switch'in zaten kanıtlanmış NC/COM + INPUT_PULLUP deseni doğrudan yeniden kullanılabildi - yeni bir donanım okuma mantığı icat etmeye gerek kalmadı.

## Sonuçlar

Banner/Telegram'da "Konteyner: Swan PIR" adıyla ayrı gösteriliyor. Donanım henüz bağlı olmadığından `swan_pir` alanı sürekli `true` (fail-safe "açık devre" okuması) - bu yüzden [[Her Sensore Ayri Aktif Pasif Anahtari]] eklenene kadar diğer bayrakların sıfırlanmasını engelliyordu, artık "Swan PIR Aktif" kapatılarak devre dışı bırakılabiliyor. Kurulum sonrası: `/api/status`'taki `konteyner.swan_pir` alanından NC/NO polaritesi doğrulanmalı (ters ise `konteynerSensorleriOku()`'daki `==HIGH` → `==LOW` tek satır değişir).
