---
tags: [sorun-cozum, arayuz]
---

# Alarm Banner Odak Kilidi Nedeniyle Geç Görünüyordu

## Belirti

Ayarlar sekmesinde bir input alanına tıklıyken gerçek bir alarm (PIR) tetiklendi - banner görünmedi, "Hareket: Var/Yok" alanı da anlamsız/tutarsız değişiyor gibi göründü. Ana sayfaya geçince banner hemen ortaya çıktı.

## Kök neden

"Kullanıcı bir input/select/textarea'da yazarken arka plan güncellemeleri (SSE + `guncelle()`) o anki render'ı tamamen atlar" kilidi (`herhangiBirAlanaYaziliyorMu()`) - input alanları yazarken silinmesin diye eklenmiş genel bir korumaydı, ama bu koruma banner'ı da (input'la hiç ilgisi olmayan, kritik bir güvenlik göstergesini) kapsıyordu.

## Çözüm

Denendi: Banner render'ı ayrı bir fonksiyona (`renderAlarmBanner`) çıkarılıp odak kilidinden bağımsız her zaman çağrılacak şekilde değiştirildi. **Kullanıcı bunu test etmeden "son güncellemeyi geri al" dedi** - değişiklik commit'e hiç girmeden geri alındı. Kök neden teşhisi doğru ama düzeltme onaylanmadı.

## İlgili

- İleride tekrar denenirse: kullanıcı onayı almadan commit etme, önce cihazda test ettir.
