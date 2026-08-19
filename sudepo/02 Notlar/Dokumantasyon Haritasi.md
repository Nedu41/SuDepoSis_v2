---
tags: [not, dokumantasyon]
---

# Dokümantasyon Haritası

Amaç: projede aynı bilgi (pin numarası, sensör davranışı, mimari kararı) birden fazla yerde tutulduğu için, biri güncellenip diğerleri unutulunca "eski/yanlış kılavuz bizi hataya sürükler" riski oluşuyor. Bu not, hangi bilginin NEREDE yaşadığını ve hangisinin **otorite (tek doğru kaynak)** olduğunu tek bakışta gösterir. Kod değişince buradaki ilgili tüm yerler BİRLİKTE güncellenmeli.

## Konu bazlı otorite tablosu

| Konu | Otorite (tek doğru kaynak) | Ayrıca tekrar eden/bağımlı yerler | Not |
|---|---|---|---|
| GPIO pin atamaları (Konteyner/ESP32) | `esp32_master/include/config.h` (`#define ..._PIN`) | `esp32_master/docs/pinout.html`, `esp32_master/src/main.cpp` içindeki gömülü "Bilgiler" sekmesi pin tabloları | **En yüksek drift riski** - aynı pin bilgisi 3 yerde. config.h değişince ikisi de kontrol edilmeli. |
| GPIO pin atamaları (Sudepo/ESP8266) | `esp8266_slave/include/config.h` | `esp8266_slave/docs/pinout.html`, `esp8266_slave/README.md` | |
| Nano IO pin planı | `nano_io/README.md` + `nano_io/src/main.cpp` | `esp32_master/src/main.cpp` "Bilgiler" sekmesi (Nano tablosu) | |
| Genel sistem mimarisi (zon ayrımı, RS485 akışı, mod/susturma mantığı) | `SISTEM_MIMARISI.md` (repo kökü) | `sudepo/03 Kararlar/` altındaki ilgili karar notları | Mimari notu güncel tutmak en kolay unutulan - büyük bir davranış değişikliği (ör. siren zamanlaması, gaz alarmı) sonrası kontrol et. |
| Alarm/siren zamanlama sabitleri, mod davranışı | Kod (`alarmLedGuncelle()` / `esp8266_slave` loop() yorumları) | `SISTEM_MIMARISI.md`, `sudepo/03 Kararlar/` | Bu proje boyunca en sık değişen alan. |
| RS485 mesaj formatı/alanları | `esp8266_slave/README.md` (RS485 durum alanları bölümü) | `SISTEM_MIMARISI.md` (özet referans verir) | Yeni RS485 alanı eklenince README güncellenmeli. |
| MPPT/RS232/PI30 bağlantısı | `esp32_master/docs/mppt-baglanti-kilavuzu.html` | `SISTEM_MIMARISI.md`, `sudepo/03 Kararlar/MPPT RS232 PI30.md` (varsa) | |
| Yedek akü kablolaması | `esp32_master/docs/yedek-aku-baglanti-kilavuzu.html` | `sudepo/03 Kararlar/` | |
| Kutu/panel yerleşimi | `esp32_master/docs/kutu-arka-panel.html` | - | Fiziksel/donanımsal, kod değişikliğinden bağımsız. |
| Bilinen sorun-çözüm geçmişi | `sudepo/04 Sorun-Cozum/` | Claude'un kendi hafıza dosyaları (proje türü) | İkisi de var olabilir, ama Obsidian kalıcı/görünür kaynak - hafıza ona referans vermeli, tekrar yazmamalı. |
| Genel proje tanıtımı (kurulum, build) | Repo kökü `README.md` | `esp32_master/README.md`, `esp8266_slave/README.md` | Her alt proje kendi build/flash adımlarını anlatır, kök README genel bakış. |

## Kural: kod değişikliği → doküman senkronu

Bir kod değişikliği aşağıdakilerden birini etkiliyorsa, **aynı oturumda** yukarıdaki tablodan ilgili satırın "Ayrıca tekrar eden" sütunundaki TÜM dosyalar kontrol edilip güncellenir:
- Pin ataması (eklendi/değişti/kaldırıldı)
- Bir sensör/röle/donanımın adı ya da davranışı (ör. "alev sensörü" → "duman dedektörü" rename'i gibi)
- Alarm/mod/zamanlama mantığı
- RS485 mesaj formatı

## En büyük risk noktası (öncelikli iyileştirme adayı)

`esp32_master` tarafında pin bilgisi **3 farklı yerde elle** tutuluyor (config.h, docs/pinout.html, main.cpp içindeki gömülü Bilgiler sekmesi). `esp8266_slave` tarafında zaten `web/index.html`/`web/app.js` → `web_content.h` için otomatik üretim deseni var (`gen_web_content.py`, build öncesi çalışır) - benzer bir yaklaşım (config.h'deki pin tanımlarından pinout.html/Bilgiler tablosunu otomatik üretmek) ileride bu 3'lü tekrarı ortadan kaldırabilir. Şimdilik uygulanmadı - kapsamlı bir refactor gerektirir, kullanıcı isterse ayrı bir iş olarak ele alınabilir.
