#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// SuDepoSis v2 - Ana Konfigürasyon
// ============================================================

// ===== Gizli Bilgiler (WiFi/OTA/MQTT sifreleri) =====
// secrets.h git disinda tutulur (.gitignore). Ilk kurulumda
// secrets.h.example dosyasini secrets.h olarak kopyalayip
// gercek degerlerini gir.
#include "secrets.h"

// ===== WiFi Ayarları =====
#define WIFI_STATIC_IP_OCTET_1 192
#define WIFI_STATIC_IP_OCTET_2 168
#define WIFI_STATIC_IP_OCTET_3 1
#define WIFI_STATIC_IP_OCTET_4 50
#define WIFI_GATEWAY_OCTET_1 192
#define WIFI_GATEWAY_OCTET_2 168
#define WIFI_GATEWAY_OCTET_3 1
#define WIFI_GATEWAY_OCTET_4 1
#define WIFI_SUBNET_OCTET_1 255
#define WIFI_SUBNET_OCTET_2 255
#define WIFI_SUBNET_OCTET_3 255
#define WIFI_SUBNET_OCTET_4 0
#define WIFI_DNS1_OCTET_1 8
#define WIFI_DNS1_OCTET_2 8
#define WIFI_DNS1_OCTET_3 8
#define WIFI_DNS1_OCTET_4 8
#define WIFI_DNS2_OCTET_1 1
#define WIFI_DNS2_OCTET_2 1
#define WIFI_DNS2_OCTET_3 1
#define WIFI_DNS2_OCTET_4 1
#define MDNS_NAME "kalburum"

// ===== OTA Ayarları =====
#define OTA_HOSTNAME "kalburum"

// ===== AP Mod Ayarları =====
#define AP_SSID "kalburum"
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

// ===== MQTT Ayarları =====
#define MQTT_BROKER "127.0.0.1"  // Lokal Mosquitto broker
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "sudeposis-master"

// MQTT Topics
#define MQTT_TOPIC_LEVEL "sudeposis/level"
#define MQTT_TOPIC_ESP8266 "sudeposis/esp8266/status"
#define MQTT_TOPIC_NANO "sudeposis/nano/status"
#define MQTT_TOPIC_ALARM "sudeposis/alarm"
#define MQTT_TOPIC_COMMAND "sudeposis/command"

// ===== RS485 Ayarları =====
// ESP8266 slave SoftwareSerial için 9600 baud seçildi.
// NOT: ESP32-S3'te UART pinleri (klasik ESP32'nin aksine) sabit degil - GPIO
// matrisi uzerinden Serial1.begin() cagrisinda hangi pin verilirse o kullanilir
// (bkz main.cpp). Bu yuzden RX/TX/DE pinleri, karttaki fiziksel kablolamaya en
// uygun herhangi bir SERBEST GPIO'ya (asagidaki Konteyner Donanimi bolumundeki
// listeye bkz) tasinabilir - donanimsal/yazilimsal bir kisit yok. Kartin sag-
// orta bolgesine denk gelen GPIO37/38/39 secildi (kullanicinin tercihi).
#define RS485_BAUDRATE 9600
#define RS485_RX_PIN 37      // GPIO37 - MAX485 RO (Alici cikisi -> ESP32 RX)
#define RS485_TX_PIN 38      // GPIO38 - MAX485 DI (Verici girisi <- ESP32 TX)
#define RS485_DE_PIN 39      // GPIO39 - MAX485 DE/RE (Verici Enable)
#define RS485_UART_NUM 1     // UART1

// ===== MPPT (RS232 / PI30-Voltronic protokolu) Ayarlari - "victor NML 3200-24" =====
// Sahada olculdu (2026-08): Bu cihazin RJ45 "COM" portu RS485 DEGIL, RS-232 -
// datasheet "Iletisim Arayuzu: RS232/WIFI" diyor, kullanici klonunun kutu
// icindeki serigrafisi de 12 numarali portu "RS-232 communication port" olarak
// isaretliyor. Menu kisaltmalari (USB/SBU/SUB cikis onceligi, AGM/FLD/LIB
// batarya tipi) Voltronic/Axpert ailesiyle birebir eslesiyor - protokol PI30
// (ASCII komut-cevap + CRC16), Modbus RTU DEGIL. Onceki Modbus/register
// yaklasimi bu yuzden TAMAMEN degistirildi - A/B/Modbus register'i YOK.
// Kaynak: "Axpert MKS II&MKS III&MKS IV RS232 Protocol" (Voltronic resmi
// dokumani, bkz sudepo/05 belgeler/ altindaki datasheet'ler).
//
// Servisten alinan resmi CN1 ("RS232.WIFI") sema fotosuyla DOGRULANDI
// (2026-08-14, bkz sudepo/ altindaki WhatsApp fotolari) - RJ45 pin haritasi
// (T568B renk sirasi, pin1=turuncu-beyaz):
//   pin1 turuncu-beyaz = RXD (WiFi modulunun kendi RX'i - yani inverter
//                         mainboard'unun TX ciktisi, bize gore MPPT->ESP32)
//   pin2 turuncu       = TXD (WiFi modulunun kendi TX'i - mainboard'un RX
//                         girisi, bize gore ESP32->MPPT)
//   pin3,5,6,7         = NG (kullanilmiyor)
//   pin4 mavi          = +12V VCC - BAGLAMA, WiFi modulune guc icin,
//                         MAX3232/ESP32'ye baglanirsa yakabilir.
//   pin8 kahverengi    = GND
// NOT: 2026-08-13 sahada yapilan ilk multimetre olcumu (GND=pin1,
// +13V=pin8) bu resmi semayla TERS cikti - ev yapimi RJ45 breakout'ta pin
// sayma yonu (klips yukari/asagi) karismis olmali. Resmi sema (servisten,
// bu porta ozel CN1 etiketli) esas alindi; sahada kablolarken RJ45'i T568B
// standardina gore (klips SIZE dogru, pin1 SOLDA) sayarak dogrulayin.
//
// Donanim: RS485/MAX485 modulu bu hat icin GECERSIZ - RS232-TTL cevirici
// (MAX3232/HW-390 gibi) gerekir, DE/RE (yon) pini yok (RS232 full-duplex).
// GPIO40/41/42'deki eski MAX485 modulu SOKULDU (2026-08-14) - MAX3232
// kullanicinin tercihiyle bu pinlere (40/41) tasindi, GPIO42 hala serbest
// (RS232'de DE/RE yok, ihtiyac yok).
// ONEMLI: MAX3232 modullerinin TTL tarafi silkscreen'de "TX"/"RX" diye
// etiketlenir ama bu etiketler URETICIYE GORE DEGISIR (bazilari "cipin
// kendi TX/RX'i", bazilari "MCU'ya gore TX/RX" anlaminda kullanir) - bu
// yuzden BURADA VE TUM DOKUMANLARDA cipin kendi (belirsizlik olmayan)
// datasheet pin adlari kullanilir:
//   R1OUT = TTL cikis (RS232'den alinan veri buradan CIKAR)  -> ESP32 RX
//   T1IN  = TTL giris (ESP32'nin gonderecegi veri buraya GIRER) <- ESP32 TX
// Modulunuzdeki etiket ne yaziyorsa yazsin, ISLEVE gore baglayin.
// 2026-08-15 saha loopback testinde fiziksel kablolamanin R1OUT/T1IN'e gore
// TERS oldugu ortaya cikti (bkz test/mppt_loopback_test.cpp); lehimle
// ugrasmamak icin fiziksel telleri degistirmek yerine burada RX/TX GPIO
// atamasi yer degistirildi - GERCEKTE R1OUT->GPIO41, T1IN<-GPIO40 baglidir.
#define MPPT_UART_RX_PIN 41     // GPIO41 - MAX3232 R1OUT -> ESP32 RX (UART2)
#define MPPT_UART_TX_PIN 40     // GPIO40 - ESP32 TX -> MAX3232 T1IN (UART2)
#define MPPT_UART_NUM 2         // UART2
#define MPPT_BAUDRATE 2400      // PI30 protokolu sabit: 2400 8N1
#define MPPT_POLL_INTERVAL_MS 5000
#define MPPT_STALE_MS 15000
// QPIGS cevabi ~117 byte - 2400 baud'da SADECE aktarimi ~490ms suruyor
// (117*10bit/2400baud). Eski deger (500ms) bu yuzden sinirda kaliyordu,
// en ufak gecikmede QPIGS zaman asimina ugruyordu (QMOD/QPIWS kisa
// olduklari icin sorun cikarmiyordu - sahada 2026-08-14 fake_mppt.py
// testiyle tespit edildi). Rahat bir pay icin buyutuldu.
#define MPPT_RESPONSE_TIMEOUT_MS 1200

// ===== Yedek Aku (ADC voltaj izleme + gunduz-only sarj kontrolu) =====
// 3x 12V 20Ah kullanilmis ("cikma eski") scooter akusu, HEPSI PARALEL
// baglanarak tek bir 12V ~60Ah yedek banka olusturur (3'u ayni anda sarj
// edilebilsin diye seri degil paralel - kullanicinin tercihi). Bu banka
// SADECE dusuk guclu alarm elektroniginin (ESP32 + role mantigi) 5V
// hattina, TEK YONLU bir guc diyotu (diyot-OR) uzerinden yedek olarak
// baglanir - ana 24V hatta veya sulama pompasina DEGIL (kapasite/saglik
// bilinmedigi icin bilincli olarak dar tutuldu). Diyot yonu: yedek aku
// SADECE 5V hattı sarktiginda devreye girer, o hattan asla akim CEKMEZ.
//
// SARJ (aktif, sistem kontrollu): Ana 24V hattan bir role/MOSFET
// uzerinden bu 12V bankaya sarj yapilir, YEDEK_AKU_SARJ_PIN ile ESP32
// tarafindan acilir/kapatilir. Gunduz/gunes var mi sorusu, RS232/PI30
// uzerinden zaten okunan mpptData.pv_power kullanilarak cevaplanir - ayri
// bir RTC/saat mantigina GEREK YOK. PV uretim esiğinin ALTINDAYSA (gece
// veya bulutlu/karanlik) role KAPALI kalir ki ana batarya geceleri bu
// yuzden ekstra tuketilmesin. MPPT verisi bayat/okunamiyorsa FAIL-SAFE:
// role KAPALI kalir (sarj etmemek, gunes olup olmadigini bilmeden ana
// bataryayi riske atmaktan daha guvenli).
//
// 12V (gercekte sarj sirasinda ~10.5-15V araligi) hat, dirençli bir
// gerilim bolucu ile ESP32 ADC'nin 0-3.3V araligina indirilir. Ornek oran
// (~1/4.7, 1% toleransli dirençlerle): R1(ust)=100kOhm, R2(alt)=27kOhm ->
// 15V girişte ADC ucunda ~3.2V (guvenli marj). YEDEK_AKU_ADC_OLCEK gercek
// dirençlere ve sahada multimetreyle yapilan kalibrasyona gore DOGRULA/duzelt.
#define YEDEK_AKU_ADC_PIN 2         // GPIO2 - ADC1 kanali (ADC2 WiFi aktifken guvenilmez)
#define YEDEK_AKU_ADC_OLCEK 4.70f   // (R1+R2)/R2 = (100+27)/27, teorik bolucu orani
// Sahada multimetreyle 3 noktada (3.625/5.115/12.18 V) kalibre edildi.
// Sabit ofset cikarma orta/ust bolgeyi bozdugundan (ilk denemede) terk
// edildi - saf kazanc carpani 3 noktaya cok daha iyi oturuyor (force-origin
// en kucuk kareler fit). Bosta ~0.2V ADC tabani icin ayri olu-bolge esigi
// kullaniliyor (asagida), boylece gercek okumalar bozulmuyor.
#define YEDEK_AKU_KALIBRASYON_KAZANC 1.097f
#define YEDEK_AKU_OLU_BOLGE_V 0.3f  // Bu ham voltajin altinda 0V goster (ADC bosta tabani)
#define YEDEK_AKU_POLL_INTERVAL_MS 5000
#define YEDEK_AKU_DOLU_V 12.8f      // Bu voltajin ustu: "Dolu" (12V kursun-asit, dinlenme voltaji)
#define YEDEK_AKU_ZAYIF_V 11.5f     // Bu voltajin altı: "Zayif" (sarj/degisim gerekir, derin desarjdan kacinilir)
// GPIO21 ESKIDEN yedek aku sarj rolesi/MOSFET tetiklemesiydi (bkz git
// gecmisi) - Schulzz marka PWM solar sarj kontrolcusu bu isi artik kendi
// basina (donanimsal) yaptigindan bu mantik TAMAMEN KALDIRILDI (2026-08-24).
// Ayni GPIO21 + ayni fiziksel MOSFET modulu simdi "Acil Durum Lambasi"
// icin yeniden kullaniliyor (bkz asagidaki ACIL_LAMBA_PIN) - MOSFET'in yuk
// tarafina artik sari RCA uzerinden 12V veriliyor, cikisi acil durum
// lambasina gidiyor.
#define ACIL_LAMBA_PIN 21           // GPIO21 - Acil Durum Lambasi MOSFET tetikleme cikisi (eski sarj rolesi pini)

// ===== Ana Guc (24V ana hat) Izleme - ADS1115 (I2C, harici ADC) =====
// ADC1 (GPIO1-10) tamamen dolu (GPIO2/6/10 - bkz Konteyner Ek Sensorler),
// ADC2 WiFi aktifken guvenilmez - bu yuzden ana akunun 3 kademeli
// ayarlanabilir esigi icin ayri bir I2C ADC modulu (ADS1115, envanterde
// zaten mevcut) eklendi. Mevcut AHT10 I2C hattina (SDA=GPIO36, SCL=GPIO42)
// bindirilir - farkli adres (0x48) oldugu icin AHT10 (0x38) ile cakismaz,
// yeni GPIO harcanmaz. Sadece ana aku icin kullaniliyor (MQ6/GP2Y10
// bilerek native ESP32 ADC'de birakildi - GP2Y10'un 320us'lik hizli darbe
// olcumu ADS1115'in cevrim suresine gore cok riskli olurdu).
#define ADS1115_I2C_ADDR 0x48
#define ADS1115_KANAL 0             // AIN0 (single-ended, GND'ye gore) - ana aku bolucu cikisi buraya bagli
// Ana hat 24V nominal (2x12V seri jel aku), sarj sirasinda ~30V'a kadar
// cikabilir. Bolucu: R1(ust)=100kOhm, R2(alt)=10kOhm -> oran=(100+10)/10=11,
// 30V girişte ADS1115 ucunda ~2.73V (3.3V beslemeye gore guvenli marj).
// SAHADA GERCEK DIRENC DEGERLERIYLE VE MULTIMETREYLE DOGRULA/kalibre et
// (YEDEK_AKU_KALIBRASYON_KAZANC deseni gibi).
#define ANA_GUC_BOLUCU_ORAN 11.0f
#define ANA_GUC_POLL_INTERVAL_MS 5000
// 3 kademeli varsayilan esikler (NVS'de kalici, web'den ayarlanabilir -
// konteynerGazEsikVolt ile ayni desen). Sadece BILDIRIM icin kullanilir,
// hicbir role/yuk OTOMATIK kesilmez/acilmaz (kullanici karari: "ihtiyac
// duyarsam acarim", acil lamba manuel butonla veya panik/alarm modunda
// otomatik yanar - bkz main.cpp acilLambaGuncelle()).
#define ANA_GUC_ESIK1_V 23.5f       // Kademe 1 "Dusuk" - bilgi amacli
#define ANA_GUC_ESIK2_V 22.0f       // Kademe 2 "Kritik" - daha guclu uyari
#define ANA_GUC_ESIK3_V 21.0f       // Kademe 3 "Acil" - en kritik uyari

// ===== Konteyner Ek Sensorler (AHT10 + MQ6 + Duman Dedektoru) =====
// AHT10: sicaklik/nem, I2C - kutuphane KULLANILMAYIP (proje deseni: ModbusMaster
// yerine elle CRC16, IRremote yerine ham kenar yakalama gibi) Wire.h ile elle
// yazilir: tetikle (0xAC 0x33 0x00), 80ms bekle, 6 byte oku, 20-bit ham
// sicaklik/nem hesapla. Konteyner'e OZEL yeni alanlar (sensorData.temperature
// ESP8266'dan gelen, hep 0.0 gonderilen bir yer tutucu - KARISTIRILMAZ,
// dokunulmaz).
#define AHT10_SDA_PIN 36    // GPIO36 - kartin sag tarafi
#define AHT10_SCL_PIN 42    // GPIO42 - kartin sag tarafi
#define AHT10_I2C_ADDR 0x38
#define AHT10_POLL_INTERVAL_MS 5000

// MQ6 (gaz sensoru) - artik alarma BAGLI (bkz main.cpp konteynerGazVar
// kullanimi). ADC1 kanali (ADC2 WiFi aktifken
// guvenilmez). MQ6'un isitici elemani surekli guclendiginde onemli akim
// cekiyor (gece batarya tuketimi icin kritik) - bu yuzden GUC DONGUSU
// uygulanir: MQ6_POWER_PIN'den transistor/MOSFET uzerinden her
// MQ6_CYCLE_MS'de bir MQ6_POWER_ON_MS kadar guc verilir, olcum alinir,
// sonra tekrar kesilir (bkz main.cpp mq6Poll()).
#define MQ6_ADC_PIN 10              // GPIO10 - ADC1 kanal 9
#define MQ6_POWER_PIN 16            // MOSFET/transistor modulunun sinyal ucu - MQ6 VCC hattini anahtarlar
#define MQ6_POWER_ON_MS (60UL * 1000UL)        // guc verildikten sonra acik kalma (isinma+olcum) suresi
#define MQ6_CYCLE_MS (10UL * 60UL * 1000UL)    // dongu periyodu - bu surenin MQ6_POWER_ON_MS'i acik, kalani kapali
#define MQ6_POLL_INTERVAL_MS 500 // 2026-08-25: 5000'den dusuruldu - kullanici modulun kendi LED'i aninda yanarken alarmin 3-5sn gec gelmesinden sikayetci oldu, kok neden bu araligin uzunlugu idi

// EFS-903R (kuru kontak röleli duman dedektörü) sahada test edilip
// çalıştırılamadı (muhtemelen arızalı) - yerine Sharp GP2Y1014AU0F/GP2Y1010AU0F
// (optik/ışık-saçılımlı toz+duman sensörü) geçildi (2026-08-20, kullanıcı
// kararı, bkz [[project_konteyner_sensor_eklentileri]]). Bu sensör, gerçek
// fotoelektrik duman dedektörleriyle AYNI prensiple çalışır (kuru kontak
// DEĞİL, LED darbesi + ADC okuma) - MQ6 gibi ADC1 kanalı ister (ADC2 WiFi
// aktifken güvenilmez). ADC1 (GPIO1-10) tamamen doluydu - PIR2_PIN GPIO6'dan
// GPIO17'ye TAŞINDI (kullanıcı sahada PIR2 kablosunu fiziksel olarak taşıyacak),
// boşalan GPIO6 GP2Y10'un analog çıkışına (Vo) verildi.
//
// Darbe zamanlaması Sharp datasheet'ine göre: LED'i GP2Y10_LED_PIN üzerinden
// (MOSFET modülüyle) HIGH yap, GP2Y10_SAMPLE_DELAY_US bekle, ADC oku, toplam
// GP2Y10_LED_PULSE_US dolunca LED'i LOW yap. Bu tek ölçüm ~320us sürer, loop()'u
// bloklaması ihmal edilebilir (HC-SR04 tetik darbesiyle aynı mantık).
#define GP2Y10_LED_PIN 18            // GPIO18 - LED sürücü MOSFET modülünün sinyal ucu
#define GP2Y10_ADC_PIN 6             // GPIO6 - ADC1 kanal 5 (PIR2'den boşaltıldı), sensörün Vo çıkışı
#define GP2Y10_LED_PULSE_US 320      // LED'in toplam açık kalma süresi (datasheet: 320us ±20us)
#define GP2Y10_SAMPLE_DELAY_US 280   // LED açıldıktan sonra ADC örneklemesi için bekleme (sinyal bu noktada stabil)
#define GP2Y10_POLL_INTERVAL_MS 2000 // İki ölçüm arası bekleme - ısıtıcı olmadığından MQ6 gibi güç döngüsüne gerek yok

// TEST (2026-08-25, GEÇİCİ): Boşta Vo ~0.02V'ye yapışık kalıyor, kullanıcı
// LED'in hiç yanmadığından şüpheleniyor - GP2Y10_LED_PIN'i doğrudan LED değil
// bir MOSFET modülü sürüyor, o modül "aktif-LOW" tipteyse mantığımız TERS
// çalışır (biz "ölç" dediğimiz anda aslında LED'i SÖNDÜRÜYOR olabiliriz).
// Bu anahtarı 1 yapıp reflash edip smoke testi tekrarlayın - eğer LED gerçekten
// tersse artık raw'da anlamlı bir sıçrama görülmeli. İşe yaramazsa 0'a geri al.
// 2026-08-25 ARASTIRMA: Sharp'in resmi pin fonksiyonuna gore LED kontrol
// pini (sensorun kendi pin3'u) HIGH=LED KAPALI, LOW=LED ACIK (bizim ilk
// varsayimimizin TERSI). Ilk polarite testi (asagidaki bayrak =1 yapilip
// denenmisti) GPIO6 kablosu HALA KOPUKKEN yapilmisti - o yuzden gecersizdi.
// Kablo lehimlendikten SONRA bu TEKRAR test ediliyor. Kaynak:
// microcontrollerslab.com GP2Y1014AU0F pinout: "Pin 3: HIGH=IR LED off,
// LOW=IR LED on".
#define GP2Y10_LED_AKTIF_LOW 1        // 0 = mevcut (HIGH=yak), 1 = ters (LOW=yak) - TEST icin degistirin

// ===== Batarya Kapasitesi (kalan kullanim suresi tahmini icin) =====
// 2x12V 100Ah jel aku SERI baglanip 24V, 100Ah'lik tek bir hat olusturuyor
// (README - "2x12V 100A jel aku"). DOGRULA - farkliysa sadece burasi degisir.
#define BATTERY_CAPACITY_AH 100.0f
#define BATTERY_NOMINAL_VOLTAGE 24.0f
#define BATTERY_CAPACITY_WH (BATTERY_CAPACITY_AH * BATTERY_NOMINAL_VOLTAGE)

// ===== Konteyner Donanimi (IR alici, alarm LED, PIR) =====
// ESP32-S3-DevKitC-1 (N8, PSRAM yok) pin planlamasi - ileride yeni eklenti
// eklerken cakisma olmasin diye:
//   KULLANILAN:    GPIO37, GPIO38, GPIO39 (RS485 - ESP8266'ya)
//                  GPIO4, GPIO5, GPIO7, GPIO8, GPIO9 (asagida - IR/LED+Buzzer/Reed/Siren/Lamba)
//                  GPIO40, GPIO41 (yukarida - MPPT RS232/PI30, MAX3232 - eski
//                    MAX485'in yerinde, 2026-08-14'te sokulup buraya tasindi)
//                  GPIO2 (yukarida - Yedek Aku ADC voltaj bolucu girisi, ADC1 kanali)
//                  GPIO21 (yukarida - Acil Durum Lambasi MOSFET cikisi, eski sarj rolesi pini)
//                  GPIO36, GPIO42 (yukarida - AHT10/ADS1115 I2C SDA/SCL)
//                  GPIO10 (yukarida - MQ6 analog giris, ADC1 kanali)
//                  GPIO6 (yukarida - GP2Y10 duman/toz sensoru analog cikisi, ADC1 kanali -
//                    2026-08-20'de PIR2'den boşaltıldı, bkz GP2Y10_ADC_PIN yorumu)
//                  GPIO17 (asagida - PIR2, GPIO6'dan buraya TASINDI)
//                  GPIO18 (yukarida - GP2Y10 LED surucu kontrolu)
//                  GPIO13 (asagida - Swan Quad PET PIR NC/COM kontagi, 2026-08-25'te
//                    GPIO15'ten buraya TASINDI - SCART pin8/13'lu konnektor pin9 uzerinden
//                    disari cikacak, artik "amaci belirsiz" degil)
//                  GPIO14 (asagida - Fiziksel Acil Durum butonu / gercek panik tetigi)
//                  GPIO12 (asagida - Sari RCA, amaci henuz belirlenmedi ama pin ayrildi)
//   ASLA KULLANMA: GPIO0, GPIO3, GPIO45, GPIO46 (strapping/boot pinleri)
//                  GPIO26-32 (bu karttaki Quad Flash icin ayrilmis)
//                  GPIO1, GPIO3 (UART0 - USB debug seri portu, bkz platformio.ini)
//   SERBEST (gelecekteki eklentiler icin): GPIO11, GPIO15, GPIO33, GPIO34
//                  (GPIO40/41 artik MPPT/MAX3232'de, GPIO16/17/18 artik MQ6/PIR2/GP2Y10'da,
//                  GPIO42/36 artik AHT10/ADS1115 I2C'de, GPIO14 Acil Buton'da, GPIO12 Sari
//                  RCA'da, GPIO13 artik Swan PIR'da kullaniliyor - GPIO15 bosaldi)
//   Ekran (ileride): I2C ekran (SSD1306/SH1106 OLED gibi) icin YENI PIN
//     GEREKMEZ - mevcut AHT10/ADS1115 I2C hattina (GPIO42=SDA, GPIO36=SCL)
//     farkli adresle bindirilebilir. SPI ekran (CS/DC/RST/SCK/MOSI - 5 sinyal)
//     icin yukaridaki 4 serbest pin (11,15,33,34) yeterli.
#define IR_RECV_PIN 4     // GPIO4 - IR alici modulunun OUT/sinyal ucu (VCC/GND dogrudan besleme)
#define ALARM_LED_PIN 5   // GPIO5 - Kirmizi LED (+ seri direnc) VE buzzer PARALEL bagli, ayni sinyali paylasir (pin tasarrufu - ikisinin akimi GPIO limitinin altinda kalir) - kucuk/yerel sesli-gorsel isaret
#define PIR2_PIN 17       // GPIO17 - Konteynerdaki PIR hareket sensorunun OUT ucu (2026-08-20: eski GPIO6'dan tasindi, GP2Y10 sensorune ADC1 kanali acmak icin - kullanici sahada kabloyu fiziksel olarak tasidi)
#define KAPI_REED_PIN 7   // GPIO7 - Konteyner kapisi reed (manyetik) switch, digital (INPUT_PULLUP)
// Swan Quad PET PIR (ticari alarm dedektoru) - NC/COM role kontagi, reed
// switch ile AYNI mantik (INPUT_PULLUP): kontak kapaliyken (hareket/kurcalama
// yok) pin GND'ye baglanip LOW okunur, kontak acilinca (hareket VEYA kablo
// kesilirse/guc giderse - NC'nin fail-safe avantaji) pin pull-up ile HIGH'e
// cikar. 12V besleme SIREN/LAMBA ile AYNI harici hattan verilir, ESP32'nin
// 3.3V/5V ciktilarindan DEGIL. 2026-08-25: heniz fiziksel cikisi verilmemisti,
// GPIO15'ten GPIO13'e tasindi - SCART Pin 8 (Turuncu/Beyaz, 13'lu ic
// konnektorun pin9'u) uzerinden disari cikarilacak, bkz docs/pinout.html.
#define SWAN_PIR_PIN 13   // GPIO13 - Swan Quad PET PIR NC/COM kontagi
// Siren/Lamba: ALARM_LED_PIN'den AYRI, gercek role modulu uzerinden calisan
// donanim (Sudepo Zonu'ndaki Nano "Alarm Rolesi"/"Depo Ici Lamba Rolesi" ile
// ayni role). Varsayilan HIGH=aktif - role modulunuz aktif-LOW ise (kablolama
// tersse) main.cpp'de alarmLedGuncelle() icindeki digitalWrite satirlarini
// ters cevirmeniz yeterli (reed switch'teki gibi tek satirlik duzeltme).
#define KONTEYNER_SIREN_PIN 8   // GPIO8 - Alarm sireni rolesi (Sesli/Onayli+Sesli-onay'da aktif)
#define KONTEYNER_LAMBA_PIN 9   // GPIO9 - Uyari lambasi/flasoru rolesi (siren ile birlikte VEYA Onayli+"Sessiz(Lamba)" onayinda tek basina aktif)
// GPIO14 - Fiziksel Acil Durum Butonu (INPUT_PULLUP, butona basinca GND'ye
// kisa devre). 2026-08-25'e KADAR acilLambaManuel'i toggle ediyordu; kullanici
// bunun mantiksiz oldugunu belirtti ("emergency buton dediğim panik butonun
// fiziksel olanı") - artik basisinda GERCEK panigi (alarmStatus.panic_mode,
// web/IR Panik butonuyla AYNI panikTetikle() cagrisi) tetikler. Acil Durum
// Lambasi'nin manuel ac/kapa ozelligi KALDIRILMADI, sadece artik SADECE web
// arayuzunden erisilebilir (bkz main.cpp acilLambaManuel).
#define ACIL_BUTON_PIN 14
#define KONTEYNER_ACIL_BUTON_COOLDOWN_MS 2000 // gercek panikTetikle() cagrilari arasi min sure - gevsek/gurultulu pin RS485'i art arda bloke etmesin
// PLANLANAN (2026-08-25): Buton disariya SCART Pin 17 (Yesil) uzerinden cikarilacak,
// GND donusu Pin 21 (Shield/GND) - GPIO14 degismiyor, sadece kablo yolu SCART'a tasiniyor.
// bkz docs/pinout.html SCART tablosu.

// Tetik animasyonu (2026-08-25, kullanici talebi): GAZ HARIC (patlayici gaz
// icin gecikme guvenlik riski, eskisi gibi aninda/surekli kalir) her alarm
// tetiklenmesinde (panik dahil, kapi/PIR/Swan PIR/duman) lamba ilk birkac
// saniye pirpir eder, siren ise bir sure sessiz kaldiktan sonra kisa
// atislarla calmaya baslar - tetik surdukce bu atis paterni TEKRARLAR (eski
// kademeli gecikme/chirp/bekleme/aktif deseninin YERINI ALDI, bkz main.cpp
// alarmLedGuncelle).
#define KONTEYNER_TETIK_LAMBA_FLASH_TOPLAM_MS 3000   // lamba ilk bu kadar sure pirpir eder
#define KONTEYNER_TETIK_LAMBA_FLASH_PERIYOT_MS 1000  // her pirpir cevrimi (yanik+sonuk) - 3000/1000=3 kez
#define KONTEYNER_TETIK_SIREN_GECIKME_MS 10000       // siren ilk bu kadar sessiz kalir
#define KONTEYNER_TETIK_SIREN_ATIS_MS 300            // her atisin acik kalma suresi
#define KONTEYNER_TETIK_SIREN_ARALIK_MS 3000         // atislar arasi sessizlik

// Sari RCA (arka panel, 13'lu ic konnektor pin 4) - amaci henuz belirlenmedi,
// pinMode input/output yonu belirlenince eklenecek.
#define SARI_RCA_PIN 12   // GPIO12 - amaci henuz belirlenmedi

// NOT: ESP8266 status satiri (alarm mod/mute/pending alanlari eklendikten
// sonra) ~270 karaktere ulasti. 9600 baud'da bu ~280ms surer - eski
// 200ms/500ms degerleri mesaji ortadan kesiyor, "anlik degerlerin sifirlanip
// duzelmesi" buradan kaynaklaniyordu. RS485_TIMEOUT_MS bu yuzden buyutuldu.
// RS485_UPDATE_INTERVAL ilk seferde 1000ms'e cikarilmisti ama bu, tam okuma
// artik guvenilir calisirken (~300-350ms/dongu) gereksiz yere yavas kaldi -
// 600ms hem gecikmeyi geri toparliyor hem de donguler arasinda cakismaya
// yetecek pay (~250ms) birakiyor.
#define RS485_TIMEOUT_MS 400
#define RS485_UPDATE_INTERVAL 600

// ===== Depo Ayarları =====
#define TANK_EMPTY_CM 190.0
#define TANK_FULL_CM 15.0
#define TANK_CAPACITY_LITERS 3000.0
#define TANK_HORIZONTAL 1    // 1 = Yatay silindir, 0 = Dikey

// ===== Alarm Eşikleri =====
#define ALARM_LEVEL_PERCENT 15.0
#define ALARM_LEAK_THRESHOLD_MINUTES 60
#define ALARM_ROLE_ACTIVE 1  // NC röle

// ===== Zaman Ayarları =====
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 6
#define DAY_MEASUREMENT_INTERVAL 60    // saniye
#define NIGHT_MEASUREMENT_INTERVAL 900 // saniye

// ===== SPIFFS Dosyaları =====
#define SPIFFS_SIZE 8192  // 8KB SPIFFS
#define FILE_KAYITLAR "/kayitlar.csv"
#define FILE_SETTINGS "/settings.json"
#define FILE_MONTHLY_ARCHIVE "/monthly_archive.csv"
#define FILE_ALARM_LOG "/alarm_log.csv"

// ===== Web Sunucu =====
#define WEB_PORT 80
#define WEB_UPDATE_INTERVAL 10000  // 10s live update

// ===== Device IDs (RS485 Protocol) =====
#define DEVICE_ID_ESP8266 0x01
#define DEVICE_ID_NANO 0x02
#define DEVICE_ID_MASTER 0x00

// ===== Hava Durumu / Yagmur Tahmini (sabit konum, internet varken cekilir) =====
// Konum secimi/geocode YOK - bahce sabit, koordinat burada tanimli. Kalburum'un
// kendi RTC'si olmadigi icin "ne zaman cekildi" ESP8266'nin DS1307 RTC'sinden
// (mevcut GET_ZAMAN RS485 komutuyla) okunan gercek takvim gunune gore izlenir -
// NTP/epoch senkronizasyonuna gerek kalmaz.
#define GARDEN_LATITUDE 40.833907
#define GARDEN_LONGITUDE 29.730263
#define WEATHER_FORECAST_API "https://api.open-meteo.com/v1/forecast"
#define WEATHER_FORECAST_DAYS 7
#define WEATHER_RAIN_THRESHOLD_MM 1.0f          // Yarin bu kadar mm+ beklenirse sulama atlanir
#define WEATHER_STALE_DAYS 7                     // Bu kadar gunden eski tahmin gormezden gelinir (fail-open)
#define WEATHER_CHECK_INTERVAL_MS (30UL * 60UL * 1000UL)  // Guncellik/oneri bu araliklarla yeniden hesaplanir

// ===== OTA (GitHub'dan guncelleme) =====
// Sabit "en son surum" linki: repoya push edilen .bin dosyasina isaret eder,
// versiyon karsilastirmasi yapilmaz - "Guncelle" tusuna basildiginda dogrudan
// bu adresten indirilip yazilir. Yeni bir firmware yayinlamak icin:
//   1) pio run (esp32_master) ile derle
//   2) .pio/build/esp32s3/firmware.bin dosyasini esp32_master/firmware/esp32.bin'e kopyala
//   3) main dalina push et - link otomatik guncel halini gosterir (raw.githubusercontent.com HEAD'i takip eder)
#define GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/Nedu41/SuDepoSis_v2/main/esp32_master/firmware/esp32.bin"

// ===== BLE (Telefon Uygulamasi) =====
// Telefondaki eslesmis uygulama, WiFi agina hic girmeden dogrudan BLE ile
// Kontrol sekmesindeki komutlari (lamba/alarm/kapi/panik) gonderip anlik
// durumu okuyabilsin diye. UUID'ler ve cihaz adi test edilen ornek koddan
// (zaten calisiyordu) aynen tasindi - telefon tarafi degismemeli.
#define ENABLE_BLE 1
#define BLE_DEVICE_NAME "ESP32S3_Yonetici"
#define BLE_SERVICE_UUID        "4faac001-82ab-4dc1-9106-97217895d03a"
#define BLE_CHARACTERISTIC_UUID "3a200001-526b-4e01-9fa6-07217895d03a"
#define BLE_NOTIFY_INTERVAL_MS 2000
// Agir TLS (HTTPS - hava durumu/Telegram) istekleri bu esigin altinda heap
// varken atlanir; dusuk heap'te BLE/WiFi coexistence altinda cokme/reset
// (ve bunun sonucu "kirli" BLE kopmasi) riskini azaltir - bkz bleKomutIsle
// yanindaki BLE bolumu notu.
#define BLE_SAFE_MIN_HEAP 40000

// ===== Özellikler =====
// MQTT_BROKER "127.0.0.1" hicbir zaman gercek bir broker'a isaret etmiyordu
// (ESP32 kendi kendini gosteriyordu, lwIP'de gercek bir loopback arayuzu de
// yok) - mqtt_connect() yine de loop()'ta HER 10 SANIYEDE BIR baglanmayi
// deniyor, PubSubClient'in varsayilan soket zaman asimi (15sn) boyunca
// server.handleClient()'i bloke ediyordu. Bu, reset sonrasi web sayfasinin
// "bazen" acilmamasinin (RST/USB/guc ile degil, sadece refresh'in bu bloke
// pencereye denk gelip gelmemesiyle ilgili) asil sebebiydi. Kullanici su an
// MQTT kullanmiyor - ileride gercek bir broker adresiyle tekrar acilabilir.
#define ENABLE_MQTT 0
#define ENABLE_OTA 1
#define ENABLE_SPIFFS 1
#define DEBUG_SERIAL 1

// ===== Serial Debug =====
#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#endif // CONFIG_H
