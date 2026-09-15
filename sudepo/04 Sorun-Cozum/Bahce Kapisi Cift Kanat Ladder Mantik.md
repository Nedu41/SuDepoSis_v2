---
tags: [sorun-cozum, bahce-kapisi, rs485, r413d08]
---

# Bahçe Kapısı Çift Kanat Ladder Mantık

## Belirti

Kalburum'un tek "Aç/Kapat" butonu (iki kanadı birlikte, gecikmeli sırayla hareket ettiren `kapiCiftKanatAc/Kapat`) defalarca TUTARSIZ davranıyordu: bazen sadece sol kapı çalışıyor, bazen hiçbiri, bazen bir röle anlık çekip kendiliğinden bırakıyordu. Sudepo'nun kendi tekli (Kapı1/Kapı2 ayrı ayrı) butonları ise HER ZAMAN sorunsuzdu. Ayrıca yön değiştirirken (aç↔kapat) bir kez PSU'yu 3.2V'a düşürecek kadar ciddi bir "sigorta atması" (gerçekte PSU'nun hiccup-mode koruması) yaşandı.

## Denenen ama tek başına yetersiz kalan teoriler

- PSU/güç kaynağı sorunu
- Aşırı akım eşiği çok düşük (motor/ACS712 henüz bağlı değilken sensör gürültüsü sahte tetikleme yapıyordu - GERÇEK bir bulgu ama asıl sebep değildi)
- RS485 hattı genel güvenilirliği (%49 "no response" ölçüldü, gerçekti ama "neden sadece kapama etkileniyor açma değil" sorusunu açıklamıyordu)

## Kök neden (üç ayrı hata üst üste binmişti)

1. **Modbus inter-frame sessizliği eksikti** - `r413RoleYaz()` art arda çağrıldığında (önce motoru durdur, sonra yeni yönü aç) çerçeveler arasında Modbus RTU'nun gerektirdiği ~3.6ms (9600 baud'da 3.5 karakter) sessizlik yoktu. R413D08 bunları tek bozuk çerçeve sanıp sessizce reddediyordu (fire-and-forget, hata dönmüyor). Tek başına gönderilen bir test yazımı bu yüzden hep çalışıyordu, gerçek kapı komutlarındaki 3-4 ardışık çerçeve genelde kayboluyordu.
2. **rs485KomutDinle() reentrancy'si** - "beklerken RS485 komutlarını da dinle" diye eklenen düzeltmeler, Kalburum'dan TAM o sırada yeni bir komut gelirse aynı kapının state machine'inin kendi içinden tekrar (reentrant) çalışmasına yol açıyordu - dış çağrının okuduğu "durum" değişkenleri bayatlayıp üzerine yazılıyordu.
3. **Kilit rölesi garanti altında değildi** - sadece kilit-darbe fazının kendi zaman aşımında bırakılıyordu; reentrancy yüzünden bu fazdan başka bir yola geçilirse kilit sonsuza kadar enerjili kalabiliyordu.

## Çözüm

1. `r413RoleYaz()`'a çerçeve sonrası `delay(5)` eklendi.
2. `rs485KomutDinle()` çağrıları, kapı state machine'ini mutasyona uğratabilen ÜÇ bekleme fonksiyonundan (`eskiYonBirakmasiniBekle`, `r413RoleKapatDogrulayarak`, `nanoAnalogOku`) kaldırıldı - sadece salt-okunur servisler nested çağrılabilir kuralı netleşti.
3. `kapiMotorDurdur()` artık HER çağrıldığında kilidi de garanti altına alıyor.
4. **En önemlisi:** eski soyut "gecikmeliKomut + kapiCiftKanatAc/Kapat" mekanizması TAMAMEN kaldırılıp, kullanıcının sahada TAM olarak tarif ettiği net ladder-mantık sekansıyla değiştirildi:
   - **Açma:** Röle5(kilit) HIGH → 1sn → Röle3(Kapı2/SAĞ açma) HIGH → 1sn → Röle1(Kapı1/SOL açma) HIGH → 1sn → Röle5 LOW.
   - **Kapama:** Röle2(Kapı1/SOL kapama) HIGH → 2sn → Röle4(Kapı2/SAĞ kapama) HIGH.
   - Bu sahada "kapanışta ters sıra" (klasik bindirmeli kapı kuralı) GEÇERLİ DEĞİL - Kapı2 (SAĞ) her iki yönde de gecikmeli kanat.
   - SW tetiklenince ilgili röleyi kapatma işi zaten mevcut per-kapı limit-switch kontrolünden geliyor, ayrıca yazılmadı.

## Ders

- "Beklerken başka bir şeyi de servis et" deseni, servis edilen şey AYNI state'i mutasyona uğratabiliyorsa reentrancy riski taşır - sadece durum bildiren/salt-okunur servisler bu şekilde nested çağrılabilir.
- Aynı kategoride hata 2. kez tekrarlarsa patch eklemeyi durdurup ilgili dosyayı BAŞTAN SONA oku - bu oturumda bu adım çok geç atıldı, atılınca hata 10 dakikada bulundu.
- Basit yol (Sudepo tekli buton) ile karmaşık yol (Kalburum çift-kanat) ayrı ayrı test edilerek izole edilmesi, teori havuzunu anında daralttı - bunu erken önermek gerekirdi.
- Kullanıcı sahada TAM/somut bir röle-switch sırası tarif ettiğinde, mevcut soyut state machine'e zorlamak yerine DOĞRUDAN o sekansı yazmak çok daha güvenilir sonuç verdi.

## İlgili

- Bkz. hafıza: `project_bahce_kapisi_ladder_mantik_cozuldu`, `feedback_uzun_hata_ayiklamada_disiplin`
- `esp8266_slave/src/bahce_kapisi.cpp` - `bahceIkisiniAc()`/`bahceIkisiniKapat()`/`ikiliSekansPoll()`
- `esp8266_slave/include/config.h` - `BAHCE_IKILI_ADIM_AC_MS`/`BAHCE_IKILI_ADIM_KAPA_MS`
