---
tags: [sorun-cozum, ir]
---

# IR Kumanda: Geç Gelen (Stale) Yanıt Yarış Durumu

## Belirti

Kullanıcı "seçim yapamıyorum, liste hemen kayboluyor, hızlı davranırsam yakalıyorum" diye şikayet ediyordu.

## Kök neden

IR yakalamayla ilgili değil, saf bir JS yarış durumuydu: `/api/ir/ogren_durum` endpoint'ine 800ms'de bir atılan istekler sırayla dönmüyordu. Kod yakalanıp form (seçim kutusu) gösterildikten sonra, daha önce gönderilmiş ama geç çözülen bir istek "hazir:false" ile formu "Kumandada bir tuşa basın..." yazısıyla eziyordu.

## Çözüm

Geç gelen (stale) `/api/ir/ogren_durum` yanıtlarına karşı koruma eklendi.

## İlgili

- [[IRremote Kutuphanesi Kaldirilmasi]]
