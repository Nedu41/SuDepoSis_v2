---
tags: [sorun-cozum, konteyner, wifi]
---

# WiFi STA Reconnect Eksikliği

## Belirti

Konteyner ESP32'de "web sayfası bazen açılmıyor" şikayeti tekrarlıyordu. Kritik ayrım: **sistem çalışıyordu** (panik butonu/RS485 gibi WiFi'ye ihtiyaç duymayan işlevler sorunsuzdu), sadece web arayüzüne erişilemiyordu.

## Teşhis

`ping <cihaz-ip>` router'dan "Destination host unreachable" döndürdü - cihaz ağda hiç görünmüyordu. Aynı anda seri monitörde RS485 logları kesintisiz akıyordu (loop() çalışıyor, sadece WiFi kopmuş).

## Kök neden

`wifi_connect()` (ve içindeki `wifiMulti.run()`) **sadece `setup()`'ta bir kez** çağrılıyordu. `loop()`'ta WiFi durumunu kontrol edip yeniden bağlanmayı deneyen hiçbir kod yoktu. Router kesintisi/sinyal düşmesi/DHCP lease sorunu gibi bir nedenle STA bir kez koparsa, cihaz **elle resetlenene kadar** web arayüzüne bir daha asla erişilemiyordu.

## Çözüm

`wifiReconnectPoll()` eklendi, `loop()`'ta her zaman çalışıyor - `WiFi.status() != WL_CONNECTED` ise 15sn'de bir `WiFi.reconnect()` dener (asenkron, bloke etmez).

Aynı oturumda ayrıca: `weatherKontrolEt()`'e `WEATHER_BOOT_GRACE_MS` (15sn) eklendi - boot sonrası WiFi bağlanır bağlanmaz atılan senkron HTTPS hava durumu isteğinin `loop()`'u bloke etme riski azaltıldı (ikincil katkıda bulunan neden olabilirdi, ana neden değildi).

## Ders

"Sistem çalışıyor ama bir şeye erişilemiyor" tipi belirsiz şikayetlerde önce **hangi alt-sistemlerin gerçekten çalıştığını** ayırt et (burada: RS485/panik WiFi'den bağımsız, çalışıyordu → sorun WiFi katmanında). `ping`/ağ seviyesinde test, kod okumadan önce kök nedeni hızlıca daralttı - [[project_mqtt_loop_blok_web_sayfasi|MQTT loop blok]] vakasıyla aynı ders sınıfı: loop()'ta gizli bir "bir kere çalışır, kopunca geri gelmez" varsayımı.

## İlgili

- Bkz. hafıza: WiFi STA Reconnect Eksikliği ÇÖZÜLDÜ
- `esp8266_slave` tarafında da benzer bir eksiklik olabilir, kontrol edilmedi - benzer şikayet gelirse orayı da incele.
