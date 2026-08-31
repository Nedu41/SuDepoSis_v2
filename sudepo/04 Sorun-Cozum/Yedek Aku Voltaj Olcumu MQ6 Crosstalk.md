---
tags: [sorun-cozum, konteyner, adc, mq6, yedek-aku]
---

# Yedek Akü Voltaj Ölçümü - MQ6 Crosstalk (GPIO10 → GPIO2)

## Belirti

CN2 konnektörü (AHT10 I2C + MQ6 AOUT sinyali taşıyan) takılıyken Yedek Akü voltaj okuması (GPIO2, ADS1115 değil - ESP32 dahili ADC1) 17V gibi gerçekçi olmayan bir değer veriyordu, normal aralık ~11-12V. CN2 çıkarılınca veya MQ6 modülü sökülünce değer normale dönüyordu.

## Teşhis

Sırayla elenen teoriler:
1. **Pin çakışması** - yok. GPIO2 (Yedek Akü) ile GPIO10/36/42 (CN2: MQ6 AOUT + AHT10 I2C) tamamen ayrı pinler.
2. **Kısa devre** - yok, kablolar kontrol edildi, süreklilik testinde sorun bulunmadı.
3. **AC gürültü/kapasitif spike** - kısmen doğru ama eksik. 100nF, sonra medyan filtre (7 örnek) denendi, MQ6 kapalıyken ADC hâlâ **tam satürasyona (4095, sabit 17.01V)** gidiyordu - medyan bile değiştirmedi çünkü tüm örnekler zaten aynıydı. Bu, tekil spike değil **sürekli bir durum** olduğunu gösterdi.
4. **Kesin kanıt (multimetre)**: MQ6 kapalıyken GPIO2 ucunda doğrudan multimetre ile ölçüm yapıldığında değer **2.4V ile 3.3V arasında, ~yarım saniyede bir salınıyordu** - yani pin gerçekten floating/kararsız, sabit yanlış bir DC değil.

## Kök neden

MQ6 modülü **kapalıyken** (`MQ6_POWER_PIN` LOW, duty-cycle'lı çalışma - çoğu zaman kapalı), `MQ6_ADC_PIN` (GPIO10) üzerindeki dahili pull-down (~45kΩ) + sahada eklenen harici 10kΩ pull-down, MQ6'nın sızıntı/yarı-aktif çıkışını (MOSFET tam kesmiyor olabilir) bastırmaya **yetmiyordu**. GPIO10 floating/kararsız kalıyor, bu salınım CN2 kablo demeti üzerinden (fiziksel yakınlık, kapasitif kuplaj) Yedek Akü'nün yüksek empedanslı gerilim bölücüsüne (127kΩ = 100k+27k, EMI'ye çok duyarlı) sızıyor, ESP32'nin ADC'si bu darbeleri örnekleme anında yakalayıp satürasyona (4095/17V) gidiyordu.

## Çözüm

`main.cpp`: MQ6 kapalıyken `MQ6_ADC_PIN`'i pasif `INPUT_PULLDOWN` yerine aktif `OUTPUT LOW` yap (birkaç ohm'luk gerçek bir GND kenetlemesi, kΩ'larla ölçülen pull-down'dan çok daha güçlü). MQ6 açıldığında tekrar `INPUT_PULLDOWN`'a dönülüyor (`analogRead()` zaten okuma sırasında ADC moduna alıyor).

- `setup()`: pin başlangıçta `OUTPUT LOW` (MQ6 zaten kapalı başlıyor)
- `mq6Poll()`: güç durumu değiştiği anda pin modu da birlikte değiştiriliyor (powered→INPUT_PULLDOWN, kapalı→OUTPUT LOW)

Sahada doğrulandı: MQ6 açık/kapalı fark etmeksizin Yedek Akü okuması artık sabit ~11.6V.

Ayrıca teşhis sürecinde eklenen `[MQ6] guc=ACIK/KAPALI` debug log satırı kalıcı olarak bırakıldı (ileride benzer bir crosstalk şüphesinde işe yarar).

## Ders

- **Pasif pull-down (dahili veya harici direnç) her zaman yeterli değildir** - kaynak (burada MQ6'nın "kapalı" ama tam kesilmemiş çıkışı) yeterince güçlüyse floating pin'i bastıramaz. Aktif `OUTPUT LOW` (gerçek bir sürücü) çok daha güvenilir bir "kesin GND'ye kenetleme" yöntemidir.
- **Multimetre ile ESP32 ADC'nin gördüğü değer FARKLI olabilir** - multimetre DC ortalama/yavaş salınım gösterirken, ADC'nin hızlı örneklemesi anlık darbe/tepe değerini yakalayabilir. "Multimetre normal gösteriyor ama cihaz anormal okuyor" durumunda hemen "yazılım hatası" sonucuna atlamadan, multimetreyi **sorunun aynı anda, cihazın okuduğu zamanda** tekrar tekrar ölçtürmek (tek bakışta değil) gerçek durumu ortaya çıkarabilir - burada ilk multimetre testi "sabit" görünmüştü, ikinci/daha dikkatli test salınımı yakaladı.
- **Medyan/ortalama filtre her gürültü sorununu çözmez** - sorun tekil spike değil sürekli/kalıcı bir durumsa (tüm örnekler bozuksa), filtre işe yaramaz. Filtre eklemeden önce "bu bir spike mi yoksa sürekli mi" ayrımını yapmak zaman kazandırır.
- Aynı konnektör (CN2) üzerinde birden fazla sinyalin "fiziksel routing paylaşımı, elektriksel ilgisi yok" diye dokümante edilmiş olması, gerçekte crosstalk'a karşı garanti değildir - yüksek empedanslı analog hatlar (buradaki gibi) özellikle risklidir.

## İlgili

- [[project_konteyner_sensor_eklentileri]]
- [[project_yedek_aku_sistemi]]
- Bkz. hafıza: Yedek Akü MQ6 GPIO10 Crosstalk ÇÖZÜLDÜ
