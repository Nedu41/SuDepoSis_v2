---
tags: [sorun-cozum, ota]
---

# OTA "No Response" Firewall Sorunu

## Belirti

`espota` ile OTA yükleme sırasında "No response" hatası alınıyordu.

## Kök neden

Kod hatası değil — Windows ağı "Public" profilindeyken `python.exe` inbound bağlantıları Windows Firewall tarafından bloklanıyordu.

## Çözüm

Ağ profilini "Private" yapmak sorunu çözdü.

## İlgili

- Bkz. hafıza: OTA Firewall/Public Profil
