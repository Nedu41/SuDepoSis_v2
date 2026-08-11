---
tags: [karar, ir]
---

# IRremote Kütüphanesi Kaldırılması

## Durum

Kabul edildi

## Bağlam

IRremote kütüphanesinin protokol tanıma/repeat-kare mantığı tekrar tekrar kararsızlığa yol açtı; ayrıca bu kartta LED-feedback pini yüzünden geçmişte brownout'a bile sebep olmuştu. Ayrıca UNKNOWN protokolde `decodedRawData` daima 0 olduğundan bazı kumandalarda "ilk 2 tuş çalışıyor, sonrası tanınmıyor" sorunu vardı.

## Karar

IRremote kütüphanesi tamamen kaldırıldı. Yerine kütüphanesiz, `attachInterrupt` + `micros()` ile kenar zamanlaması yakalayıp kare bitiminde FNV-1a hash ile "kod" üreten kendi basit kodumuz kullanılıyor.

## Gerekçe

- Hangi protokol olursa olsun çalışır (ham zamanlama darbelerinden hash üretiliyor)
- Gürültü minimum kenar sayısıyla eleniyor
- Tuşa basılı tutma sırasındaki tekrarlar zaman-bazlı kilitle yutuluyor

## Sonuçlar

- [[../04 Sorun-Cozum/IR Kumanda Gec Gelen Yanit Yarisi|IR Kumanda Geç Gelen Yanıt Yarışı]] sorunu bu değişiklikten sonra ayrı olarak fark edildi ve çözüldü (JS tarafı, bu kararla ilgisiz).
