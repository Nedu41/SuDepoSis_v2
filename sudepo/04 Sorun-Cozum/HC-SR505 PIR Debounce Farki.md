---
tags: [sorun-cozum, pir]
---

# HC-SR505 PIR Debounce Farkı

## Belirti

Konteyner PIR sensörü beklenmedik şekilde tetiklenmiyor/tetikleniyordu.

## Kök neden

Konteynerdeki PIR (HC-SR505), HC-SR501'in aksine art arda kenar vermez; çıkışı sabit ~8 saniye HIGH'ta tutar.

## Çözüm

Debounce mantığında min-tetiklenme=1 olmalı; 2+ ayarı bu sensörle asla tetiklenmez.

## İlgili

- Bkz. hafıza: HC-SR505 PIR Farkı
