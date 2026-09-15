---
tags: [sorun-cozum, konteyner, gpio, esp32-s3]
---

# GPIO35 Octal PSRAM Sorunu

## Belirti

Kalburum'a (ESP32-S3-WROOM-1 N16R8) bahçe kapısı zil hoparlörü eklenirken GPIO35 seçildi (touch/strapping/USB/UART0 değil diye "güvenli" sanılmıştı). Sahada LED bağlanınca hiç yanmadı; multimetre GPIO35'te **sürekli 0V** ölçtü - yazılım tarafında doğru komut (`ledcWriteTone`) gönderildiği seri logla kanıtlanmasına rağmen pin hiç toggle olmuyordu.

## Kök neden

Web araştırmasıyla doğrulandı: "R8" veya üstü (R8, R8V, R16) içeren ESP32-S3-WROOM-1 modüllerinde **GPIO33, 34, 35, 36, 37 modülün paketi İÇİNDE Octal PSRAM çipine donanımsal olarak bağlıdır** - modülün dışına hiç çıkmazlar. Bu, firmware'in PSRAM'ı yazılımda etkin/kapalı tutmasından TAMAMEN BAĞIMSIZ bir donanım kısıtı (proje 8MB/DIO/PSRAM'sız çalışıyor olsa bile fark etmiyor - pinler modül içinde fiziksel olarak PSRAM die'ına kablolu).

Kaynak: [ESP32-S3 Pinout Quick Guide](https://github.com/atomic14/esp32-s3-pinouts), [ESP-IDF GPIO dokümantasyonu](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html), [esphome #15907](https://github.com/esphome/esphome/issues/15907).

**Not:** Aynı testte SCART Pin 19'un (GPIO35'in dışarı çıktığı hat) şaseye temas ettiği de sonradan fark edildi - yani ölçülen 0V'nin tamamen bu kısıtan mı yoksa kısmen harici kısa devreden mi kaynaklandığı %100 ayrılamadı. Yine de resmi dokümantasyon net: bu aralık genel kural olarak kullanılmamalı.

## Çözüm

Zil hoparlörü **GPIO48**'e taşındı (dokunma/ADC/strapping/UART0/PSRAM riski olmayan pin). Sahada multimetre + LED testiyle doğrulandı: zile basılınca ~1.5V (PWM ortalaması) göründü, LED yandı.

**GPIO48'in de kendi sorunu çıktı** (ayrı not): ESP32-S3-DevKitC-1 v1.0 kartlarında GPIO47/48, PSRAM'in 1.8V SPI güç hattından besleniyor (normal 3.3V GPIO değil) VE GPIO48 kartın üzerindeki WS2812 RGB LED'in veri hattı. Zil çalınca kartın üzerindeki RGB LED de tepki verdi (kozmetik, işlevi bozmuyor). MOSFET tetiklemek için 1.8V'un yeterli olduğu sahada doğrulandı (MOSFET modülünün ön-sürücüsü muhtemelen bipolar transistör, düşük eşik gerilimi yeterli).

## Ders

ESP32-S3-WROOM-1 N16R8/R8 gibi Octal PSRAM'li modüllerde **GPIO26-37 aralığının TAMAMI** (33-37 dahil, sadece 26-32 değil) yeni bir sinyal için ASLA denenmemeli - modül üstündeki yazı veya "PSRAM'ı yazılımda kapattım" mantığı bunu değiştirmez, kısıt donanımsal. Ayrıca DevKitC-1 gibi geliştirme kartlarında **üzerinde entegre bileşen olup olmadığını** (RGB LED, buton) ve **her pinin gerçekten ana 3.3V rayından mı beslendiğini** yeni bir pin seçmeden önce mutlaka araştır - GPIO48 örneğinde olduğu gibi "boş görünen" bir pin aslında hem meşgul hem farklı bir voltaj rayında olabilir.

## İlgili

- Bkz. hafıza: `project_gpio35_octal_psram_engeli`
- "Fiziksel Acil Buton GPIO14 Touch Pin Sorunu" notu (güncellenmiş pin listesi)
- `esp32_master/include/config.h` "Konteyner Donanimi" pin planlaması yorumu
