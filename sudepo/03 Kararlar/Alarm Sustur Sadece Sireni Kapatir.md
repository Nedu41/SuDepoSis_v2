---
tags: [karar, alarm]
---

# Alarm Sustur Sadece Sireni Kapatır

## Durum

Kabul edildi

## Bağlam

Tüm zonlar aynı anda alarm verirse Sustur'a basıldığında hem siren hem lamba birden kapanıyordu. Kullanıcı, sireni kesin kapatmak isterken görsel uyarının (lamba) devam etmesini istedi.

## Karar

Susturma (Sustur/Sireni Kapat) artık SADECE sireni etkiler. Lamba, alarm durumu sürdükçe yanmaya devam eder - en az 60sn, en fazla 10dk (enerji bütçesi üst sınırı, lamba 9W / siren 3-4W hesaplanırken eklendi). Manuel lamba açma bu sınırların dışında (süresiz). Panik modu hâlâ tamamen istisna - hem siren hem lamba mute/süre sınırlarından bağımsız, elle kapatılana kadar sürer.

## Gerekçe

Sustur, "artık gürültü istemiyorum ama hâlâ bir durum var, bunu görsel olarak hatırlamak istiyorum" ihtiyacına karşılık geliyor. Lambanın süresiz yanması ise enerji bütçesi açısından riskli (akü gece tükeniyor) - bu yüzden üst sınır eklendi.

## Sonuçlar

Hem Konteyner (ESP32) hem Sudepo (ESP8266) zonunda simetrik uygulandı. Konteyner: `konteynerSusturuldu` (RS485'ten bağımsız yerel bayrak), `konteynerCikisIstenir` (lamba hedefi, mute'tan bağımsız) / `konteynerBuzzerVar` (siren, mute'a bağımlı) ayrımı. Sudepo: `alarmSusturuldu`, `lambaHedefHam` ayrı hesaplanıp `sirenSeciliHam`'dan bağımsız.
