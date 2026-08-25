---
tags: [sorun-cozum, analog, mq6, gp2y10, adc]
---

# Analog Sensör Yanlış Okuma Kontrol Listesi (MQ6 / GP2Y10 vakası)

## Belirti

MQ6 (gaz) ve GP2Y10 (duman) sensörleri günlerce şu belirtileri gösterdi: boşta/bağlantısızken rastgele oynayan değerler, bağlıyken sabit 0 veya sabit MAX'ta donuk kalma, gerçek gaz/dumana tepkisizlik, sert cisimle (tornavida/kalem) dokununca yanıltıcı sıçramalar.

## Sırayla bulunan kök nedenler (hepsi aynı anda karışmıştı)

1. **Kablo kopukluğu** — GP2Y10'un Vo (analog çıkış) kablosu (GPIO6) fiziksel olarak kopuktu. Belirti: boşta/sabit ~0.02V, hiçbir teste tepki yok. **Çözüm:** lehimle.
2. **Floating ADC pini gürültü toplar** — bağlantısız/yarım bağlı bir analog giriş, yakındaki kablolardan (WiFi, röle/MOSFET anahtarlaması) parazit toplayıp rastgele küçük değerler üretir. Bu NORMAL bir fizik kuralı, sensör arızası değil.
3. **ESP32'ye özgü pull-down/pull-up tuzağı** — `analogRead()` çağrısı, pini ADC moduna alırken dijital pull-up/pull-down devresini KOPARIYOR. `pinMode(pin, INPUT_PULLDOWN)` okumadan ÖNCE çağrılırsa hiçbir işe yaramaz (hemen ardından gelen `analogRead()` onu siliyor) — **okumadan SONRA** çağrılmalı. Kaynak: [ESP32 Forum](https://www.esp32.com/viewtopic.php?t=17431).
4. **Zayıf sürüşlü sensörlerde pull-down gerilim bölücü gibi davranabilir** — MQ6 (güçlü op-amp/comparator çıkışlı) bu pull-down'dan etkilenmedi, ama GP2Y10'un duman sinyali (mV mertebesinde, Voc+ΔV) küçük olduğundan pull-down'la bastırılmış olabileceği şüphesi doğdu (aynı desen daha önce PIR2'de de yaşanmıştı) — GP2Y10 için pull-down kaldırıldı, MQ6'da bırakıldı.
5. **Yanlış varsayılan LED polaritesi** — GP2Y10'un LED kontrol pini (Sharp Pin 3) datasheete göre **HIGH=KAPALI, LOW=AÇIK** — kodun ilk varsayımı tam tersiydi. Bu tip "aktif-LOW/HIGH" varsayımlarını KODA yazmadan önce üreticinin datasheet'inden doğrula.
6. **En büyük ve gerçek kök neden: LED kontrol kablosu (GPIO18) hiç bağlanmamıştı** — haftalarca "MOSFET modülü aktif-LOW olabilir" gibi teoriler üretildi ama gerçekte MOSFET hiç takılmamıştı, GPIO tamamen havadaydı. **Ders: "acaba yanlış bağlanmış mı" diye teorize etmeden önce "gerçekten bağlı mı" diye sor.**
7. **Yanıltıcı "çalışıyor" testi** — tornavida/kalem gibi PARLAK bir cismi sensörün içine sokmak MAX değer üretiyordu; bu GERÇEK optik algılamayı DEĞİL, çoğunlukla elektriksel temas/parazit ya da (LED gerçekten çalışıyorsa) çok güçlü bir yansımayı gösterir — **gerçek duman/gaz testi olmadan "çalışıyor" sonucuna varma**. (Bu vakada nihayetinde parlak yüzeyin özellikle tepki vermesi gerçek optik çalışmaya işaret etti, ama bu ancak diğer tüm ihtimaller elenince güvenilir bir kanıt oldu.)
8. **Aşırı yoğun test dumanı yanıltıcı olabilir** — oda tamamen dumanla dolunca sensör her yerde MAX okur, bu ARIZA değildir; kontrollü/lokal bir duman kaynağıyla test edilmeli, aksi halde "sürekli max, hiç düşmüyor" diye yanlış teşhis konur.

## Genel teşhis sırası (bir dahaki sefere)

1. Fiziksel süreklilik: multimetreyle her kabloyu (Vo, LED/kontrol, VCC, GND) uçtan uca kontrol et — **her şeyden önce, "gerçekten bağlı mı" sorusunu sor.**
2. Üreticinin datasheet'inden pin fonksiyonlarını (aktif-HIGH mı LOW mu, harici transistör gerekir mi) doğrula, koddaki varsayımı ASLA tahmin etme.
3. Boşta okuma normal mi (Voc gibi bir taban değer mi, yoksa 0/MAX'a mı yapışık) kontrol et.
4. Kontrollü/küçük bir gerçek uyarıcı (az miktarda gaz/duman) ile test et - aşırı doz veya "sert cisimle dokunma" testleri yanıltıcıdır.
5. ESP32 ADC pinlerinde pull-up/down kullanacaksan `analogRead()`'den SONRA uygula.
6. Zayıf çıkışlı sensörlerde pull-up/down eklerken sinyali bastırma riskini göz önünde bulundur, gerçek testle doğrula.

## İlgili

- Bkz. hafıza (Claude): `project_konteyner_sensor_eklentileri` (bu vakanın tam kronolojisi, tarih damgalı)
- Bkz. hafıza (Claude): `feedback_donanim_teorisinden_once_ucuz_kontrol` (ilişkili genel prensip - kod teorisine atlamadan önce ucuz/fiziksel kontrolleri yap)
- [[2026-08-25]] günlüğüne bkz
