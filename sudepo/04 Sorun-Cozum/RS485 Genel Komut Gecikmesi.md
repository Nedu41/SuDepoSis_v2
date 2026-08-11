---
tags: [sorun-cozum, rs485]
---

# RS485 Genel Komut Gecikmesi

## Belirti

Lamba aç/kapa gibi komutlar gecikmeli uygulanıyordu.

## Kök neden

RS485 hat çakışması — ölçülerek doğrulandı.

## Çözüm

Mutex eklendi ve gereksiz otomatik gönderim kaldırıldı.

## İlgili

- **Not:** Mutex'i veya kaldırılan otomatik gönderimi geri getirme.
