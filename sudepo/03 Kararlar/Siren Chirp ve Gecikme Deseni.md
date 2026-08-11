---
tags: [karar, alarm]
---

# Siren Chirp ve Gecikme Deseni

## Durum

Kabul edildi

## Bağlam

Bir tetikleyici (kapı/PIR) başladığı anda siren doğrudan sürekli çalmaya başlıyordu - kısa/geçici tetiklenmelerde bile tam gürültü anında devreye giriyordu.

## Karar

Panik hariç, bir tetikleyici episode'u başladığında siren ANINDA sürekli çalmaz: önce sadece 1 saniyelik kısa bir "chirp" çalar, sonra 3. saniyeye kadar sessiz kalır; tetikleyici hâlâ aktifse 3. saniyeden itibaren sürekli çalmaya başlar.

## Gerekçe

Kısa/yanlışlıkla tetiklenmelerde (anlık kapı açılıp kapanması gibi) tam sirenin hemen gürlememesi için - "önce kısa bir uyarı, gerçekten uzun sürüyorsa tam alarm" mantığı, klasik alarm sistemlerindeki "giriş gecikmesi" konseptine benzer.

## Sonuçlar

Her iki zonda simetrik uygulandı: `konteynerSirenEpisodeMs`/`KONTEYNER_SIREN_CHIRP_SURE_MS`/`KONTEYNER_SIREN_GECIKME_SURE_MS` (ESP32), `sirenEpisodeBaslangicMs`/`SIREN_CHIRP_SURE_MS`/`SIREN_GECIKME_SURE_MS` (ESP8266). Lamba bu desenden etkilenmiyor, her zaman anında yanıyor (görsel uyarı gecikmemeli). Ayrıca: siren, panik hariç 2 dakika kesintisiz çalarsa otomatik susturulur (sensör arızası/unutulmuş tetiklenmeye karşı) - Konteyner'de bu ek olarak Telegram bildirimi de gönderir, Sudepo'da ESP8266'nın kendi Telegram entegrasyonu olmadığından sessizce susturulur.
