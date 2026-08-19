---
tags: [karar, alarm]
---

# Siren Chirp ve Gecikme Deseni

## Durum

Kabul edildi (2026-08-19'da güncellendi - desen değişti, mimari aynı kaldı)

## Bağlam

Bir tetikleyici (kapı/PIR) başladığı anda siren doğrudan sürekli çalmaya başlıyordu - kısa/geçici tetiklenmelerde bile tam gürültü anında devreye giriyordu. İlk çözüm (2026-08-11, 1sn chirp/3sn gecikme) yeterince uzun bir doğrulama penceresi vermiyordu.

## Karar (GÜNCEL - 2026-08-19)

Panik (ve artık gaz alarmı, bkz [[project_gaz_alarmi_susturma_ve_etkin_anahtari]]) hariç, bir tetikleyici episode'u başladığında siren şu **kademeli** deseni izler:
- t=0: lamba hemen yanar (bu desenden BAĞIMSIZ, hep anında).
- t=0..5sn: siren sessiz (ilk gecikme).
- t=5sn'de 0.3sn'lik kısa bir "chirp".
- chirp sonrası 10sn sessiz (bekleme).
- tetikleyici hâlâ aktifse 5sn tam güçle çalar (aktif).
- tetikleyici sürdükçe bu **10sn-bekle/5sn-aktif döngüsü** tekrarlanır.

Tüm bu 4 süre (ilk gecikme, chirp süresi, bekleme süresi, aktif süre) artık web arayüzünden ayarlanabilir (NVS'de kalıcı) - eskiden `#define` sabitleriydi.

## Gerekçe

Kısa/yanlışlıkla tetiklenmelerde tam sirenin hemen gürlememesi için. Eski 1sn/3sn deseni çok kısaydı; yeni 5sn+10sn'lik pencere, özellikle Konteyner PIR'ının kendi "Onay Süresi" (varsayılan 10sn) ile üst üste bindiğinde, kısa/yanlış hareketlerin tam siren çalmadan tamamen sönmesini sağlıyor.

## Sonuçlar

Her iki zonda simetrik uygulandı (faz-tabanlı state machine: 0=ilk gecikme,1=chirp,2=bekleme,3=aktif):
- Konteyner/ESP32: `konteynerSirenGecikmeSaniye`/`konteynerSirenChirpMs`/`konteynerSirenBeklemeSaniye`/`konteynerSirenAktifSaniye` (NVS: `k_sir_gec`/`k_sir_chp`/`k_sir_bek`/`k_sir_akt`).
- Sudepo/ESP8266: `ayar.sirenGecikmeSaniye`/`sirenChirpMs`/`sirenBeklemeSaniye`/`sirenAktifSaniye` (EEPROM struct alanı, EEPROM_MAGIC 0xA548→0xA549 - bu flaş kullanıcının TÜM Sudepo ayarlarını WiFi hariç fabrika değerine döndürdü, bilerek kabul edildi).

Oto-sustur ölçümü (2dk kesintisiz çalarsa otomatik susturma) artık "kesintisiz çalma" yerine **epizot başlangıcından itibaren geçen TOPLAM süre** üzerinden yapılıyor - eski yöntem (chirp/bekleme aralıklarında sürekli sıfırlanan sayaç) yeni aralıklı desende asla 2dk'ya ulaşamıyordu, bu bug fark edilip düzeltildi.

Lamba bu desenden hâlâ etkilenmiyor, her zaman anında yanıyor.

Ayrıca aynı gün: Konteyner Ayarlar sayfasındaki "PIR Ayarları" kartı, artık siren zamanlamasını da içerdiği için "**Konteyner Alarm Ayarları**" olarak yeniden adlandırıldı.
