# Standart 1 — Pano Teması

Tüm proje arayüzlerinin ortak teması (2026-10-01 onaylandı). Koyu, siyah çerçeveli, eski boyalı elektrik panosu hissi.

- `standart1_pano_temasi.css`: bitmiş CSS bloğu. `/* === PANO TEMASI ... */` ile `/* === /PANO TEMASI === */` arasındadır.
- `uret_standart1.py`: bu bloğu üretir (`python uret_standart1.py`).

Kullanıldığı yerler (blok birebir aynı, biri değişince diğerleri de güncellenir):
- `esp32_master/include/web_ui.h` (Kalburum)
- `esp8266_slave/src/main.cpp` — `PANO_CSS` (PROGMEM, `handleCSS` parça parça gönderir; ESP8266'da String'e eklenmez, RAM'e sığmıyor)

Kurallar:
- Kapalıyken tüm butonlar gri. Durum açıkken `btn-on` ile neon yanar. Ton sınıfları: varsayılan yeşil, `ton-kirmizi` (tehlike), `ton-amber` (uyarı/susturma), `ton-mavi` (bahçe kapısı hareket).
- Açık renk üstüne açık renk kullanılmaz. Emoji ikon yoktur.
- Üst bar sağ: `WiFi: ağ (IP)` ve `vX.Y.Z • Son güncelleme: ...`.
