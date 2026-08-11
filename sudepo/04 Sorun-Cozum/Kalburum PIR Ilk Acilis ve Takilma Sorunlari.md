---
tags: [sorun-cozum, pir, konteyner]
---

# Kalburum PIR İlk Açılış ve Takılma Sorunları

## Belirti

Üç ayrı ama birbiriyle karışan belirti: (1) cihaz ilk açıldığında hiç hareket yokken sürekli/yanlış alarm veriyordu, (2) reset sonrası "Hareket: Var" hiç değişmeden takılı kalıyordu, (3) gerçek bir tetiklenmede "Sustur" butonu işe yaramıyor, siren hep açık kalıyordu.

## Kök neden

Üç farklı kök neden üst üste bindi:

1. **Sentinel bug:** `konteynerPirSonHareketMs` (0 = "hiç hareket görülmedi") boot anında `millis()-0 <= tutmaMs` hesabıyla yanlışlıkla "hareket var" sayılıyordu.
2. **Isınma süresi:** HC-SR505, boot sonrası ısınma süresi boyunca kararsız/rastgele HIGH verebiliyor - bunu filtreleyen bir grace-period yoktu.
3. **Asıl neden (en can alıcı):** PIR sinyal kablosunda gerçek bir **temassızlık** vardı. Bunu "float pin" teorisiyle `INPUT_PULLDOWN` ekleyerek çözmeye çalışıldı - kullanıcı kabloyu fiziksel olarak düzeltince pull-down'lı hal artık GERÇEK hareketi de algılamaz oldu (muhtemelen sensörün zayıf sürüculü çıkışıyla dahili pull-down'ın gerilim bölücü gibi davranması). `INPUT`'a geri dönüldü, gerçek sorun hep kablo/temas kalitesiymiş.

Ayrıca Susturma (mute) bug'ı: Konteyner sireni önce hiç `alarmStatus.muted`'e bakmıyordu, sonra bakınca da bu değişken ESP8266'dan gelen HER RS485 durum mesajında (~600ms) ezildiği için susturma bir anlık çalışıp hemen geri açılıyordu.

## Çözüm

- Sentinel karşılaştırması `konteynerPirSonHareketMs != 0` koruması ile düzeltildi.
- `KONTEYNER_PIR_BOOT_GRACE_MS` (90sn) eklendi - boot sonrası bu süre içinde eskalasyon (siren/lamba/Telegram) tetiklenmez, ham "Hareket" alanı etkilenmez.
- PIR2_PIN `INPUT` olarak kaldı (pull-down denemesi geri alındı).
- Konteyner için RS485'ten tamamen bağımsız, yerel `konteynerSusturuldu` bayrağı eklendi - artık ESP8266 bağlantısından etkilenmiyor.

## İlgili

- [[Riskli Degisiklikler Geri Alindi - PIR Pulldown ve RS485 Timeout]]
- Bkz hafıza: project_pir_kalburum_ilk_acilis, HC-SR505 PIR Farkı
