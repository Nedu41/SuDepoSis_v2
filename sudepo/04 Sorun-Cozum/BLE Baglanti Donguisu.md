---
tags: [sorun-cozum, ble]
---

# BLE Bağlantı Döngüsü

## Belirti

Android uygulaması ESP32 ile BLE üzerinden kararsız bağlanıyor, sürekli kopup yeniden bağlanıyordu.

## Kök neden

Sorun ESP32 firmware'inde değil, Android uygulamasındaydı: reconnect döngüsü ve `connectGatt` çağrısında `TRANSPORT_LE` eksikliği.

## Çözüm

Android tarafında reconnect mantığı ve `connectGatt(..., TRANSPORT_LE)` düzeltildi.

## İlgili

- Bkz. hafıza: BLE Bağlantı Sorunu ÇÖZÜLDÜ
- **Not:** ESP32 tarafındaki BLE zamanlama parametrelerini kanıt olmadan değiştirme — gecikmeyi kötüleştirebilir.
