#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// SuDepoSis v2 - ESP8266 Ölçüm Slave Konfigürasyonu
// Güncellenmiş: v12 arayüz + RS485 + Nano + Lamba
// ============================================================

// ===== RS485 Ayarları (ESP32-S3 Master ile haberleşme) =====
// Daha güvenilir haberleşme için esp8266 SoftwareSerial ile 9600 baud kullan.
#define RS485_BAUDRATE 9600
#define RS485_TX_PIN D0       // GPIO16 - MAX485 DI
#define RS485_RX_PIN D7       // GPIO13 - MAX485 RO
#define RS485_DE_PIN D3       // GPIO0 - MAX485 DE/RE (boot: LOW, jumper 3V3 for upload)

// ===== HC-SR04 Ultrasonik Sensör =====
#define TRIG_PIN D5           // GPIO14
#define ECHO_PIN D6           // GPIO12

// ===== Nano UART0 Haberleşmesi (HardwareSerial) =====
// ESP8266 D9(GPIO3)=UART0 RX ← Nano TX
// ESP8266 D10(GPIO1)=UART0 TX → Nano RX
// Arada 470Ω direnç, ESP D10-GND arası 1KΩ direnç
#define NANO_BAUDRATE 9600
// NOT: Kapi/PIR/role gibi alarm tetikleyicileri sadece bu sorguyla taze kalir.
// 1000ms iken bir tetiklenmenin fark edilmesi (+ ESP32'ye ulasmasi) toplamda
// saniyelerce surebiliyordu. 300ms'e cekildi - Nano yaniti tipik olarak
// ~10-20ms surdugu icin bus/CPU yukunu onemli olcude artirmiyor.
#define NANO_POLL_INTERVAL 300

// ===== Depo Lambası (Nano kontrolü) =====
#define LAMBA_PIN D13          // xxx - Depo iç lamba rölesi (Nano tetikler)

// ===== PIR Sensörü =====
// Artik ayri PIN_READ komutuyla degil, Nano'nun GET_STATUS yanitindaki
// "PIR=" alaniyla dogrudan okunuyor (bkz nano_io PIR_PIN, config.h).

// ===== RTC I2C (DS1307) =====
#define RTC_SDA D2            // GPIO4
#define RTC_SCL D1            // GPIO5

// ===== Gizli Bilgiler (WiFi/OTA sifreleri) =====
// secrets.h git disinda tutulur (.gitignore). Ilk kurulumda
// secrets.h.example dosyasini secrets.h olarak kopyalayip
// gercek degerlerini gir.
#include "secrets.h"

// ===== WiFi Ayarları =====
#define WIFI_AP_SSID "SuDepo"
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_HIDDEN false
#define WIFI_AP_MAX_CLIENTS 4
// MDNS_HOSTNAME artık WIFI_AP_SSID'den türetiliyor (src/main.cpp içinde mdnsHostname())

// ===== Zaman Ayarları =====
#define NIGHT_START_HOUR 22
#define NIGHT_END_HOUR 6

// ===== Yagmur Tahmini (Kalburum'dan RS485 ile gelir) =====
// Bkz. applyMoistureControl()/yagmurSulamaAtlaGecerli() - Kalburum RS485
// uzerinden periyodik tazeler (kablo baglantisi surdukce internetten
// bagimsiz calisir), bu yuzden kisa bir guvenlik penceresi yeterli.
#define RAIN_SKIP_STALE_MS (24UL * 3600UL * 1000UL)  // 24 saat

// ===== Depo Kalibrasyonu (Varsayılan) =====
#define TANK_EMPTY_CM 90.0
#define TANK_FULL_CM 10.0
#define TANK_CAPACITY_LITERS 3000.0
#define TANK_HORIZONTAL 1  // 1=Yatay Silindirik, 0=Dikey/Dikdörtgenn

// ===== EEPROM =====
// DİKKAT: Ayarlar struct'ı WIFI_EEPROM_ADDR (256) ile başlayan WiFi
// bölgesinden ÖNCE bitmeli - struct'a alan eklerken bu sınırı aşmadığından
// emin olun (mevcut struct + siren zamanlama alanları ~70 byte, bolca marj var).
// 512 byte, ESP8266 EEPROM (4096 max) için güvenli ve geleceğe dönük.
#define EEPROM_SIZE 512
#define EEPROM_MAGIC 0xA549  // siren zamanlama ayarlari eklendi (eski: 0xA548)

// Alarm CIKISLARI (siren/role, lamba) - mod basina bitmask, ALARM_TRIGGER_*
// (girdi/sensor) bitmask'inden bagimsiz ayri bir kume.
#define ALARM_OUTPUT_SIREN 1
#define ALARM_OUTPUT_LAMBA 2

// WiFi (STA) bilgileri artik ayri bir EEPROM bolgesinde, kendi magic'i ile
// saklanir. Boylece Ayarlar struct'ina alan eklenip EEPROM_MAGIC degistiginde
// (struct duzeni degistigi icin varsayilanlara donulmesi gerekir) kayitli
// WiFi agi/sifre SILINMEZ - iki bolge birbirinden bagimsiz surumlenir.
#define WIFI_EEPROM_ADDR 256
#define WIFI_EEPROM_MAGIC 0x5731  // 'W1'

// ===== Alarm Tetikleyicileri (Bitmask) =====
#define ALARM_TRIGGER_KAPI1      0x01  // Bit 0: Sol kapı
#define ALARM_TRIGGER_KAPI2      0x02  // Bit 1: Sağ kapı
#define ALARM_TRIGGER_PIR        0x04  // Bit 2: PIR sensörü (hareket)
#define ALARM_TRIGGER_SU_SEVIYE  0x08  // Bit 3: Su seviyesi düşük
#define ALARM_TRIGGER_KACAK      0x10  // Bit 4: Kaçak algılama
#define ALARM_TRIGGER_SENSOR     0x20  // Bit 5: Sensor hatası

// ===== Alarm Modları =====
#define ALARM_MOD_SESLI   1  // Tetiklenince siren/röle hemen çalışır
#define ALARM_MOD_SESSIZ  2  // Röle hiç çalışmaz, sadece web/SSE bildirimi gösterilir
#define ALARM_MOD_ONAYLI  3  // Tetiklenince önce onay bekler; onaylanınca sesli gibi davranır

// ===== Alarm Eşikleri (Varsayılan) =====
#define ALARM_THRESHOLD_PERCENT 15.0
#define MIN_FILL_LITERS 20.0
#define LEAK_THRESHOLD_MINUTES 60

// ===== Kayitlar (Dolum Gecmisi) =====
#define MAX_KAYIT_SAYISI 30  // Sadece en son bu kadar kayit tutulur, eskiler otomatik silinir

// ===== Ölçüm Aralıkları (saniye) =====
#define DAY_MEASURE_INTERVAL 60
#define NIGHT_MEASURE_INTERVAL 900

// ===== Toprak Nem Okuma Sıklığı (2026-09-06 kullanıcı talebi) =====
// Toprak nemi saniyeler icinde degismez, "anlik" okumaya gerek yok - eskiden
// masterGonder() (~1sn) ve SSE (~4sn) her dongude analogRead yapiyordu.
// Artik otomatik okumalar bu araliga sikistirilir (moistureOkuOtomatik()),
// manuel "Canli Olcum" butonu ve pencereye giris ani bu araligi ATLAYIP
// aninda taze deger okur (bkz main.cpp).
#define MOISTURE_OKUMA_ARALIGI_MS (3600000UL)  // 1 saat

// ===== Toprak Nem Kontrol Penceresi (2026-09-06 kullanıcı talebi) =====
// Nem rolesi (D5, NC kontak - harici sulama programlayicisinin devresine
// SERI baglanmis bir kesici, dogrudan vana surucusu DEGIL) artik SADECE bu
// haftalik pencerede aktif degerlendirilir - pencere disinda her zaman
// de-enerjili (vana serbest) kalir, boylece nem yeterliyken gunlerce
// gereksiz enerji harcanmaz. Gun/saat web'den ayarlanabilir (varsayilanlar
// sadece ilk kurulum icin), programlayicinin gercek calisma saatine gore
// (orn. 18:00-18:30 sulama ise 17:00-19:00 pencere yeterli pay birakir).
#define MOISTURE_KONTROL_MAGIC 0x4D4B  // "MK" - bkz struct Ayarlar yorumu
#define MOISTURE_KONTROL_BASLANGIC_SAAT_VARSAYILAN 17
#define MOISTURE_KONTROL_BASLANGIC_DAKIKA_VARSAYILAN 0
#define MOISTURE_KONTROL_BITIS_SAAT_VARSAYILAN 19
#define MOISTURE_KONTROL_BITIS_DAKIKA_VARSAYILAN 0

// ===== GitHub OTA (dogrudan, internet varsa - orn. telefon hotspot'u) =====
// Kalburum'daki (esp32_master) ayni ozellikle eslesir - bkz oradaki
// GITHUB_FIRMWARE_URL. Bahcede internet yoksa bunun yerine hala Kalburum'un
// yerel firmware deposu (http://kalburum.local/firmware/esp8266.bin)
// kullanilabilir; Ayarlar sayfasindaki "Firmware URL" alani ile secilir.
//
// KOK NEDEN KESIN TESHIS EDILDI (2026-08-27, DEBUG_SERIAL=1 + seri log ile
// olculdu, spekulasyon degil): raw.githubusercontent.com VE cdn.jsdelivr.net
// ikisi de bu cihazda basarisiz - performOTA()'daki probeMaxFragmentLength()
// jsDelivr'de de 0 donuyor (MFLN desteklenmiyor), BearSSL varsayilan buyuk
// tampona dusup net "Unable to allocate memory for SSL structures and
// buffers" hatasi veriyor (heap o an ~12KB'a dusuyor). Yani sorun CDN secimi
// DEGIL - bu ESP8266'nin (WebServer+LittleFS+RS485+alarm state ile zaten
// dolu ~80KB RAM) HICBIR HTTPS sunucusuna (MFLN destekleyen haric, ki
// bulunamadi) yetecek bellegi yok. cdn.jsdelivr.net'in TEK dezavantaji
// (agresif ~7gun onbellek, push sonrasi purge gerektirmesi) fayda saglamadan
// eklendigi icin raw.githubusercontent.com'a GERI DONULDU (daha basit, ekstra
// purge adimi yok). GitHub-dogrudan HTTPS OTA bu donanimda GUVENILMEZ -
// Kalburum'un yerel HTTP deposu (http://kalburum.local/firmware/esp8266.bin,
// TLS yok) BIRINCIL yontem, bu sadece "internet varsa bonus" secenegi.
#define GITHUB_FIRMWARE_URL "https://raw.githubusercontent.com/Nedu41/SuDepoSis_v2/main/esp8266_slave/firmware/esp8266.bin"

// ===== Bahçe Kapısı (2 kanat, silecek motoru + R413D08 röle) =====
// HENÜZ SAHAYA KURULMADI (2026-09-07 planlandı) - donanım zamanla monte
// edilecek, altyapı önceden hazırlanıyor. Bkz proje hafızası
// project_bahce_kapisi_motor_gelecek_ozellik.
//
// "Kapalı" pozisyonu için YENİ switch gerekmiyor: mevcut depo alarm kapı
// sensörleri (DOOR1_PIN=D2/DOOR2_PIN=D3, nano_io config.h) zaten bu iki
// kanadın üzerinde - ESP8266 tarafında kapi1Acik/kapi2Acik olarak sürekli
// taze tutuluyor (nanoPoll/GET_STATUS). Bahçe kapısı "kapalı" durumu için
// bunlar DOĞRUDAN kullanılıyor (kapı açıldığında bu sensör true olur, yani
// "kapalı" = !kapi1Acik) - ekstra Nano sorgusu gerekmez.
// DİKKAT: Bu paylaşım nedeniyle motorla kapı açılınca kapi1Acik/kapi2Acik
// true olur - alarm sistemi bunu ALARM_TRIGGER_KAPI1/2 olarak algılar. Motorla
// açılış sırasında bu tetikleyicinin bypass edilmesi ayrıca ele alınmalı
// (henüz YAPILMADI - bkz proje hafızası project_bahce_kapisi_motor_gelecek_ozellik).
//
// Sadece "tam açık" pozisyonu için YENİ limit switch var (kanat başı 1,
// toplam 2) - Nano'nun yedek GPIO'larında (bkz project_nano_yedek_pin_hazir_altyapi).
// Akım sensörleri de Nano analog girişlerinde. Bu pinler pinKorumali() ile
// genel /pin/* API'sinden korunuyor.
#define BAHCE_KAPI1_ACIK_PIN    7   // D7  - Kapı1 tam açık limit switch (INPUT_PULLUP, tetiklenince LOW)
#define BAHCE_KAPI2_ACIK_PIN    9   // D9  - Kapı2 tam açık limit switch
#define BAHCE_KAPI1_AKIM_PIN    15  // A1  - Kapı1 motor akım sensörü (ACS712 5A)
#define BAHCE_KAPI2_AKIM_PIN    16  // A2  - Kapı2 motor akım sensörü (ACS712 5A)

// R413D08 (8CH RS485/Modbus RTU röle) - mevcut Sudepo<->Konteyner RS485
// hattına (swSerial, RS485_TX_PIN/RX_PIN) 3. node olarak eklenir. DİKKAT:
// bu hat daha önce çakışma/gecikme sorunu yaşamıştı (bkz proje hafızası
// project_rs485_gecikme_cozumu) - Modbus çerçeveleri SADECE kapı hareket
// halindeyken, seyrek aralıklarla gönderilir, yanıt beklenmez (fire-and-forget,
// gerçek sonuç limit switch/akım sensörüyle doğrulanır).
#define R413D08_MODBUS_ADRES  1
// Motor yön kontrolü: 2 röle/motor (A ve B terminali bağımsız +V/GND'ye
// çekilir) - AÇIK: A=ON,B=OFF | KAPALI: A=OFF,B=ON | DUR: A=OFF,B=OFF
// (iki terminal de GND'de = fren/durma, kısa devre riski yok). MOSFET
// H-köprüsü YERİNE bilinçli olarak seçildi (bkz proje hafızası).
#define BAHCE_KAPI1_RELE_A  0  // R413D08 kanal 1
#define BAHCE_KAPI1_RELE_B  1  // R413D08 kanal 2
#define BAHCE_KAPI2_RELE_A  2  // R413D08 kanal 3
#define BAHCE_KAPI2_RELE_B  3  // R413D08 kanal 4
#define BAHCE_KAPI1_KILIT_RELE  4  // R413D08 kanal 5 - elektrikli solenoid kilit (darbeli)
#define BAHCE_KAPI2_KILIT_RELE  5  // R413D08 kanal 6

// "Kapalı" limit switch bilgisi artik mevcut alarm kapi sensorunden geldiginden
// (yukaridaki not), bu switch'e ulasilamadan (orn. kilit/mekanik arizasi)
// motorun sonsuza dek "kapaniyor" durumda kalmamasi icin BAHCE_MAX_HAREKET_MS
// tek guvenlik agidir - fiziksel arizaya karsi zaten timeout/asiri-akim var.

// Solenoid kilit darbe süresi: enerji verilince kilit açılır/serbest kalır,
// bu süre kadar beklenip motor başlatılır, sonra röle bırakılır (sürekli
// enerjili tutmaya gerek yok - kilit yayla kendini tekrar kilitler).
#define BAHCE_KILIT_PULSE_MS 1000

// Hareket halindeyken limit switch/akım kontrol aralığı - Nano round-trip
// (~10-50ms) ile bus/CPU yükü arasında NANO_POLL_INTERVAL ile aynı mantık.
#define BAHCE_POLL_ARALIK_MS 250
// Motor bu süreden uzun çalışırsa (limit switch'e hiç ulaşmadıysa) güvenlik
// için otomatik durdurulur - gerçek kanat hareket süresi SAHADA ölçülüp
// buna göre ayarlanmalı (şimdilik geniş bir üst sınır).
#define BAHCE_MAX_HAREKET_MS 20000UL

// ACS712 5A modül: 185mV/Amper hassasiyet, Nano 5V ADC (10-bit, 0-1023).
// BAHCE_AKIM_SIFIR_RAW: 0A'de sensörün ham ADC okuması (teorik ~512, SAHADA
// KALİBRE ET - motor bağlı değilken/dururken ölçülüp buraya girilmeli).
#define ACS712_MV_PER_AMP 185.0
#define BAHCE_AKIM_SIFIR_RAW 512
// Bu akımın üzerinde "sıkışma/zorlanma" kabul edilip motor durdurulur -
// silecek motorunun normal çalışma akımına göre SAHADA KALİBRE ET.
#define BAHCE_AKIM_ESIK_A 4.0

// Zil butonu (2026-09-07): dışarıda buton, içeride (Nano/Sudepo) buzzer
// "ding-dong" çalar - klasik kapı zili, motor/limit switch ile aynı Nano'ya
// bağlı. Nano'nun yedek pinlerinden biri (D10) kullanılıyor.
#define BAHCE_ZIL_BUTON_PIN 10   // D10 - INPUT_PULLUP, basılınca LOW
#define BAHCE_ZIL_POLL_ARALIK_MS 400
#define BAHCE_ZIL_TON1_HZ 2000
#define BAHCE_ZIL_TON2_HZ 1500
#define BAHCE_ZIL_TON_SURE_MS 250

// ===== Debug =====
// NOT: Serial (UART0) Nano ile PAYLASILIYOR (D9/D10) - acarken Nano'yu
// fiziksel olarak ayirin (jumper) yoksa debug metni Nano'nun komut
// parser'ina karisip yanlis komut sanilabilir. 2026-08-27'de GitHub OTA
// "connection failed" teshisi icin kullanildi, sonuc: BearSSL heap yetersiz
// (bkz GITHUB_FIRMWARE_URL yorumu) - artik kapali.
#define DEBUG_SERIAL 0

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H