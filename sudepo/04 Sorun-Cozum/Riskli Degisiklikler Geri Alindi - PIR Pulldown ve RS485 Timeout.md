---
tags: [sorun-cozum, rs485, pir]
---

# Riskli Değişiklikler Geri Alındı - PIR Pulldown ve RS485 Timeout

## Belirti

İki ayrı "iyileştirme" denemesi, kanıt olmadan (sadece teorik gerekçeyle) yapıldı ve ikisi de gerçek regresyona yol açtı.

## Kök neden

1. **PIR2_PIN `INPUT_PULLDOWN`:** "float pin" teorisiyle eklendi, ama sensörün zayıf sürüculü çıkışıyla dahili pull-down çakışıp gerçek hareketi de algılamaz hale getirdi.
2. **RS485 ACK timeout kısaltma (1000ms×3 → 400ms×2):** Alarm banner butonlarını "hızlandırmak" için yapıldı. Bu projede RS485 hat çakışması daha önce zaten gerçek/belgelenmiş bir sorun olduğundan (bkz [[RS485 Genel Komut Gecikmesi]]), daha dar bir pencere `ALARM_MUTE` gibi komutların ulaşmasını güvenilmez hale getirdi - kullanıcı reed switch alarmını tetikleyip Sustur'a bastığında siren susmadı.

## Çözüm

İkisi de eski, kanıtlanmış değerlerine geri alındı: `PIR2_PIN` → `INPUT`, RS485 ACK timeout → `1000ms, 3 deneme`.

## İlgili

- Bkz hafıza: feedback_ble_tuning_riski (genişletildi - artık sadece BLE değil, RS485/GPIO pull gibi TÜM donanım zamanlama parametreleri için geçerli genel kural: kanıt olmadan "teorik olarak daha iyi" gerekçesiyle değiştirme).
- [[Kalburum PIR Ilk Acilis ve Takilma Sorunlari]]
