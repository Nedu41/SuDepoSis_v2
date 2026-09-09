# SuDepo İzleme (Android WebView App)

Mevcut ESP8266 (Sudepo) ve ESP32 (Konteyner) web arayüzlerini tek Android
uygulamasında gösteren ince bir WebView sarmalayıcı. Sunucu tarafında
hiçbir değişiklik gerekmez.

- Sudepo: `http://sudepo.local`
- Konteyner: `http://kalburum.local`

## Çalıştırma
1. Android Studio ile `android_app` klasörünü aç (File > Open).
2. Gradle sync bitince telefonunu bağla (USB hata ayıklama açık) veya bir
   emülatör seç, ▶ Run'a bas.
3. Telefon, ESP'lerle aynı WiFi ağında olmalı (mDNS aynı ağ içinde çalışır).

Üstteki "Sudepo" / "Konteyner" butonlarıyla arayüzler arasında geçiş
yapılır, son seçilen sekme hatırlanır. Aşağı çekince sayfa yenilenir.
