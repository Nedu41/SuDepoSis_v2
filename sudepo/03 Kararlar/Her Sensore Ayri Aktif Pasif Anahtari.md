---
tags: [karar, alarm]
---

# Her Sensöre Ayrı Aktif/Pasif Anahtarı

## Durum

Kabul edildi

## Bağlam

Konteyner zonuna yeni bir sensör (Swan PIR) eklendiğinde, henüz donanıma bağlanmadığı için sürekli "açık devre" (true/tetiklenmiş) okuyordu - bu da genel reset mantığını kilitliyordu (bkz [[Kalburum PIR Ilk Acilis ve Takilma Sorunlari]] deseniyle benzer bir sınıf sorun). Ayrıca kullanıcı genel olarak arızalı/kablosuz bir sensörü tek tıkla devre dışı bırakabilmek istedi.

## Karar

Her sensöre, mod/zamandan tamamen bağımsız, her zaman geçerli ayrı bir aktif/pasif anahtarı eklendi:
- **Konteyner:** PIR2, Kapı reed, Swan PIR - ayrı ayrı (`konteynerPirEtkin`/`konteynerKapiEtkin`/`konteynerSwanEtkin`, NVS'de kalıcı).
- **Sudepo:** Kapı1, Kapı2, PIR, Su Seviyesi, Kaçak, Sensör Hatası - tek bir bitmask (`ayar.alarmSensorEtkin`, mevcut `ALARM_TRIGGER_*` bitlerini yeniden kullanır).

Pasif yapılan sensör hâlâ ham durumunu (Var/Yok) gösterir (teşhis için) ama alarma/banner/Telegram'a hiç katkı yapmaz.

## Gerekçe

Mevcut "Zamana Bağlı Tetikleyiciler" (gündüz/gece) ve "Mod Senaryoları" (Sesli/Sessiz/Onaylı girdi) checkbox'ları zaten vardı ama bunlar zamana/moda göre koşulluydu - donanım arızası/bağlı-olmama gibi kalıcı bir durumu ifade etmek için basit, koşulsuz bir anahtar gerekiyordu.

## Sonuçlar

Sudepo tarafı, YENİ bir RS485 komutu icat edilmeden mevcut GET_AYARLAR/SET_AYARLAR köprüsü üzerinden (ESP32'nin `alanlar[]` beyaz listesine tek satır eklenerek) Kalburum arayüzüne bağlandı - düşük riskli, kanıtlanmış bir mekanizma üzerine inşa edildi. Gerçek cihazda API round-trip testiyle doğrulandı.
