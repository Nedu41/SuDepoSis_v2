// ============================================================
//  BAHÇE SULAMA - SU SEVİYE SİSTEMİ v12
//  ESP8266 12E (NodeMCU) + HC-SR04 + DS1307 RTC + EEPROM + LittleFS
//  + RS485 (ESP32-S3 Master ile haberlesme)
//  + Nano UART0 (kapı ve röle kontrolü)
// ============================================================

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecureBearSSL.h>
#include <Updater.h>
#include <SoftwareSerial.h>
#include "config.h"
#include "web_content.h" // OTOMATIK URETILIR - bkz scripts/gen_web_content.py

// ============ WiFi AYARLARI (config.h) ============
// AP: SuDepo3T / 18811881 (her zaman acik)
// STA: config.h'dan STA_SSID / STA_PASSWORD

// ============ NESNELER ============
ESP8266WebServer server(80);
RTC_DS1307 rtc;
bool rtcHazir = false;

// ============ KAYIT DOSYALARI ============
const char* KAYIT_DOSYASI = "/kayitlar.csv";
const char* TUKETIM_DOSYASI = "/tuketim.csv";
const char* TUKETIM_GECMIS_DOSYASI = "/tuketim_gecmis.csv";

// ============ EEPROM AYARLARI ============
// EEPROM_SIZE config.h'dan gelir (512) - EEPROM_BOYUT yerine
// struct boyutu 512'den küçük olduğu için taşma olmaz.
// EEPROM_MAGIC config.h'den gelir (0xA543 - WiFi sıfırlama)

struct Ayarlar {
  uint16_t magic;
  float bosMesafe;
  float doluMesafe;
  float depoKapasiteLitre;
  float alarmSeviyeYuzde;
  uint8_t geceBaslangicSaat;
  uint8_t geceBitisSaat;
  uint16_t olcumAraligiGunduz;
  uint16_t olcumAraligiGece;
  float minDolumLitre;
  uint16_t kacakEsikDakika;
  uint8_t depoYatay;
  uint8_t alarmRoleAktif;
  uint8_t alarmTriggerGunduz;  // Gündüz alarm tetikleyicileri (bitmask)
  uint8_t alarmTriggerGece;    // Gece alarm tetikleyicileri (bitmask)
  uint8_t alarmMod;            // 1=Sesli 2=Sessiz 3=Onayli
  uint8_t alarmMaskSesli;      // Sesli mod senaryosu (bitmask)
  uint8_t alarmMaskSessiz;     // Sessiz mod senaryosu (bitmask)
  uint8_t alarmMaskOnayli;     // Onayli mod senaryosu (bitmask)
  uint8_t alarmSensorEtkin;    // Mod/zamandan BAGIMSIZ, her zaman gecerli genel sensor ac/kapa (ALARM_TRIGGER_* bitleri) - kablosuz/arizali sensoru tamamen devre disi birakmak icin
  uint8_t alarmOutputSesli;    // Sesli mod cikislari (bitmask: ALARM_OUTPUT_SIREN|ALARM_OUTPUT_LAMBA)
  uint8_t alarmOutputSessiz;   // Sessiz mod cikislari (bitmask) - varsayilan: sadece lamba
  uint8_t moistureAutomatic;
  uint8_t moistureThresholdLow;
  uint8_t moistureThresholdHigh;
  // SR501 her tetiklenmede ~3sn HIGH kalip ~5sn LOW'a duser (kendi kilitlenme
  // suresi), hareket surdukce bu dongu tekrarlar. Tek darbenin suresi
  // guvenilmez bir sinyal (Time potuna gore degisir), ama tek seferlik
  // gurultunun bir kez, gercek surekli hareketin ise ardarda birden fazla
  // darbe uretmesi guvenilir - bu yuzden pencere icindeki darbe SAYISINA
  // bakiyoruz (bkz. nanoStatusAyristir).
  uint8_t pirPencereSaniye;  // Bu sn'lik pencere icinde darbeler sayilir (0 = filtre yok, ham deger aninda gecerli)
  uint8_t pirMinTetiklenme;  // Pencere icinde PIR tetiklenmis sayilmasi icin gereken minimum ayri darbe sayisi
  // Siren kademeli zamanlama ayari (Konteyner/ESP32 ile ayni mantik, kullanici
  // talebi: "yanlis tetiklerden etkilenmemek icin"). Lamba bu ayarlardan
  // BAGIMSIZ, tetik aninda hemen yanar. Bkz asagidaki state machine (loop()).
  uint16_t sirenGecikmeSaniye;   // ilk chirp'e kadar bekleme (tetik baslangicindan itibaren)
  uint16_t sirenChirpMs;         // chirp suresi (ms)
  uint16_t sirenBeklemeSaniye;   // chirp/aktif-periyot sonrasi sessizlik
  uint16_t sirenAktifSaniye;     // dongudeki tam-aktif kalma suresi
};
Ayarlar ayar;

// WiFi (STA) bilgileri - ayri EEPROM bolgesi, Ayarlar struct'indan bagimsiz
// (bkz. config.h WIFI_EEPROM_ADDR aciklamasi)
struct WifiAyarlar {
  uint16_t magic;
  char ssid[32];
  char sifre[32];
};
WifiAyarlar wifiAyar;

void wifiAyarlariYukle() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(WIFI_EEPROM_ADDR, wifiAyar);
  EEPROM.end();
  if (wifiAyar.magic != WIFI_EEPROM_MAGIC) {
    wifiAyar.magic = WIFI_EEPROM_MAGIC;
    wifiAyar.ssid[0] = '\0';
    wifiAyar.sifre[0] = '\0';
  } else {
    wifiAyar.ssid[31] = '\0';
    wifiAyar.sifre[31] = '\0';
  }
}

void wifiAyarlariKaydet() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(WIFI_EEPROM_ADDR, wifiAyar);
  bool ok = EEPROM.commit();
  EEPROM.end();
  DEBUG_PRINTF("[EEPROM] WiFi kaydedildi: %s (SSID=%s, sifreVar=%d)\n", ok ? "OK" : "HATA", wifiAyar.ssid, strlen(wifiAyar.sifre) > 0);
}

void varsayilanAyarlar() {
  ayar.magic = EEPROM_MAGIC;
  ayar.bosMesafe = TANK_EMPTY_CM;
  ayar.doluMesafe = TANK_FULL_CM;
  ayar.depoKapasiteLitre = TANK_CAPACITY_LITERS;
  ayar.alarmSeviyeYuzde = ALARM_THRESHOLD_PERCENT;
  ayar.geceBaslangicSaat = NIGHT_START_HOUR;
  ayar.geceBitisSaat = NIGHT_END_HOUR;
  ayar.olcumAraligiGunduz = DAY_MEASURE_INTERVAL;
  ayar.olcumAraligiGece = NIGHT_MEASURE_INTERVAL;
  ayar.minDolumLitre = MIN_FILL_LITERS;
  ayar.kacakEsikDakika = LEAK_THRESHOLD_MINUTES;
  ayar.depoYatay = TANK_HORIZONTAL;
  ayar.alarmRoleAktif = 1;
  ayar.alarmTriggerGunduz = 0xFF;  // Varsayilan: tüm tetikleyiciler aktif
  ayar.alarmTriggerGece = 0xFF;    // Varsayilan: tüm tetikleyiciler aktif
  ayar.alarmMod = ALARM_MOD_SESLI;
  ayar.alarmMaskSesli = 0xFF;
  ayar.alarmMaskSessiz = 0xFF;
  ayar.alarmMaskOnayli = 0xFF;
  ayar.alarmSensorEtkin = 0xFF; // Varsayilan: tum sensorler aktif
  ayar.alarmOutputSesli = ALARM_OUTPUT_SIREN | ALARM_OUTPUT_LAMBA;
  ayar.alarmOutputSessiz = ALARM_OUTPUT_LAMBA;  // sessiz mod: siren yok, sadece lamba bildirimi
  ayar.moistureAutomatic = 0;
  ayar.moistureThresholdLow = 40;
  ayar.moistureThresholdHigh = 70;
  ayar.pirPencereSaniye = 10;
  ayar.pirMinTetiklenme = 2;
  ayar.sirenGecikmeSaniye = 5;
  ayar.sirenChirpMs = 300;
  ayar.sirenBeklemeSaniye = 10;
  ayar.sirenAktifSaniye = 5;
}

void ayarlariKaydet() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, ayar);
  // FIX: commit() sonucu kontrol edilmiyordu; yazma hatasinda veri kaybi
  // fark edilmiyordu. Artik debug'ta gercek durum gorunur.
  bool ok = EEPROM.commit();
  EEPROM.end();
  DEBUG_PRINTF("[EEPROM] Kaydedildi: %s\n", ok ? "OK" : "HATA");
}

void ayarlariYukle() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, ayar);
  EEPROM.end();
  DEBUG_PRINTF("[EEPROM] Yuklendi magic=0x%04X\n", ayar.magic);
  // FIX: magic bozuksa varsayilan ayarlarla basla (bozuk veri kullanimi engellenir).
  // WiFi bilgileri bu magic'e bagli degil, ayri yuklenir (bkz wifiAyarlariYukle).
  bool gecerli = (ayar.magic == EEPROM_MAGIC);
  if (!gecerli) {
    DEBUG_PRINTLN("[EEPROM] Magic uyusmazligi - varsayilan ayarlar yukleniyor");
    varsayilanAyarlar();
    ayarlariKaydet();
  } else {
    if (ayar.pirPencereSaniye > 120) ayar.pirPencereSaniye = 10; // eski firmware'den kalma gecersiz/bozuk deger
    // Ust sinir asagidaki PIR_DARBE_GECMISI_BOYUTU (8, satir ~220) ile
    // ESLESMELI - buyugu, sayac en fazla 8 olabildigi icin ASLA
    // tetiklenemeyen bir alarm demekti (bug). Define bu noktadan sonra
    // oldugundan (once kullanilamaz) literal 8 yazildi.
    if (ayar.pirMinTetiklenme == 0 || ayar.pirMinTetiklenme > 8) ayar.pirMinTetiklenme = 2;
  }
}

// ============ OLCUM DEGISKENLERI ============
float sonSeviyeCm = 0, sonYuzde = 0, sonLitre = 0;
String sonOlcumZamani = "-";

// ============ DOLUM TESPITI ============
// 2026-08-27 kullanici bulgusu: "Canli Izle" 2sn'de bir olctugunden, su
// yuzeyi dalgalanmasi/ultrasonik yankı gurultusu TEK ORNEKTE 100L'e varan
// sahte "ani dolum/bosalma" gibi gorunebiliyordu - bu da "Otomatik Tespit"
// kayitlarini (ve tuketim/kacak sayaclarini) gereksiz yere sisiriyordu.
// Iki onlem birden alindi: (1) karar mekanizmasi HAM ardisik farka degil,
// yumusatilmis (EMA) seviyeye bakar - ekrandaki ANLIK deger (sonLitre) BUNDAN
// ETKILENMEZ, sadece dolum/kacak/kayit KARARI icin ayri bir seviyeEMA
// tutulur; (2) bir dolum "bolumu" en az MIN_DOLUM_SURESI_MS surmeden
// Otomatik Tespit kaydi YAZILMAZ - gercek bir tanker/sebeke dolumu dakikalar
// surer, tek olcumluk bir sicrama olmaz.
#define NOISE_ESIK_LITRE 3.0
#define SEVIYE_EMA_AGIRLIK 0.25f
#define MIN_DOLUM_SURESI_MS (30UL * 1000UL)
bool ilkOlcumTamamlandi = false;
float seviyeEMA = 0;
bool dolumDevamEdiyor = false;
float dolumBaslangicLitre = 0, dolumSonPikLitre = 0;
unsigned long dolumBaslangicMs = 0;

// ============ TUKETIM ============
float gunlukTuketim = 0, aylikTuketim = 0;
String gunlukTarih = "", aylikYilAy = "";

// ============ KACAK ALARMI ============
bool kacakSuruyor = false, kacakAlarmi = false;
unsigned long kacakBaslangicMs = 0;
bool alarmAktif = false, sensorHatasi = false;
unsigned long sonOtomatikOlcumMs = 0;

// ============ ALARM MOD DURUMU ============
uint8_t alarmTetikleyenMask = 0; // O an alarmi tetikleyen sensor(ler) - bitmask, banner/uyarida gosterilir
bool alarmCikisLambaIstenen = false; // Bu dongude lamba flasinin acik olmasi gerekiyor mu (mod cikis ayarindan, MIN sure uygulandiktan SONRAKI nihai deger)
unsigned long lambaMinSureBaslangicMs = 0; // 0 = lamba hedefi su an kapali; !=0 ise bu andan itibaren LAMBA_MIN_SURE_MS boyunca lamba acik tutulur
#define LAMBA_MIN_SURE_MS (60UL * 1000UL) // Sustur/tetikleyici erken temizlense bile lamba en az bu kadar yanik kalir
bool alarmSusturuldu = false;   // Susturma - tetikleyici aktifken siren susturulur (mesaj/banner kalir)
unsigned long sirenEpisodeBaslangicMs = 0; // 0 = siren cikisi su an secili degil - HEM state machine referans ani HEM oto-sustur olcumu icin kullanilir (bkz asagidaki kademeli zamanlama deseni)
uint8_t sirenFaz = 0;              // 0=ilk gecikme,1=chirp,2=bekleme,3=aktif (Konteyner/ESP32 ile ayni mantik)
unsigned long sirenFazBaslangicMs = 0;
#define SIREN_MAX_SURE_MS (2UL * 60UL * 1000UL) // bu kadar kesintisiz calarsa (sensor arizasi ihtimaline karsi, Konteyner/ESP32 ile ayni deger) otomatik susturulur - ESP8266'nin Telegram'i olmadigindan bildirim gitmez, sadece susturulur
unsigned long lambaSurekliBaslangicMs = 0; // 0 = alarm-tetikli lamba su an surekli yanmiyor
#define LAMBA_MAX_SURE_MS (10UL * 60UL * 1000UL) // alarm-tetikli lamba (manuel haric) bu kadar kesintisiz yanarsa zorla soner - Konteyner/ESP32 ile ayni deger, enerji butcesi ust siniri
bool alarmOnayBekliyor = false; // Mod 3 (Onayli): tetiklendi, onay bekleniyor
bool alarmOnaylandi = false;    // Mod 3 (Onayli): onaylandi, sesli mod gibi davranir
bool alarmOnaySadeceLamba = false; // Mod 3 (Onayli): "sadece lamba" secildi - siren/role yok, sadece lamba flaşörü

// ============ ALARM LAMBA FLASI ============
// Alarm fiziksel olarak tetiklendiginde (role aktifken) depo lambasi da
// yanip sonerek gorsel uyari verir. Alarm bitince lambayi tetiklenmeden
// onceki (kullanicinin elle ayarladigi) durumuna geri dondurur.
#define LAMBA_FLASH_ARALIK_MS 400
bool lambaFlashAktif = false;
bool lambaFlashOncekiManuel = false;
unsigned long lambaFlashSonDegisimMs = 0;

// ============ PANIK MODU ============
bool panicAktif = false;  // Manuel panik override - anahtar gibi, acik/kapali kalir
bool panicRoleAktif = false;  // Panik modunda rolenin aktif olup olmadigi

// ============ SSE ============
WiFiClient sseClient;
bool sseAktif = false;

// ============ NANO VERISI ============
bool kapi1Acik = false, kapi2Acik = false;
bool pirAcik = false;           // PIR sensörü hareket algısı (ham deger)
// pirTetikleyici, pencere icinde en az ayar.pirMinTetiklenme kez ayri darbe
// olursa true olur (bkz nanoStatusAyristir).
bool pirTetikleyici = false;
// Konteyner (ESP32) zonundaki "on uyari" buzzer'iyla AYNI mantik (2026-08-27
// kullanici talebi): her yeni PIR hareket "bolumu" basladiginda (henuz gercek
// alarm degil) Nano'nun D12 pinine bagli PASIF buzzer'dan 1sn'lik kisa bir
// bip verilir - GERCEK bir alarm/siren zaten calmiyorsa (roleFizikselDurum
// false ise). Buzzer PASIF oldugundan (kendi osilatoru yok) digitalWrite
// yeterli degil - Nano'ya ozel TONE_PLAY komutu eklenip TEK SEFERLIK USB ile
// reflash edildi (bkz nano_io main.cpp). pinKorumali ile bu pin de korunuyor.
#define NANO_BUZZER_PIN 12
#define PIR_DARBE_GECMISI_BOYUTU 8
unsigned long pirDarbeGecmisi[PIR_DARBE_GECMISI_BOYUTU]; // en son darbelerin baslangic zamanlari (dairesel tampon)
uint8_t pirDarbeGecmisiIndex = 0; // pirDarbeGecmisi'nde bir sonraki yazilacak slot
uint8_t pirDarbeSayisiPencerede = 0; // debug/UI icin: su an pencere icinde kalan darbe sayisi
unsigned long pirSonAktifMs = 0; // PIR ham en son ne zaman aktif goruldu (iletisim toleransi icin)
bool roleFizikselDurum = false;
bool rolePolariteHigh = true;  // Nano GET_STATUS'tan gelir - dropdown gercek durumu yansitsin diye
bool nanoBaglantiVar = false;
bool lambaAcik = false;
bool moistureOutputActive = false;
int moistureRaw = 0;
float moisturePercent = 0.0;

// ============ RS485 ============
String sonRS485AlinanMsj = "(yok)";
unsigned long sonRS485AlinanMs = 0;

// ============ mDNS ============
// Hostname, WIFI_AP_SSID'den türetilir (küçük harf, boşluk -> "-")
// Böylece AP adı = mDNS hostname (örn "SuDepo" -> "sudepo.local")
bool mdnsAktif = false;
String mdnsHostname() {
  String h = String(WIFI_AP_SSID);
  h.toLowerCase();
  h.replace(" ", "-");
  return h;
}

// ============ FORWARD DECLARATIONS ============
void tuketimYukle();
void olcumYap();
void kayitlariSiniraGetir(int maxKayit);
void nanoBuzzerChirp();

// ============ ZAMAN YARDIMCILARI ============
String simdikiZamanStr() {
  if (!rtcHazir) return "RTC yok";
  DateTime now = rtc.now();
  if (now.year() < 2024 || now.year() > 2099) { rtcHazir = false; return "RTC gecersiz"; }
  char buf[24];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d", now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second());
  return String(buf);
}

String simdikiTarihISO() {
  if (!rtcHazir) return "";
  DateTime now = rtc.now();
  if (now.year() < 2024 || now.year() > 2099) return "";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.year(), now.month(), now.day());
  return String(buf);
}

String simdikiYilAy() {
  if (!rtcHazir) return "";
  DateTime now = rtc.now();
  if (now.year() < 2024 || now.year() > 2099) return "";
  char buf[10];
  snprintf(buf, sizeof(buf), "%04d-%02d", now.year(), now.month());
  return String(buf);
}

// ===== Gunluk Alarm Logu (2026-08-27 kullanici talebi, ESP32 Merkez Kontrol
// panelindeki AYNI ozellik - iki arayuz paralel tutulmali). ESP32'nin aksine
// burada RTC YEREL (I2C DS1307) oldugundan RS485 gecikme riski yok - zaman
// dogrudan simdikiZamanStr() ile aninda okunabilir, ayri bir onbellek
// gerekmez. Son 5 kayit RAM'de, tumu LittleFS'e (/alarm_log.csv) kalici.
#define ALARM_TRIGGER_TETIKLEYICI_ADET 6
const char* ALARM_TETIKLEYICI_ADLARI[ALARM_TRIGGER_TETIKLEYICI_ADET] = {
  "Sol Kapi", "Sag Kapi", "PIR", "Su Seviyesi", "Kacak", "Sensor Hatasi"
};
String alarmTetikleyenMetniStr(uint8_t mask, bool panik) {
  if (panik) return "Panik (elle acildi)";
  String l = "";
  for (int i = 0; i < ALARM_TRIGGER_TETIKLEYICI_ADET; i++) {
    if (mask & (1 << i)) { if (l.length()) l += ", "; l += ALARM_TETIKLEYICI_ADLARI[i]; }
  }
  return l.length() ? l : "-";
}

#define ALARM_LOG_RAM_ADET 5
#define ALARM_LOG_DOSYASI "/alarm_log.csv"
#define ALARM_LOG_MAX_SATIR 300 // LittleFS sinirli - asilirsa en eski yarisi silinir

struct AlarmLogKaydi { String zaman; String baslik; String tetikleyen; };
AlarmLogKaydi alarmLogRAM[ALARM_LOG_RAM_ADET];
uint8_t alarmLogRAMDolu = 0;
bool alarmLogOncekiVar = false;
// Ayni tetikleyici pencere icinde tekrar tekrar kayit acmasin (ESP32 Merkez
// Kontrol panelindeki AYNI mantik - iki arayuz paralel tutulmali).
#define ALARM_LOG_TEKRAR_BASTIRMA_MS (5UL * 60UL * 1000UL)
String alarmLogSonTetikleyen = "";
unsigned long alarmLogSonYazmaMs = 0;

void alarmLoguDosyayaEkle(const String& satir) {
  if (LittleFS.exists(ALARM_LOG_DOSYASI)) {
    File f = LittleFS.open(ALARM_LOG_DOSYASI, "r");
    int satirSayisi = 0;
    if (f) { while (f.available()) { f.readStringUntil('\n'); satirSayisi++; } f.close(); }
    if (satirSayisi >= ALARM_LOG_MAX_SATIR) {
      File fr = LittleFS.open(ALARM_LOG_DOSYASI, "r");
      String kalanlar; int atlanacak = satirSayisi / 2, i = 0;
      while (fr.available()) {
        String s = fr.readStringUntil('\n');
        if (i++ >= atlanacak) { kalanlar += s; kalanlar += "\n"; }
      }
      fr.close();
      File fw = LittleFS.open(ALARM_LOG_DOSYASI, "w");
      if (fw) { fw.print(kalanlar); fw.close(); }
    }
  }
  File f = LittleFS.open(ALARM_LOG_DOSYASI, "a");
  if (f) { f.println(satir); f.close(); }
}

void alarmLoguKaydet(const String& baslik, const String& tetikleyen) {
  String zaman = simdikiZamanStr();
  for (int i = ALARM_LOG_RAM_ADET - 1; i > 0; i--) alarmLogRAM[i] = alarmLogRAM[i - 1];
  alarmLogRAM[0] = { zaman, baslik, tetikleyen };
  if (alarmLogRAMDolu < ALARM_LOG_RAM_ADET) alarmLogRAMDolu++;
  String satir = zaman + "," + baslik + "," + tetikleyen;
  satir.replace("\n", " "); satir.replace("\r", " ");
  alarmLoguDosyayaEkle(satir);
}

// loop()'taki ana alarm bloguyla AYNI "aktif mi" hesabi (kucuk, bilerek
// tekrarlanan bir kontrol - ayri bir fonksiyona cikarmak yerine, boylece
// asil alarm/siren state machine'ine hic dokunulmuyor).
void alarmLoguKontrolEt() {
  bool alarmVar = panicRoleAktif || (ayar.alarmRoleAktif && (alarmTetikleyenMask != 0 || alarmOnayBekliyor));
  if (alarmVar && !alarmLogOncekiVar) {
    String baslik = panicRoleAktif ? "PANIK AKTIF" : (alarmOnayBekliyor ? "ALARM - Onay Bekliyor" : "ALARM TETIKLENDI");
    String tetikleyen = alarmTetikleyenMetniStr(alarmTetikleyenMask, panicRoleAktif);
    bool aynisiYakinda = (tetikleyen == alarmLogSonTetikleyen) && (millis() - alarmLogSonYazmaMs < ALARM_LOG_TEKRAR_BASTIRMA_MS);
    if (!aynisiYakinda) {
      alarmLoguKaydet(baslik, tetikleyen);
      alarmLogSonTetikleyen = tetikleyen;
      alarmLogSonYazmaMs = millis();
    }
  }
  alarmLogOncekiVar = alarmVar;
}

bool geceModuMu() {
  if (!rtcHazir) return false;
  DateTime now = rtc.now();
  int saat = now.hour();
  int b = ayar.geceBaslangicSaat, e = ayar.geceBitisSaat;
  if (b == e) return false;
  if (b < e) return (saat >= b && saat < e);
  return (saat >= b || saat < e);
}

// ============ NANO HABERLESME ============
void nanoBuzzerKontrolPoll(); // asagida tanimli - nanoPoll() bundan once kullaniyor
// Bekleyen komut kuyruğu - GET_STATUS ile çakışmayı önler
struct NanoPendingCmd {
  String komut;         // "LAMBA_ON", "LAMBA_OFF", vb.
  String ackBeklenen;   // "ACK:LAMBA_ON" vb.
  uint8_t deneme;       // Kaç kez denendi
  unsigned long sonGonderMs;
};
static NanoPendingCmd pendingCmd = {"", "", 0, 0};
static bool nanoMesgul = false; // Yanıt bekleniyor mu?

// Nano'dan gelen yanıtı oku (non-blocking, timeout ms içinde)
String nanoYanitOku(unsigned long timeoutMs) {
  unsigned long bas = millis();
  String yanit = "";
  while (millis() - bas < timeoutMs) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n') break;
      if (c != '\r') yanit += c;
    }
    yield();
  }
  yanit.trim();
  return yanit;
}

// Status yanıtını ayrıştır
void nanoStatusAyristir(const String& yanit) {
  if (yanit.length() == 0) return;
  nanoBaglantiVar = true;
      kapi1Acik          = (yanit.indexOf("D0=1") >= 0);
      kapi2Acik          = (yanit.indexOf("D1=1") >= 0);
      // PIR artik ayri bir PIN_READ komutuyla degil, GET_STATUS yanitinin
      // kendisinden okunuyor - iki ayri komutu ayni pencerede art arda
      // gondermenin yol actigi zamanlama/kesilme sorunlari ortadan kalkti.
      {
        // SR501 her tetiklenmede ~3sn HIGH / ~5sn LOW dongusunde calisiyor
        // (kendi kilitlenme suresi), o yuzden tek darbenin suresi degil,
        // pencere icindeki AYRI DARBE SAYISI degerlendiriliyor. Her yeni
        // darbenin (pirAcik false->true gecisi) baslangic zamani dairesel
        // tampona yazilir, sonra pencere icinde kalan kayitlar sayilir.
        // Tolerans: RS485/Nano hattinda tek bir okumanin "0" donup darbeyi
        // ikiye bolmesi gibi saf iletisim hatalarina karsi kucuk bir pay -
        // gercek darbeler arasi bosluk (min ~5sn kilitlenme) bundan cok
        // daha buyuk oldugu icin yanlislikla birlesme riski yok.
        bool pirHam = (yanit.indexOf("PIR=1") >= 0);
        unsigned long simdiPir = millis();
        const unsigned long PIR_ILETISIM_TOLERANSI_MS = (unsigned long)NANO_POLL_INTERVAL * 2UL;
        if (pirHam) {
          if (!pirAcik) {
            pirDarbeGecmisi[pirDarbeGecmisiIndex] = simdiPir; // yeni darbe basliyor
            pirDarbeGecmisiIndex = (pirDarbeGecmisiIndex + 1) % PIR_DARBE_GECMISI_BOYUTU;
            // Yeni PIR "bolumu" - Konteyner/ESP32'deki AYNI "on uyari" bip'i
            // (bkz NANO_BUZZER_PIN yorumu). Gercek alarm/siren zaten
            // calmiyorsa kisa bir bip verilir - calıyorsa gereksiz, siren
            // zaten duyulur oldugundan atlanir.
            if (!roleFizikselDurum) nanoBuzzerChirp();
          }
          pirSonAktifMs = simdiPir;
          pirAcik = true;
        } else if (pirAcik && simdiPir - pirSonAktifMs > PIR_ILETISIM_TOLERANSI_MS) {
          pirAcik = false;
        }

        if (ayar.pirPencereSaniye == 0) {
          pirDarbeSayisiPencerede = pirAcik ? 1 : 0;
          pirTetikleyici = pirAcik; // filtre yok - ham deger aninda gecerli
        } else {
          unsigned long pencereMs = (unsigned long)ayar.pirPencereSaniye * 1000UL;
          uint8_t sayac = 0;
          for (uint8_t i = 0; i < PIR_DARBE_GECMISI_BOYUTU; i++) {
            unsigned long t = pirDarbeGecmisi[i];
            if (t != 0 && simdiPir - t <= pencereMs) sayac++;
          }
          pirDarbeSayisiPencerede = sayac;
          pirTetikleyici = (sayac >= ayar.pirMinTetiklenme);
        }
      }
      roleFizikselDurum  = (yanit.indexOf("RELE=1") >= 0);
      if (yanit.indexOf("POLARITY=") >= 0) rolePolariteHigh = (yanit.indexOf("POLARITY=1") >= 0);
      lambaAcik          = (yanit.indexOf("LAMBA=1") >= 0);
      moistureOutputActive = (yanit.indexOf("MOISTURE=1") >= 0);
      // NOT: Nano'nun GET_STATUS yanitindaki MOISTURE_RAW=/MOISTURE_PCT=
      // ARTIK OKUNMUYOR - gercek nem sensoru Nano'da degil, ESP8266'nin
      // kendi A0 pininde (bkz moistureOku()). Nano'nunkini buraya
      // yazdirmak, ESP8266'nin dogru yerel okumasini periyodik olarak
      // Nano'nun (baglanmamis) A0 gurultusuyle eziyordu - Kalburum
      // panelinde degerin "bir dogru bir en yuksek" sicramasinin sebebi buydu.
}

void nanoPoll() {
  static unsigned long son = 0;
  unsigned long simdi = millis();

  // --- Bekleyen komut varsa önce onu işle (retry dahil) ---
  if (pendingCmd.komut.length() > 0) {
    // Her 150ms'de bir retry (max 3 deneme)
    if (simdi - pendingCmd.sonGonderMs >= 150) {
      if (pendingCmd.deneme >= 3) {
        // 3 denemede de ACK gelmedi - vazgeç, durumu sorgula
        DEBUG_PRINTF("[NANO] RETRY BASARISIZ: %s\n", pendingCmd.komut.c_str());
        pendingCmd = {"", "", 0, 0};
        nanoMesgul = false;
      } else {
        // Serial buffer'ı temizle, komutu gönder
        while (Serial.available()) Serial.read();
        Serial.print(pendingCmd.komut + "\n");
        pendingCmd.deneme++;
        pendingCmd.sonGonderMs = simdi;
        DEBUG_PRINTF("[NANO] Gonderiliyor (deneme %d): %s\n", pendingCmd.deneme, pendingCmd.komut.c_str());

        // Yanıt bekle (80ms - Nano'nun 10ms loop'u + seri gecikme)
        String yanit = nanoYanitOku(80);
        if (yanit.length() > 0 && yanit.indexOf(pendingCmd.ackBeklenen) >= 0) {
          DEBUG_PRINTF("[NANO] ACK alindi: %s\n", yanit.c_str());
          pendingCmd = {"", "", 0, 0};
          nanoMesgul = false;
          son = simdi; // Poll timer'ı sıfırla
        }
        // ACK gelmedi -> bir sonraki döngüde tekrar denenecek
      }
    }
    return; // Pending komut varken GET_STATUS gönderme
  }

  // Buzzer hedefi (alarm rolesini izler) degistiyse GET_STATUS'tan ONCE
  // gonder - alarm sesi periyodik pollun arkasinda gecikmesin.
  nanoBuzzerKontrolPoll();
  if (pendingCmd.komut.length() > 0) return;

  // --- Periyodik GET_STATUS ---
  if (simdi - son < NANO_POLL_INTERVAL) return;
  son = simdi;

  while (Serial.available()) Serial.read(); // Temiz başla
  Serial.print("GET_STATUS\n");

  // FIX: GET_STATUS yaniti ~85 karakter - 9600 baud'da iletimi tek basina
  // ~90ms suruyor, Nano'nun kendi islem/loop gecikmesi eklenince eskiden
  // buradaki 80ms timeout neredeyse HER ZAMAN yetersiz kaliyordu. Yanit
  // kesiliyor, kuyrukta kalan baytlar asagidaki "temizle" adiminda TAM
  // temizlenemeden PIN_READ gonderiliyordu - bu da PIR okumasini sürekli
  // kirletiyordu (kapi/role/lamba yanitin baslarinda oldugu icin etkilenmiyordu).
  String yanit = nanoYanitOku(200);
  if (yanit.length() > 0) {
    nanoStatusAyristir(yanit);
  } else {
    nanoBaglantiVar = false;
  }
}

bool nanoLambaKontrol(bool ac) {
  // Retry kuyruğuna ekle - GET_STATUS ile çakışmayı önler
  // ACK formatı Nano ile eşleşmeli: "ACK:LAMBA_ON" / "ACK:LAMBA_OFF"
  String komut = ac ? "LAMBA_ON" : "LAMBA_OFF";
  String ack   = ac ? "ACK:LAMBA_ON" : "ACK:LAMBA_OFF";

  // Önceki pending komut varsa iptal et (yeni komut öncelikli)
  pendingCmd = {komut, ack, 0, 0};
  nanoMesgul = true;

  // Optimistik güncelle (UI hemen tepki versin)
  lambaAcik = ac;
  DEBUG_PRINTF("[NANO] Lamba komutu kuyruga alindi: %s\n", komut.c_str());
  return true; // Async - ACK loop'ta işlenecek
}

bool nanoMoistureKontrol(bool ac) {
  // ACK formatı: "ACK:MOISTURE_ON" / "ACK:MOISTURE_OFF"
  String komut = ac ? "MOISTURE_ON" : "MOISTURE_OFF";
  String ack   = ac ? "ACK:MOISTURE_ON" : "ACK:MOISTURE_OFF";

  pendingCmd = {komut, ack, 0, 0};
  nanoMesgul = true;

  moistureOutputActive = ac;
  DEBUG_PRINTF("[NANO] Moisture komutu kuyruga alindi: %s\n", komut.c_str());
  return true;
}

// D12'ye baglanan PASIF buzzer icin - digitalWrite (PIN_WRITE) sabit HIGH/LOW
// verir, pasif buzzer'in ses cikarmasi icin osilasyon (tone()) gerekir. Nano
// tarafina bunun icin ozel TONE_PLAY komutu eklendi (2026-08-27, tek seferlik
// USB reflash ile - genel PIN_WRITE API'si buna yetmiyordu). tone() Nano'da
// kendi zamanlayicisiyla otomatik durur, burada ayrica "kapat" komutu gerekmez.
#define BUZZER_CHIRP_FREKANS_HZ 2500
#define BUZZER_CHIRP_SURE_MS 1000
// FIX (kullanici talebi, 2026-08-27): Buzzer ONCE alarm ROLESiNE (roleFizikselDurum,
// siren FSM'inin kademeli gecikme/chirp/bekleme/aktif zamanlamasi) baglanmisti -
// ama rolenin cikisi mod/cikis secimine gore SIRENSIZ da olabiliyor (orn. Sessiz
// modda sirenSeciliHam=false, role hic tetiklenmez) ve kullanici bunun yerine
// web arayuzundeki KIRMIZI BANNER'in (alarmGenelKutu) gorunurlugunu istedi -
// app.js'deki tarayici-tarafi bipSesi()'ni tetikleyen AYNI kosul (alarmSimdiVar):
// alarmTetikleyenMask || alarmOnayBekliyor || panicAktif. Banner YENI gorunur
// oldugunda (onceki dongude yoktu) TEK bir kisa bip (nanoBuzzerChirp ile ayni
// TONE_PLAY, kendi kendine durur) kuyruga alinir - surekli/kademeli ton YOK.
bool buzzerChirpBekliyor = false;
bool bannerAlarmOncekiDurum = false;

// FIX: eskiden burada dogrudan pendingCmd/nanoMesgul yaziliyordu - eger tam o
// anda baska bir komut (orn. RELAY_ON, GET_STATUS) beklemedeyse onu sessizce
// EZIYORDU. Artik sadece bir "istek" bayragi koyuyor, gercek gonderim
// nanoBuzzerKontrolPoll()'da (nanoPoll icinde, pendingCmd bosken) yapiliyor.
void nanoBuzzerChirp() {
  buzzerChirpBekliyor = true;
}

void nanoBuzzerKontrolPoll() {
  if (pendingCmd.komut.length() > 0 || nanoMesgul) return; // tek-slot protokol - mevcut komutu ezme
  if (!buzzerChirpBekliyor) return;
  buzzerChirpBekliyor = false;
  pendingCmd = {"TONE_PLAY:" + String(NANO_BUZZER_PIN) + "," + String(BUZZER_CHIRP_FREKANS_HZ) + "," + String(BUZZER_CHIRP_SURE_MS), "ACK:TONE_PLAY", 0, 0};
  nanoMesgul = true;
}

void moistureOku() {
  moistureRaw = analogRead(A0);
  moisturePercent = 100.0 - (moistureRaw * 100.0 / 1023.0);
  if (moisturePercent < 0) moisturePercent = 0;
  if (moisturePercent > 100) moisturePercent = 100;
}

// Kalburum'un hava durumu ozelligi ("yarin yagmur var, bugun sulama atla")
// RS485 ile SET_RAIN_SKIP=1/0 gonderir. Kalici EEPROM alani degil - bilincli
// tercih: Kalburum zaten RS485 uzerinden periyodik tazeler (WiFi/internet
// durumundan bagimsiz, kablo baglantisi surdukce), ESP8266 resetlenirse
// varsayilan "sulama normal calissin" (false) guvenli taraftir. Bayrak,
// Kalburum'dan uzun sure (RAIN_SKIP_STALE_MS) haber alinamazsa da otomatik
// geçersiz sayilir - Kalburum kapali/RS485 hatti kopuk gibi durumlarda
// sulamanin sonsuza kadar yanlislikla engellenmemesi icin (fail-open).
bool yagmurSulamaAtla = false;
unsigned long yagmurSonGuncellemeMs = 0;

bool yagmurSulamaAtlaGecerli() {
  return yagmurSulamaAtla && (millis() - yagmurSonGuncellemeMs < RAIN_SKIP_STALE_MS);
}

// Kalburum'un (ESP32) MPPT'den okudugu aku voltaji kritik esigin altina
// dusunce RS485 ile MASTER:SET_BATTERY_LOW=1 gonderip sulama rolesini
// zorla kapatir - hem otomatik moda hem elle "ac" isteklerine engel olur
// (bkz applyMoistureControl ve rs485KomutDinle icindeki SET_MOISTURE=
// dalı). Voltaj toparlaninca Kalburum SET_BATTERY_LOW=0 gonderip serbest
// birakir - burada ayrica bir "geri ac" mantigi gerekmez, mevcut otomatik/
// manuel akis normal calismaya devam eder.
bool batteryLowOverride = false;

void applyMoistureControl() {
  if (batteryLowOverride) {
    if (moistureOutputActive) nanoMoistureKontrol(false);
    return;
  }
  if (!ayar.moistureAutomatic) return;
  if (sensorHatasi) return;
  if (moisturePercent <= ayar.moistureThresholdLow && !moistureOutputActive) {
    if (yagmurSulamaAtlaGecerli()) return;  // Yarin yagmur bekleniyor, yeni sulama baslatma
    nanoMoistureKontrol(true);
  } else if (moisturePercent >= ayar.moistureThresholdHigh && moistureOutputActive) {
    nanoMoistureKontrol(false);
  }
}

bool nanoRoleKontrol(bool tetikle) {
  // ACK formatı: "ACK:RELAY_ON" / "ACK:RELAY_OFF"
  String komut = tetikle ? "RELAY_ON" : "RELAY_OFF";
  String ack   = tetikle ? "ACK:RELAY_ON" : "ACK:RELAY_OFF";

  pendingCmd = {komut, ack, 0, 0};
  nanoMesgul = true;

  roleFizikselDurum = tetikle;
  DEBUG_PRINTF("[NANO] Role komutu kuyruga alindi: %s\n", komut.c_str());
  return true;
}

bool nanoRolePolariteKontrol(bool aktifHigh) {
  // D4 rolesinin "aktif" seviyesini (HIGH/LOW) Nano'yu reflash etmeden
  // degistirir - Nano bunu kendi EEPROM'unda kalici saklar.
  String komut = String("SET_RELAY_POLARITY:") + (aktifHigh ? "1" : "0");
  pendingCmd = {komut, "ACK:SET_RELAY_POLARITY", 0, 0};
  nanoMesgul = true;
  DEBUG_PRINTF("[NANO] Role polarite komutu kuyruga alindi: %s\n", komut.c_str());
  return true;
}

// ============ RS485 ============
SoftwareSerial swSerial(RS485_RX_PIN, RS485_TX_PIN); // RX=D7=GPIO13, TX=D0=GPIO16
void rs485Gonder(const char* data) {
  digitalWrite(RS485_DE_PIN, HIGH);
  delayMicroseconds(100);
  swSerial.print(data);
  // FIX: swSerial.flush() ~166ms bloklar; yerine kısa delay.
  // SoftwareSerial buffer küçük olduğu için print() zaten tamamlanır.
  delay(2);
  digitalWrite(RS485_DE_PIN, LOW);
}

// FIX: masterGonder() hem periyodik (1000ms) hem de poll isteğine yanıt olarak çalışır.
// Periyodik gönderme, SoftwareSerial'in güvenilmez olduğu durumlarda yedek sağlar.
// ESP32 poll'u kaçsa bile veri akışı devam eder.
void masterGonder() {
  // Kalburum'a (ESP32) her zaman taze nem degeri gitsin diye gonderim
  // aninda oku - eskiden bu, sadece sudepo sayfasi acikken (SSE) veya
  // 60-900sn'lik periyodik olcumde tazeleniyordu, aradaki surede Kalburum
  // eski/durgun deger gorebiliyordu.
  moistureOku();
  // EKSIK KALAN YARISI (BUG): deger burada taze okunuyordu ama role KARARI
  // (applyMoistureControl) HALA sadece 60sn (gunduz) / 900sn=15dk (gece!)
  // periyodik olcum donguisunde veriliyordu - Kalburum panelinde deger
  // saniyede guncellenirken (bu fonksiyon ~1sn'de bir calisir), rolenin esigi
  // gecmesine ragmen tepki vermesi 15 dakikaya kadar gecikebiliyordu. Karar
  // da artik AYNI sikta, degerle birlikte taze veriliyor.
  if (ayar.moistureAutomatic) applyMoistureControl();
  // FIX: Mesaj ~230 byte, 160 byte buffer'a sığmıyordu - RS485 verisi kesiliyordu
  char buf[320];
  snprintf(buf, sizeof(buf),
    "ESP8266:LEVEL=%.1f,PCT=%.1f,LITRE=%.0f,TEMP=%.1f,MODE=%s,K1=%d,K2=%d,R=%d,LAMBA=%d,ALARM=%d,ERR=%d,RTC=%d,LEAK=%d,LEAK_DK=%lu,FILL=%d,MOISTURE_RAW=%d,MOISTURE_PCT=%.1f,MOISTURE_OUTPUT=%d,MOISTURE_AUTO=%d,MOISTURE_LOW=%d,MOISTURE_HIGH=%d,ALARM_MOD=%d,ALARM_MUTE=%d,ALARM_PENDING=%d,PANIC=%d,TRIG_MASK=%d,BATTERY_LOW=%d\n",
    sonSeviyeCm, sonYuzde, sonLitre, 0.0,
    geceModuMu() ? "night" : "day",
    kapi1Acik ? 1 : 0,
    kapi2Acik ? 1 : 0,
    roleFizikselDurum ? 1 : 0,
    lambaAcik ? 1 : 0,
    ayar.alarmRoleAktif ? 1 : 0,
    sensorHatasi ? 1 : 0,
    rtcHazir ? 1 : 0,
    kacakAlarmi ? 1 : 0,
    kacakSuruyor ? (millis() - kacakBaslangicMs) / 60000UL : 0,
    dolumDevamEdiyor ? 1 : 0,
    moistureRaw,
    moisturePercent,
    moistureOutputActive ? 1 : 0,
    ayar.moistureAutomatic ? 1 : 0,
    ayar.moistureThresholdLow,
    ayar.moistureThresholdHigh,
    ayar.alarmMod,
    alarmSusturuldu ? 1 : 0,
    alarmOnayBekliyor ? 1 : 0,
    panicRoleAktif ? 1 : 0,
    alarmTetikleyenMask,
    batteryLowOverride ? 1 : 0
  );
  rs485Gonder(buf);
}

// "k1=v1,k2=v2,..." formatindan tek bir anahtarin degerini cikarir (bkz
// SET_AYARLAR/GET_AYARLAR - Kalburum'un Ayarlar sekmesi icin toplu ayar
// aktarimi). Anahtar bulunamazsa bos String doner (cagiran taraf o alani
// degistirmeden birakir).
String ayarDegerAl(const String& veri, const String& anahtar) {
  String arananKey = anahtar + "=";
  int idx = veri.indexOf(arananKey);
  if (idx < 0) return "";
  int start = idx + arananKey.length();
  int end = veri.indexOf(',', start);
  if (end < 0) end = veri.length();
  return veri.substring(start, end);
}

// ============ RS485 ALICI (Master komutlarını dinle) ============
void rs485KomutDinle() {
  static String buffer;
  while (swSerial.available()) {
    char c = swSerial.read();
    if (c == '\n') {
      if (buffer.length() > 0) {
        sonRS485AlinanMsj = buffer;
        sonRS485AlinanMs = millis();
      }
      String response = "NACK:" + buffer;

      if (buffer.startsWith("MASTER:")) {
        String komut = buffer.substring(7);
        if (komut == "REQUEST_ESP8266" || komut == "REQUEST_NANO") {
          masterGonder();
          response = "ACK:" + komut;
        } else if (komut.startsWith("SET_LAMBA=")) {
          int durum = komut.substring(10).toInt();
          bool ok = nanoLambaKontrol(durum == 1);
          response = (ok ? "ACK:" : "NACK:") + komut;
        } else if (komut.startsWith("SET_ALARM=")) {
          bool ac = komut.substring(10).toInt() ? true : false;
          bool ok = nanoRoleKontrol(ac);
          if (ok) {
            ayar.alarmRoleAktif = ac ? 1 : 0;
            ayarlariKaydet();
          }
          response = (ok ? "ACK:" : "NACK:") + komut;
        } else if (komut.startsWith("SET_MOISTURE=")) {
          int durum = komut.substring(13).toInt();
          // Batarya kritikken elle "ac" istegi de reddedilir (sadece
          // otomatik moda degil) - bkz batteryLowOverride tanimi.
          bool ok = (durum == 1 && batteryLowOverride) ? false : nanoMoistureKontrol(durum == 1);
          response = (ok ? "ACK:" : "NACK:") + komut;
        } else if (komut.startsWith("SET_MOISTURE_AUTO=")) {
          bool ac = komut.substring(18).toInt() ? true : false;
          ayar.moistureAutomatic = ac ? 1 : 0;
          ayarlariKaydet();
          if (ayar.moistureAutomatic) olcumYap();
          response = "ACK:" + komut;
        } else if (komut.startsWith("SET_MOISTURE_LOW=")) {
          int value = komut.substring(17).toInt();
          if (value < 0) value = 0;
          if (value > 100) value = 100;
          ayar.moistureThresholdLow = value;
          ayarlariKaydet();
          response = "ACK:" + komut;
        } else if (komut.startsWith("SET_MOISTURE_HIGH=")) {
          int value = komut.substring(18).toInt();
          if (value < 0) value = 0;
          if (value > 100) value = 100;
          ayar.moistureThresholdHigh = value;
          ayarlariKaydet();
          response = "ACK:" + komut;
        } else if (komut.startsWith("SET_RAIN_SKIP=")) {
          yagmurSulamaAtla = komut.substring(14).toInt() ? true : false;
          yagmurSonGuncellemeMs = millis();
          response = "ACK:" + komut;
        } else if (komut.startsWith("SET_BATTERY_LOW=")) {
          batteryLowOverride = komut.substring(17).toInt() ? true : false;
          if (batteryLowOverride && moistureOutputActive) nanoMoistureKontrol(false);
          response = "ACK:" + komut;
        } else if (komut == "GET_KAYITLAR") {
          // Kayit yedekleme: ESP32'ye tum kayitlari tek satirda ('~' ile ayrilmis) gonder.
          // Yedek ESP32'nin kendi SPIFFS'inde tutulur - ESP8266'nin LittleFS'i
          // bozulur/sifirlanirsa (donanim arizasi, factory reset vb.) kurtarma icin.
          String joined = "";
          File f = LittleFS.open(KAYIT_DOSYASI, "r");
          if (f) {
            bool ilk = true;
            while (f.available()) {
              String satir = f.readStringUntil('\n'); satir.trim();
              if (satir.length() == 0) continue;
              if (!ilk) joined += "~";
              joined += satir;
              ilk = false;
            }
            f.close();
          }
          response = "ACK:GET_KAYITLAR=" + joined;
        } else if (komut == "GET_ZAMAN") {
          // ESP32'nin yedekleme zamanini gercek tarih/saat olarak damgalayabilmesi icin
          // (ESP32'de RTC yok, sadece uptime var - gercek zaman burada, DS1307'de).
          response = "ACK:GET_ZAMAN=" + simdikiZamanStr();
        } else if (komut == "RESTORE_BASLA") {
          File f = LittleFS.open("/kayit_restore_tmp.csv", "w");
          if (f) f.close();
          response = "ACK:RESTORE_BASLA";
        } else if (komut.startsWith("RESTORE_SATIR=")) {
          String satir = komut.substring(14);
          File f = LittleFS.open("/kayit_restore_tmp.csv", "a");
          if (f) { f.println(satir); f.close(); }
          response = "ACK:RESTORE_SATIR";
        } else if (komut == "RESTORE_BITIR") {
          LittleFS.remove(KAYIT_DOSYASI);
          LittleFS.rename("/kayit_restore_tmp.csv", KAYIT_DOSYASI);
          response = "ACK:RESTORE_BITIR";
        } else if (komut.startsWith("SET_KAPI=")) {
          bool kapiDurum = (komut.substring(9) == "1");
          bool ok = nanoRoleKontrol(kapiDurum);
          response = (ok ? "ACK:" : "NACK:") + komut;
        } else if (komut.startsWith("SET_ALARM_MOD=")) {
          int m = komut.substring(14).toInt();
          if (m >= 1 && m <= 3) {
            ayar.alarmMod = m; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmOnaySadeceLamba = false; alarmSusturuldu = false;
            ayarlariKaydet();
            response = "ACK:" + komut;
          } else {
            response = "NACK:" + komut;
          }
        } else if (komut.startsWith("ALARM_MUTE=")) {
          // ONCEDEN "ALARM_MUTE" kosulsuz TOGGLE komutuydu - ESP32 tarafi
          // ACK kaybedip komutu tekrar gonderirse (rs485_send_wait_ack
          // retry) susturma yanlislikla iki kez tetiklenip eski haline
          // donebiliyordu. Artik ESP32 acik HEDEF degeri gonderiyor,
          // burada dogrudan o degere SET ediliyor - retry idempotent.
          alarmSusturuldu = (komut.substring(11) == "1");
          response = "ACK:" + komut;
        } else if (komut == "ALARM_ONAYLA") {
          alarmOnaylandi = true; alarmOnayBekliyor = false; alarmOnaySadeceLamba = false;
          response = "ACK:" + komut;
        } else if (komut == "ALARM_ONAYLA_LAMBA") {
          alarmOnaySadeceLamba = true; alarmOnayBekliyor = false; alarmOnaylandi = false;
          response = "ACK:" + komut;
        } else if (komut.startsWith("PANIC=")) {
          // ESP32'nin /api/panic'i bunu bekler: web /role/panic ile ayni
          // davranis, ACK icinde yeni durumu da doner (PANIC=1/0). ONCEDEN
          // bare "PANIC" kosulsuz toggle komutuydu - ayni ACK-kaybi/retry
          // riski (bkz ALARM_MUTE yorumu) panik gibi guvenlik-kritik bir
          // ozellik icin cok daha ciddiydi. Artik acik HEDEF degere set
          // ediliyor, idempotent.
          panicRoleAktif = (komut.substring(6) == "1");
          bool ok = nanoRoleKontrol(panicRoleAktif);
          response = (ok ? "ACK:" : "NACK:") + String("PANIC=") + (panicRoleAktif ? "1" : "0");
        } else if (komut == "GET_AYARLAR") {
          // Kalburum (ESP32) Ayarlar sekmesinden bu Sudepo-zonu ayarlarini
          // gorup degistirebilsin diye - ESP8266 hala tek dogru kaynak/
          // yurutucu, ESP32 sadece okuyup RS485 ile geri yaziyor.
          char buf[540];
          snprintf(buf, sizeof(buf),
            "bosMesafe=%.1f,doluMesafe=%.1f,kapasite=%.0f,alarmYuzde=%.0f,geceBaslangic=%d,geceBitis=%d,minDolumLitre=%.0f,kacakEsikDakika=%d,depoYatay=%d,moistureAutomatic=%d,moistureThresholdLow=%d,moistureThresholdHigh=%d,triggerGunduz=%d,triggerGece=%d,alarmMod=%d,alarmSensorEtkin=%d,alarmMaskSesli=%d,alarmMaskSessiz=%d,alarmMaskOnayli=%d,alarmOutputSesli=%d,alarmOutputSessiz=%d,pirPencereSaniye=%d,pirMinTetiklenme=%d,sirenGecikmeSaniye=%d,sirenChirpMs=%d,sirenBeklemeSaniye=%d,sirenAktifSaniye=%d",
            ayar.bosMesafe, ayar.doluMesafe, ayar.depoKapasiteLitre, ayar.alarmSeviyeYuzde,
            ayar.geceBaslangicSaat, ayar.geceBitisSaat, ayar.minDolumLitre, ayar.kacakEsikDakika,
            ayar.depoYatay, ayar.moistureAutomatic, ayar.moistureThresholdLow, ayar.moistureThresholdHigh,
            ayar.alarmTriggerGunduz, ayar.alarmTriggerGece, ayar.alarmMod, ayar.alarmSensorEtkin, ayar.alarmMaskSesli,
            ayar.alarmMaskSessiz, ayar.alarmMaskOnayli, ayar.alarmOutputSesli, ayar.alarmOutputSessiz,
            ayar.pirPencereSaniye, ayar.pirMinTetiklenme,
            ayar.sirenGecikmeSaniye, ayar.sirenChirpMs, ayar.sirenBeklemeSaniye, ayar.sirenAktifSaniye);
          response = "ACK:AYARLAR=" + String(buf);
        } else if (komut.startsWith("SET_AYARLAR=")) {
          String veri = komut.substring(12);
          String v;
          v = ayarDegerAl(veri, "bosMesafe"); if (v.length()) ayar.bosMesafe = v.toFloat();
          v = ayarDegerAl(veri, "doluMesafe"); if (v.length()) ayar.doluMesafe = v.toFloat();
          v = ayarDegerAl(veri, "kapasite"); if (v.length()) ayar.depoKapasiteLitre = v.toFloat();
          v = ayarDegerAl(veri, "alarmYuzde"); if (v.length()) ayar.alarmSeviyeYuzde = v.toFloat();
          v = ayarDegerAl(veri, "geceBaslangic"); if (v.length()) ayar.geceBaslangicSaat = v.toInt();
          v = ayarDegerAl(veri, "geceBitis"); if (v.length()) ayar.geceBitisSaat = v.toInt();
          v = ayarDegerAl(veri, "minDolumLitre"); if (v.length()) ayar.minDolumLitre = v.toFloat();
          v = ayarDegerAl(veri, "kacakEsikDakika"); if (v.length()) ayar.kacakEsikDakika = v.toInt();
          v = ayarDegerAl(veri, "depoYatay"); if (v.length()) ayar.depoYatay = v.toInt();
          v = ayarDegerAl(veri, "moistureAutomatic"); if (v.length()) ayar.moistureAutomatic = v.toInt() ? 1 : 0;
          v = ayarDegerAl(veri, "moistureThresholdLow"); if (v.length()) { int x = v.toInt(); if (x < 0) x = 0; if (x > 100) x = 100; ayar.moistureThresholdLow = x; }
          v = ayarDegerAl(veri, "moistureThresholdHigh"); if (v.length()) { int x = v.toInt(); if (x < 0) x = 0; if (x > 100) x = 100; ayar.moistureThresholdHigh = x; }
          v = ayarDegerAl(veri, "triggerGunduz"); if (v.length()) ayar.alarmTriggerGunduz = v.toInt();
          v = ayarDegerAl(veri, "triggerGece"); if (v.length()) ayar.alarmTriggerGece = v.toInt();
          v = ayarDegerAl(veri, "alarmMod"); if (v.length()) {
            int m = v.toInt();
            if (m >= 1 && m <= 3) { ayar.alarmMod = m; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmOnaySadeceLamba = false; alarmSusturuldu = false; }
          }
          v = ayarDegerAl(veri, "alarmSensorEtkin"); if (v.length()) ayar.alarmSensorEtkin = v.toInt();
          v = ayarDegerAl(veri, "alarmMaskSesli"); if (v.length()) ayar.alarmMaskSesli = v.toInt();
          v = ayarDegerAl(veri, "alarmMaskSessiz"); if (v.length()) ayar.alarmMaskSessiz = v.toInt();
          v = ayarDegerAl(veri, "alarmMaskOnayli"); if (v.length()) ayar.alarmMaskOnayli = v.toInt();
          v = ayarDegerAl(veri, "alarmOutputSesli"); if (v.length()) ayar.alarmOutputSesli = v.toInt();
          v = ayarDegerAl(veri, "alarmOutputSessiz"); if (v.length()) ayar.alarmOutputSessiz = v.toInt();
          v = ayarDegerAl(veri, "pirPencereSaniye"); if (v.length()) { int x = v.toInt(); if (x < 0) x = 0; if (x > 120) x = 120; ayar.pirPencereSaniye = x; }
          v = ayarDegerAl(veri, "pirMinTetiklenme"); if (v.length()) { int x = v.toInt(); if (x < 1) x = 1; if (x > PIR_DARBE_GECMISI_BOYUTU) x = PIR_DARBE_GECMISI_BOYUTU; ayar.pirMinTetiklenme = x; }
          v = ayarDegerAl(veri, "sirenGecikmeSaniye"); if (v.length()) { int x = v.toInt(); if (x < 0) x = 0; if (x > 120) x = 120; ayar.sirenGecikmeSaniye = x; }
          v = ayarDegerAl(veri, "sirenChirpMs"); if (v.length()) { int x = v.toInt(); if (x < 50) x = 50; if (x > 5000) x = 5000; ayar.sirenChirpMs = x; }
          v = ayarDegerAl(veri, "sirenBeklemeSaniye"); if (v.length()) { int x = v.toInt(); if (x < 1) x = 1; if (x > 120) x = 120; ayar.sirenBeklemeSaniye = x; }
          v = ayarDegerAl(veri, "sirenAktifSaniye"); if (v.length()) { int x = v.toInt(); if (x < 1) x = 1; if (x > 120) x = 120; ayar.sirenAktifSaniye = x; }
          ayarlariKaydet();
          response = "ACK:SET_AYARLAR";
        }
      } else {
        String komut = buffer;
        if (komut == "GET_STATUS") {
          masterGonder();
          response = "ACK:" + komut;
        } else if (komut == "LAMBA_ON") {
          nanoLambaKontrol(true);
          response = "ACK:" + komut;
        } else if (komut == "LAMBA_OFF") {
          nanoLambaKontrol(false);
          response = "ACK:" + komut;
        } else if (komut == "RELAY_ON") {
          nanoRoleKontrol(true);
          response = "ACK:" + komut;
        } else if (komut == "RELAY_OFF") {
          nanoRoleKontrol(false);
          response = "ACK:" + komut;
        }
      }

      response += "\n";
      rs485Gonder(response.c_str());
      buffer = "";
    } else if (c != '\r') {
      buffer += c;
    }
  }
}

// ============ SILINDIK HACIM ============
float silindirHacimLitre(float y, float cap, float maxL) {
  float r = cap / 2.0;
  if (r <= 0 || maxL <= 0) return 0;
  if (y <= 0) return 0;
  if (y >= cap) return maxL;
  float u = (maxL * 1000.0) / (PI * r * r);
  float p = (r - y) / r;
  if (p > 1) p = 1; if (p < -1) p = -1;
  float k = 2 * r * y - y * y;
  if (k < 0) k = 0;
  float a = r * r * acos(p) - (r - y) * sqrt(k);
  float l = (a * u) / 1000.0;
  if (l < 0) l = 0; if (l > maxL) l = maxL;
  return l;
}

// ============ HC-SR04 ============
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long s = pulseIn(ECHO_PIN, HIGH, 30000);
  if (s == 0) return -1;
  float d = s * 0.034 / 2;
  if (d < 2 || d > 450) return -1;
  return d;
}

float olcumOrtalama() {
  float d[3]; int n = 0;
  for (int i = 0; i < 3; i++) {
    float v = measureDistance();
    if (v > 0) d[n++] = v;
    delay(50);
  }
  if (n == 0) return -1;
  if (n == 1) return d[0];
  for (int i = 0; i < n-1; i++)
    for (int j = i+1; j < n; j++)
      if (d[j] < d[i]) { float t = d[i]; d[i] = d[j]; d[j] = t; }
  return d[n/2];
}

// ============ OLCUM ============
void olcumYap() {
  moistureOku();
  applyMoistureControl();
  float m = olcumOrtalama();
  if (m > 0 && m < 500) {
    sensorHatasi = false;
    float kul = ayar.bosMesafe - ayar.doluMesafe;
    if (kul <= 0) kul = 1;
    float s = ayar.bosMesafe - m;
    if (s < 0) s = 0; if (s > kul) s = kul;
    sonSeviyeCm = s;
    if (ayar.depoYatay) {
      sonLitre = silindirHacimLitre(s, kul, ayar.depoKapasiteLitre);
      sonYuzde = (ayar.depoKapasiteLitre > 0) ? (sonLitre / ayar.depoKapasiteLitre) * 100.0 : 0;
    } else {
      sonYuzde = (s / kul) * 100.0;
      sonLitre = (sonYuzde / 100.0) * ayar.depoKapasiteLitre;
    }
    alarmAktif = (sonYuzde <= ayar.alarmSeviyeYuzde);
    sonOlcumZamani = simdikiZamanStr();

    if (ilkOlcumTamamlandi) {
      float oncekiEMA = seviyeEMA;
      seviyeEMA = seviyeEMA + SEVIYE_EMA_AGIRLIK * (sonLitre - seviyeEMA);
      float fark = seviyeEMA - oncekiEMA;
      if (fark > NOISE_ESIK_LITRE) {
        if (!dolumDevamEdiyor) { dolumDevamEdiyor = true; dolumBaslangicLitre = oncekiEMA; dolumBaslangicMs = millis(); }
        dolumSonPikLitre = seviyeEMA;
        kacakSuruyor = false; kacakAlarmi = false;
      } else {
        if (dolumDevamEdiyor) {
          float toplamArtis = dolumSonPikLitre - dolumBaslangicLitre;
          bool yeterinceUzunSurdu = (millis() - dolumBaslangicMs) >= MIN_DOLUM_SURESI_MS;
          if (toplamArtis >= ayar.minDolumLitre && yeterinceUzunSurdu) {
            File f = LittleFS.open(KAYIT_DOSYASI, "a");
            if (f) {
              f.print(simdikiTarihISO()); f.print(",");
              f.print("Otomatik Tespit,"); f.print(toplamArtis, 1); f.print(",0,Belirsiz\n");
              f.close();
              kayitlariSiniraGetir(MAX_KAYIT_SAYISI);
            }
          }
          dolumDevamEdiyor = false;
        }
        if (fark < -NOISE_ESIK_LITRE) {
          float dusus = -fark;
          String simdiTarih = simdikiTarihISO();
          String simdiYilAy = simdikiYilAy();
          if (simdiTarih.length() > 0) {
            if (gunlukTarih.length() > 0 && gunlukTarih != simdiTarih) {
              if (gunlukTuketim > 0) {
                File f = LittleFS.open(TUKETIM_GECMIS_DOSYASI, "a");
                if (f) { f.print(gunlukTarih); f.print(","); f.println(gunlukTuketim, 1); f.close(); }
              }
              gunlukTuketim = 0;
            }
            gunlukTarih = simdiTarih;
            if (aylikYilAy.length() > 0 && aylikYilAy != simdiYilAy) aylikTuketim = 0;
            aylikYilAy = simdiYilAy;
            gunlukTuketim += dusus; aylikTuketim += dusus;
            File f = LittleFS.open(TUKETIM_DOSYASI, "w");
            if (f) {
              f.print(gunlukTarih); f.print(","); f.print(gunlukTuketim, 1); f.print(",");
              f.print(aylikYilAy); f.print(","); f.println(aylikTuketim, 1);
              f.close();
            }
          }
          if (!kacakSuruyor) { kacakSuruyor = true; kacakBaslangicMs = millis(); kacakAlarmi = false; }
          if (!kacakAlarmi && (millis() - kacakBaslangicMs) / 60000UL >= ayar.kacakEsikDakika) kacakAlarmi = true;
        } else {
          kacakSuruyor = false; kacakAlarmi = false;
        }
      }
    } else {
      ilkOlcumTamamlandi = true;
      seviyeEMA = sonLitre;
      tuketimYukle();
    }

    DEBUG_PRINTF("Olcum: %.1f cm (%.1f%%) ~%.0f L\n", sonSeviyeCm, sonYuzde, sonLitre);
  } else {
    sensorHatasi = true;
    DEBUG_PRINTLN("Sensor hatasi!");
  }
}

void tuketimYukle() {
  gunlukTarih = simdikiTarihISO(); aylikYilAy = simdikiYilAy();
  gunlukTuketim = 0; aylikTuketim = 0;
  File f = LittleFS.open(TUKETIM_DOSYASI, "r");
  if (!f) return;
  String satir = f.readStringUntil('\n'); f.close(); satir.trim();
  if (satir.length() == 0) return;
  int p1 = satir.indexOf(','), p2 = satir.indexOf(',', p1+1), p3 = satir.indexOf(',', p2+1);
  if (p1 < 0 || p2 < 0 || p3 < 0) return;
  String t = satir.substring(0, p1); float g = satir.substring(p1+1, p2).toFloat();
  String y = satir.substring(p2+1, p3); float a = satir.substring(p3+1).toFloat();
  if (gunlukTarih.length() > 0 && t == gunlukTarih) gunlukTuketim = g;
  if (aylikYilAy.length() > 0 && y == aylikYilAy) aylikTuketim = a;
}

// ============ KAYIT ISLEMLERI ============
String csvTemizle(String s) { s.replace(",", " "); s.replace("\n", " "); s.replace("\r", " "); return s; }

// Sadece en son MAX_KAYIT_SAYISI kaydi tutar, daha eskileri siler - AMA
// (2026-08-27 kullanici talebi) ELLE GIRILEN kayitlar ASLA silinmez, sadece
// "Otomatik Tespit" (kisi alani tam olarak bu) kayitlari, EN ESKISINDEN
// baslayarak trim edilir. Otomatik kayitlar bitip hala limit asiliysa
// (yani elle girilenlerin kendisi limiti asiyorsa) hicbir sey silinmez -
// kullanicinin kendi girdigi gecmis, nominal sinirdan daha uzun kalabilir.
void kayitlariSiniraGetir(int maxKayit) {
  File f = LittleFS.open(KAYIT_DOSYASI, "r");
  if (!f) return;
  int toplam = 0, otomatikSayisi = 0;
  while (f.available()) {
    String s = f.readStringUntil('\n'); s.trim();
    if (s.length() == 0) continue;
    toplam++;
    if (s.indexOf(",Otomatik Tespit,") >= 0) otomatikSayisi++;
  }
  f.close();
  if (toplam <= maxKayit) return;

  int silinecek = toplam - maxKayit;
  if (silinecek > otomatikSayisi) silinecek = otomatikSayisi; // elle girilenlere dokunma
  if (silinecek <= 0) return;

  File k = LittleFS.open(KAYIT_DOSYASI, "r");
  File g = LittleFS.open("/k_tmp.csv", "w");
  if (!k || !g) { if (k) k.close(); if (g) g.close(); return; }
  int silinen = 0;
  while (k.available()) {
    String s = k.readStringUntil('\n'); s.trim(); if (s.length() == 0) continue;
    if (silinen < silinecek && s.indexOf(",Otomatik Tespit,") >= 0) { silinen++; continue; }
    g.println(s);
  }
  k.close(); g.close();
  LittleFS.remove(KAYIT_DOSYASI); LittleFS.rename("/k_tmp.csv", KAYIT_DOSYASI);
}

bool kayitEkle(String tarih, String kisi, float litre, float ucret, String kaynak) {
  File f = LittleFS.open(KAYIT_DOSYASI, "a");
  if (!f) return false;
  f.print(csvTemizle(tarih)); f.print(",");
  f.print(csvTemizle(kisi)); f.print(",");
  f.print(litre, 1); f.print(",");
  f.print(ucret, 2); f.print(",");
  f.println(csvTemizle(kaynak));
  f.close();
  kayitlariSiniraGetir(MAX_KAYIT_SAYISI);
  return true;
}

String kayitlariOkuJSON(int maxKayit) {
  File f = LittleFS.open(KAYIT_DOSYASI, "r");
  if (!f) return "[]";
  String json = "["; bool ilk = true; int i = 0;
  while (f.available()) {
    String satir = f.readStringUntil('\n'); satir.trim();
    if (satir.length() == 0) continue;
    int p1 = satir.indexOf(','), p2 = satir.indexOf(',', p1+1), p3 = satir.indexOf(',', p2+1), p4 = satir.indexOf(',', p3+1);
    if (p1 >= 0 && p2 >= 0 && p3 >= 0 && p4 >= 0) {
      if (!ilk) json += ",";
      json += "{\"index\":" + String(i) + ",\"tarih\":\"" + satir.substring(0, p1) + "\",\"kisi\":\"" + satir.substring(p1+1, p2) +
           "\",\"litre\":" + satir.substring(p2+1, p3) + ",\"ucret\":" + satir.substring(p3+1, p4) + ",\"kaynak\":\"" + satir.substring(p4+1) + "\"}";
      ilk = false;
    }
    i++; if (i >= maxKayit) break;
  }
  f.close(); json += "]"; return json;
}

struct KayitOzeti {
  float toplamLitre = 0, toplamUcret = 0;
  float ayLitre = 0, ayUcret = 0;
  float ayYagmur = 0, aySebeke = 0, ayTanker = 0;
  int kayitSayisi = 0, belirsizSayisi = 0;
};

void kayitOzet(KayitOzeti& o) {
  File f = LittleFS.open(KAYIT_DOSYASI, "r");
  if (!f) return;
  String by = simdikiYilAy();
  while (f.available()) {
    String satir = f.readStringUntil('\n'); satir.trim();
    if (satir.length() == 0) continue;
    int p1 = satir.indexOf(','), p2 = satir.indexOf(',', p1+1), p3 = satir.indexOf(',', p2+1), p4 = satir.indexOf(',', p3+1);
    if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) continue;
    String th = satir.substring(0, p1);
    float l = satir.substring(p2+1, p3).toFloat(), u = satir.substring(p3+1, p4).toFloat();
    String ky = satir.substring(p4+1);
    o.toplamLitre += l; o.toplamUcret += u; o.kayitSayisi++;
    if (ky == "Belirsiz") o.belirsizSayisi++;
    if (by.length() > 0 && th.startsWith(by)) {
      o.ayLitre += l; o.ayUcret += u;
      if (ky == "Yagmur") o.ayYagmur += l;
      else if (ky == "Sebeke") o.aySebeke += l;
      else if (ky == "Tanker") o.ayTanker += l;
    }
  }
  f.close();
}

bool kayitSil(int idx) {
  File k = LittleFS.open(KAYIT_DOSYASI, "r"); if (!k) return false;
  File g = LittleFS.open("/k_tmp.csv", "w"); if (!g) { k.close(); return false; }
  int i = 0; bool bul = false;
  while (k.available()) {
    String s = k.readStringUntil('\n'); s.trim(); if (s.length() == 0) continue;
    if (i == idx) { bul = true; i++; continue; }
    g.println(s); i++;
  }
  k.close(); g.close();
  LittleFS.remove(KAYIT_DOSYASI); LittleFS.rename("/k_tmp.csv", KAYIT_DOSYASI);
  return bul;
}

bool kayitGuncelle(int idx, String t, String k, float l, float u, String ky) {
  File kD = LittleFS.open(KAYIT_DOSYASI, "r"); if (!kD) return false;
  File g = LittleFS.open("/k_tmp.csv", "w"); if (!g) { kD.close(); return false; }
  int i = 0; bool bul = false;
  while (kD.available()) {
    String s = kD.readStringUntil('\n'); s.trim(); if (s.length() == 0) continue;
    if (i == idx) {
      g.print(csvTemizle(t)); g.print(","); g.print(csvTemizle(k)); g.print(",");
      g.print(l, 1); g.print(","); g.print(u, 2); g.print(","); g.println(csvTemizle(ky));
      bul = true;
    } else { g.println(s); }
    i++;
  }
  kD.close(); g.close();
  LittleFS.remove(KAYIT_DOSYASI); LittleFS.rename("/k_tmp.csv", KAYIT_DOSYASI);
  return bul;
}

// ============ DURUM JSON ============
String durumJson() {
  String j = "{"; // OTA test icin derleme zamani degistirici
  j += "\"firmwareBuild\":\"" __DATE__ " " __TIME__ "\",";
  j += "\"seviye\":" + String(sonSeviyeCm, 1) + ",";
  j += "\"yuzde\":" + String(sonYuzde, 1) + ",";
  j += "\"litre\":" + String(sonLitre, 0) + ",";
  j += "\"zaman\":\"" + sonOlcumZamani + "\",";
  j += "\"alarm\":" + String(alarmAktif ? "true" : "false") + ",";
  j += "\"sensorHata\":" + String(sensorHatasi ? "true" : "false") + ",";
  j += "\"gece\":" + String(geceModuMu() ? "true" : "false") + ",";
  j += "\"kapi1Acik\":" + String(kapi1Acik ? "true" : "false") + ",";
  j += "\"kapi2Acik\":" + String(kapi2Acik ? "true" : "false") + ",";
  j += "\"nanoBagli\":" + String(nanoBaglantiVar ? "true" : "false") + ",";
  j += "\"roleFizikselDurum\":" + String(roleFizikselDurum ? "true" : "false") + ",";
  j += "\"lambaAcik\":" + String(lambaAcik ? "true" : "false") + ",";
  j += "\"moistureRaw\":" + String(moistureRaw) + ",";
  j += "\"moisturePercent\":" + String(moisturePercent, 1) + ",";
  j += "\"moistureOutput\":" + String(moistureOutputActive ? "true" : "false") + ",";
  j += "\"moistureAuto\":" + String(ayar.moistureAutomatic ? "true" : "false") + ",";
  j += "\"moistureLow\":" + String(ayar.moistureThresholdLow) + ",";
  j += "\"moistureHigh\":" + String(ayar.moistureThresholdHigh) + ",";
  j += "\"gunlukTuketim\":" + String(gunlukTuketim, 1) + ",";
  j += "\"aylikTuketim\":" + String(aylikTuketim, 1) + ",";
  j += "\"kacakAlarmi\":" + String(kacakAlarmi ? "true" : "false") + ",";
  j += "\"kacakDakika\":" + String(kacakSuruyor ? (millis() - kacakBaslangicMs) / 60000UL : 0) + ",";
   j += "\"roleAktif\":" + String(ayar.alarmRoleAktif ? "true" : "false") + ",";
   j += "\"roleTetikli\":" + String(roleFizikselDurum ? "true" : "false") + ",";
   j += "\"lamba\":" + String(lambaAcik ? "true" : "false") + ",";
   j += "\"pirAcik\":" + String(pirAcik ? "true" : "false") + ",";
   j += "\"panicAktif\":" + String(panicRoleAktif ? "true" : "false") + ",";
   j += "\"pirTetikleyici\":" + String(pirTetikleyici ? "true" : "false") + ",";
   j += "\"pirDarbeSayisiPencerede\":" + String(pirDarbeSayisiPencerede) + ",";
   j += "\"alarmTetikleyenMask\":" + String(alarmTetikleyenMask) + ",";
   j += "\"triggerGunduz\":" + String(ayar.alarmTriggerGunduz) + ",";
   j += "\"triggerGece\":" + String(ayar.alarmTriggerGece) + ",";
   j += "\"alarmMod\":" + String(ayar.alarmMod) + ",";
   j += "\"alarmSusturuldu\":" + String(alarmSusturuldu ? "true" : "false") + ",";
   j += "\"alarmOnayBekliyor\":" + String(alarmOnayBekliyor ? "true" : "false") + ",";
   j += "\"alarmOnaylandi\":" + String(alarmOnaylandi ? "true" : "false") + ",";
   j += "\"alarmOnaySadeceLamba\":" + String(alarmOnaySadeceLamba ? "true" : "false") + ",";
   j += "\"rolePolariteHigh\":" + String(rolePolariteHigh ? "true" : "false") + ",";
   j += "\"yagmurSulamaAtla\":" + String(yagmurSulamaAtlaGecerli() ? "true" : "false");
   j += "}";
  return j;
}

// ============ CSS ============
void handleCSS() {
  // ESP32 Merkez Kontrol paneliyle AYNI tasarim dili (renk tokenlari, kart/
  // buton/nav/led stilleri) - 2026-08-27 kullanici talebi: iki cihazin web
  // arayuzu gorsel olarak tutarli olsun. Koyu/acik tema burada ESP32'deki
  // gibi prefers-color-scheme DEGIL, mevcut elle-secilen .dark class'iyla
  // calismaya devam ediyor (bu sayfanin zaten calisan tema butonu var).
  String css = ":root{--bg:#f6f8fa;--card:#fff;--text:#1f2937;--muted:#6b7280;--border:#e5e7eb;--border-strong:#4f7fe0;--primary:#2563eb;--accent:#10b981;--warn:#f59e0b;--danger:#ef4444;--danger-bg:#ffebee;--danger-bg-t:rgba(255,235,238,.6);--tab-bg:#eef2f7;--shadow:0 1px 3px rgba(0,0,0,.1)}";
  css += ".dark{--bg:#0b1220;--card:#111827;--text:#e5e7eb;--muted:#9ca3af;--border:#374151;--border-strong:#4a72c0;--primary:#60a5fa;--accent:#34d399;--warn:#fbbf24;--danger:#f87171;--danger-bg:#3a2222;--danger-bg-t:rgba(58,34,34,.6);--tab-bg:#1a2433;--shadow:0 1px 3px rgba(0,0,0,.4)}";
  css += "*{margin:0;padding:0;box-sizing:border-box}";
  css += "body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:var(--bg);min-height:100vh;padding:12px;color:var(--text);max-width:1100px;margin:0 auto;transition:background .3s,color .3s;-webkit-tap-highlight-color:transparent}";
  css += ".topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}";
  css += ".topbar h1{font-size:20px;color:var(--text)}";
  css += ".tema-btn{background:var(--card);border:1px solid var(--border);border-radius:50%;width:38px;height:38px;font-size:14px;cursor:pointer;box-shadow:var(--shadow);color:var(--text)}";
  css += ".sekmeler{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap;position:sticky;top:0;z-index:100;padding:8px 0;background:rgba(246,248,250,.75);backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px)}";
  css += ".dark .sekmeler{background:rgba(11,18,32,.75)}";
  css += ".sekme-btn{padding:8px 12px;border:1px solid var(--border);background:var(--card);color:var(--text);border-radius:8px;cursor:pointer;font-size:13px}";
  css += ".sekme-btn.aktif{background:var(--primary);color:#fff;border-color:var(--primary);font-weight:600}";
  css += ".card{background:var(--card);border:2.5px solid var(--border-strong);border-radius:12px;padding:16px;margin-bottom:12px;box-shadow:var(--shadow)}";
  css += ".card h3{color:var(--text);margin-bottom:8px;font-size:15px;font-weight:700;letter-spacing:.2px}";
  css += ".zaman-bilgisi{color:var(--muted);margin-bottom:10px;font-size:12px;text-align:center}";
  css += ".alarm-kutu{background:rgba(239,68,68,.15);color:var(--danger);border:1px solid rgba(239,68,68,.3);padding:10px;border-radius:8px;margin-bottom:14px;font-weight:700;text-align:center;animation:pulse 1.2s infinite}";
  // Genel alarm banner'i (id alarmGenelKutu) - ESP32 #alarm-banner ile AYNI:
  // sabit, ekranin ortasinda, seffaf+bulanik zemin (altindaki yazi/icerik
  // secilebilsin), sayfa scroll'undan etkilenmez, kaydirma/reflow yapmaz.
  css += "#alarmGenelKutu{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);z-index:1000;background:var(--danger-bg-t);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);color:var(--danger);border:2px solid var(--danger);padding:20px 24px;border-radius:12px;font-weight:700;font-size:16px;text-align:center;animation:pulse 1.2s infinite;box-shadow:0 8px 30px rgba(0,0,0,.35);max-width:90vw;width:380px}";
  css += "@keyframes pulse{0%{opacity:1}50%{opacity:.6}100%{opacity:1}}";
  css += ".depo-container{display:flex;justify-content:center;margin:10px 0 20px}";
  css += ".depo{width:170px;height:230px;background:var(--tab-bg);border-radius:20px 20px 10px 10px;position:relative;overflow:hidden;border:3px solid var(--primary)}";
  css += ".su{position:absolute;bottom:0;left:0;right:0;background:linear-gradient(180deg,#42a5f5,#1976d2);transition:height 1s;border-radius:0 0 8px 8px}";
  css += ".su-dalga{position:absolute;bottom:100%;left:-50%;width:200%;height:15px;background:rgba(255,255,255,.3);border-radius:50%;animation:dalga 3s ease-in-out infinite}";
  css += "@keyframes dalga{0%{transform:translateX(0)}50%{transform:translateX(25%) translateY(-5px)}100%{transform:translateX(0)}}";
  css += ".seviye-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:22px;font-weight:bold;color:#fff;text-shadow:0 2px 10px rgba(0,0,0,.5);z-index:2}";
  css += ".info{padding:14px;background:var(--tab-bg);border-radius:10px;margin:10px 0}";
  // FIX (kullanici sikayeti, 2026-08-27): etiket ("Sol Kapi:" vb.) uzunlugu
  // satirdan satira degistiginden LED/deger farkli x konumlarinda kaliyor,
  // "girintili cikintili" gorunuyordu. ILK DENEME (space-between) LED'i
  // kart kenarina kadar itip etiketten kopardigi icin GERI ALINDI - kullanici
  // "yakinlarda ama hizali" istedi. Cozum: etikete sabit genislik (.info-label),
  // deger/LED hemen ardindan sabit x konumunda baslar (bkz web/index.html'de
  // etiketlerin <span class=info-label> icine alinmasi).
  css += ".info p{margin:6px 0;font-size:14px;display:flex;align-items:center;gap:6px}";
  css += ".info-label{min-width:78px;color:var(--muted)}";
  css += ".info strong{color:var(--primary)}";
  css += ".btn-satir{display:flex;gap:8px;margin:16px 0 6px;flex-wrap:wrap}";
  css += ".btn{flex:1;color:white;padding:10px 12px;border-radius:8px;border:none;font-size:14px;cursor:pointer;font-weight:600;min-width:120px}";
  css += ".btn:active{transform:scale(.97)}";
  css += ".btn-yesil{background:var(--accent)}.btn-turuncu{background:var(--warn)}.btn-mavi{background:var(--primary);width:100%;margin-top:12px}.btn-kirmizi{background:var(--danger);width:100%;margin-top:10px}";
  css += ".btn-satir .btn-mavi,.btn-satir .btn-kirmizi{width:auto;margin-top:0}";
  css += "label{display:block;font-size:12px;color:var(--muted);margin-top:10px}";
  // LED gostergesi - "var/yok" metni yerine kullanilir, ESP32 Merkez Kontrol
  // panelindeki .led ile AYNI (kullanicinin genel arayuz tercihi, tum
  // projelerde uygulanir). RENK ANLAMI (2026-08-27 kullanici duzeltmesi -
  // ayni mantik butun projelerde gecerli): KIRMIZI = ariza/tehlike/anormal
  // durum (ornek: baglanti YOK, kapi ACIK, duman/gaz VAR); YESIL = saglikli/
  // bagli/normal calisiyor (ornek: Nano bagli, role/lamba gorevini yapiyor);
  // SONUK/gri = notr yokluk (alarm tarafinda "tetiklenmedi" gibi). Ayni
  // gostergeyi hem "saglik" hem "alarm" anlaminda ayni renge (orn. hep
  // kirmizi=aktif) baglamak yanlis - "Nano bagli" gibi baglanti gostergeleri
  // .ok (yesil) ile, "kapi acik/duman var" gibi tehlike gostergeleri .on
  // (kirmizi) ile isaretlenir.
  css += ".led{display:inline-block;width:11px;height:11px;border-radius:50%;background:var(--border);vertical-align:middle;transition:background .15s,box-shadow .15s}";
  css += ".led.on{background:var(--danger);box-shadow:0 0 6px var(--danger)}";
  css += ".led.ok{background:var(--accent);box-shadow:0 0 6px var(--accent)}";
  // Sistem durumu 3 kademeli isik - ESP32 Merkez Kontrol panelindeki .sysdot
  // ile AYNI (normal=yesil, kritik=sari, tehlike=kirmizi soft yanip soner).
  css += ".sysdot{width:15px;height:15px;border-radius:50%;display:inline-block}";
  css += ".sysdot.normal{background:var(--accent);box-shadow:0 0 7px var(--accent)}";
  css += ".sysdot.kritik{background:var(--warn);box-shadow:0 0 7px var(--warn)}";
  css += ".sysdot.tehlike{background:var(--danger);box-shadow:0 0 9px var(--danger);animation:softblink 1.8s ease-in-out infinite}";
  css += "@keyframes softblink{0%,100%{opacity:1}50%{opacity:.3}}";
  // Checkbox gruplari (Zaman Bazli Tetikleyiciler / Mod Senaryolari): checkbox+yazi
  // ayni satirda, dikeyde ortalanmis, birden fazla checkbox yan yana kirilarak dizilir.
  css += ".cb-grid{display:flex;flex-wrap:wrap;gap:6px 18px;align-items:center}";
  css += ".cb-grid p{flex-basis:100%;margin:0 0 4px;color:var(--muted);font-size:12px}";
  css += ".cb{display:flex;align-items:center;gap:6px;margin-top:0;font-size:13px;color:var(--text);width:auto}";
  // NOT: genel "input,select{width:100%;padding:11px;border:...}" kurali
  // checkbox'lari da dev, kenarlikli kutulara ceviriyordu - burada sifirlaniyor.
  css += ".cb input[type=checkbox],.cb input[type=radio]{width:16px;height:16px;flex-shrink:0;padding:0;border:1px solid var(--border);margin:0;background:var(--card)}";
  css += ".cb input[type=checkbox]{border-radius:4px}";
  css += "input,select{padding:10px 12px;border:1px solid var(--border);border-radius:8px;font-size:14px;width:100%;margin-top:4px;background:var(--card);color:var(--text)}";
  css += ".sonuc-metni{margin-top:10px;font-size:13px;text-align:center;color:var(--muted);min-height:16px}";
  css += ".muted{color:var(--muted);font-size:13px;text-align:center;padding:10px 0}";
  css += "table{width:100%;border-collapse:collapse;font-size:12px}";
  css += "th{text-align:left;color:var(--muted);padding:6px 4px;border-bottom:2px solid var(--border)}";
  css += "td{padding:8px 4px;border-bottom:1px solid var(--border)}";
  css += ".belirsiz-satir{background:rgba(255,152,0,.12)}";
  css += ".uyari-metni{color:var(--warn);font-weight:bold}";
  css += ".duzenle-form{display:flex;flex-wrap:wrap;gap:6px;padding:8px 0}";
  css += ".duzenle-form select,.duzenle-form input{flex:1;min-width:90px;margin-top:0;padding:8px}";
  css += ".duzenle-form button{width:auto;margin-top:0;padding:8px 14px}";
  css += ".btn-sil{background:none;border:none;cursor:pointer;font-size:14px}";
  // Ayarlar/Alarm sekmelerindeki kartlar <details> oldugundan (ESP32'deki
  // acilir-kapanir Ayarlar deseniyle AYNI) - kenarlari daha belirgin.
  css += "details.card{padding:0}";
  // Acilir-kapanir kartlarin her biri kendi renginde sol kenarla ayirt
  // edilsin - ESP32 Merkez Kontrol panelindeki AYNI palet/mantik (2026-08-27,
  // "iki arayuz paralel" kurali).
  const char* kenarRenkleri[12] = {"#3b82f6","#06b6d4","#14b8a6","#f97316","#84cc16","#8b5cf6","#ec4899","#6366f1","#64748b","#f43f5e","#eab308","#22c55e"};
  for (int i = 0; i < 12; i++) {
    css += "details.card:nth-of-type(" + String(i + 1) + "){border-left:7px solid " + kenarRenkleri[i] + "}";
  }
  css += "details.card>summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px;padding:16px;margin-bottom:0;font-size:15px;font-weight:700;color:var(--text);letter-spacing:.2px}";
  css += "details.card>summary::-webkit-details-marker{display:none}";
  css += "details.card>summary::before{content:'\\25B8';display:inline-block;font-size:12px;color:var(--muted);transition:transform .15s}";
  css += "details.card[open]>summary::before{transform:rotate(90deg)}";
  css += "details.card>*:not(summary){margin-left:16px;margin-right:16px}";
  css += "details.card>summary{margin-left:0;margin-right:0}";
  css += "details.card>*:last-child{margin-bottom:16px}";
  css += "details.card>h3:first-of-type,details.card>p:first-of-type{margin-top:16px}";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/css", css);
}

// ============ HTML/JS (firmware'e gomulu - bkz web_content.h) ============
// index.html ve app.js artik LittleFS'te ayri dosyalar degil, derleme
// sirasinda scripts/gen_web_content.py tarafindan uretilen web_content.h
// icinde PROGMEM sabiti olarak firmware'in kendi icinde. Dinamik degerler
// JS tarafinda /olc, /durum, /ayarlar API'lerinden cekilir.
void handleRoot() {
  // FIX: tarayici (ozellikle mobil) bu sayfayi/JS'i cache'leyip guncellemeleri
  // gostermeyebiliyordu - Cache-Control tek basina yetersiz kalabiliyordu,
  // Pragma/Expires de eklendi (eski/agresif mobil cache'ler icin).
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", INDEX_HTML);
}


// ============ SSE PUSH ============
void ssePush() {
  if (!sseAktif) return;
  if (!sseClient.connected()) { sseAktif = false; return; }
  String data = "data: " + durumJson() + "\n\n";
  sseClient.print(data);
}

void handleSSE() {
  if (sseAktif) { sseClient.stop(); sseAktif = false; }
  sseClient = server.client();
  sseClient.setNoDelay(true);
  sseClient.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n"
  );
  sseAktif = true;
  ssePush();  // ilk veriyi hemen gönder
}

// ============ API ROTALARI ============
void handleRestart() {
  // FIX (kullanici sikayeti, 2026-08-27): 100ms gecikme cok kisaydi -
  // server.send() yaniti TCP gonderim kuyruguna koyar ama WiFi uzerinden
  // fiilen iletilip tarayiciya ULASMASI icin zaman gerekir; ESP.restart()
  // bunu beklemeden baglantiyi sert kesince tarayicidaki fetch() ne
  // basariya ne hataya duser, sonsuza kadar "Yeniden baslatiliyor..."
  // yazili kalir (ayni sorun performOTA()'da da vardi, orada da duzeltildi).
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Yeniden baslatiliyor\"}");
  server.client().flush();
  delay(500);
  ESP.restart();
}
void handleMeasure() { olcumYap(); ssePush(); server.send(200, "application/json", durumJson()); }
void handleStatus() { moistureOku(); if (ayar.moistureAutomatic) applyMoistureControl(); server.send(200, "application/json", durumJson()); }
void handleTime() {
  String json = "{\"zaman\":\"" + simdikiZamanStr() + "\",\"tarihISO\":\"" + simdikiTarihISO() + "\"}";
  server.send(200, "application/json", json);
}
void handleSetTime() {
  String m = "Hata!"; bool b = false;
  if (server.hasArg("zaman")) {
    String z = server.arg("zaman");
    if (z.length() >= 16) {
      int y = z.substring(0,4).toInt(), a = z.substring(5,7).toInt(), g = z.substring(8,10).toInt(), s = z.substring(11,13).toInt(), d = z.substring(14,16).toInt(), sn = z.length()>=19?z.substring(17,19).toInt():0;
      if (rtcHazir) { rtc.adjust(DateTime(y,a,g,s,d,sn)); m = "Zaman ayarlandi"; b = true; } else m = "RTC yok";
    } else m = "Format!";
  }
  server.send(200, "application/json", "{\"mesaj\":\"" + m + "\",\"basarili\":" + String(b?"true":"false") + "}");
}
void handleGetSettings() {
  String j = "{";
  j += "\"bosMesafe\":" + String(ayar.bosMesafe,1) + ",\"doluMesafe\":" + String(ayar.doluMesafe,1) + ",\"kapasite\":" + String(ayar.depoKapasiteLitre,0) + ",\"alarmYuzde\":" + String(ayar.alarmSeviyeYuzde,0) + ",\"geceBaslangic\":" + String(ayar.geceBaslangicSaat) + ",\"geceBitis\":" + String(ayar.geceBitisSaat) + ",\"minDolumLitre\":" + String(ayar.minDolumLitre,0) + ",\"kacakEsikDakika\":" + String(ayar.kacakEsikDakika) + ",\"depoYatay\":" + String(ayar.depoYatay) + ",\"moistureAutomatic\":" + String(ayar.moistureAutomatic ? "true" : "false") + ",\"moistureThresholdLow\":" + String(ayar.moistureThresholdLow) + ",\"moistureThresholdHigh\":" + String(ayar.moistureThresholdHigh) + ",\"triggerGunduz\":" + String(ayar.alarmTriggerGunduz) + ",\"triggerGece\":" + String(ayar.alarmTriggerGece) + ",\"alarmMod\":" + String(ayar.alarmMod) + ",\"alarmSensorEtkin\":" + String(ayar.alarmSensorEtkin) + ",\"alarmMaskSesli\":" + String(ayar.alarmMaskSesli) + ",\"alarmMaskSessiz\":" + String(ayar.alarmMaskSessiz) + ",\"alarmMaskOnayli\":" + String(ayar.alarmMaskOnayli) + ",\"alarmOutputSesli\":" + String(ayar.alarmOutputSesli) + ",\"alarmOutputSessiz\":" + String(ayar.alarmOutputSessiz) + ",\"pirPencereSaniye\":" + String(ayar.pirPencereSaniye) + ",\"pirMinTetiklenme\":" + String(ayar.pirMinTetiklenme) + ",\"sirenGecikmeSaniye\":" + String(ayar.sirenGecikmeSaniye) + ",\"sirenChirpMs\":" + String(ayar.sirenChirpMs) + ",\"sirenBeklemeSaniye\":" + String(ayar.sirenBeklemeSaniye) + ",\"sirenAktifSaniye\":" + String(ayar.sirenAktifSaniye) + "}";
  server.send(200, "application/json", j);
}
void handleSaveSettings() {
  if (server.hasArg("bosMesafe")) ayar.bosMesafe = server.arg("bosMesafe").toFloat();
  if (server.hasArg("doluMesafe")) ayar.doluMesafe = server.arg("doluMesafe").toFloat();
  if (server.hasArg("kapasite")) ayar.depoKapasiteLitre = server.arg("kapasite").toFloat();
  if (server.hasArg("alarmYuzde")) ayar.alarmSeviyeYuzde = server.arg("alarmYuzde").toFloat();
  if (server.hasArg("geceBaslangic")) ayar.geceBaslangicSaat = server.arg("geceBaslangic").toInt();
  if (server.hasArg("geceBitis")) ayar.geceBitisSaat = server.arg("geceBitis").toInt();
  if (server.hasArg("minDolumLitre")) ayar.minDolumLitre = server.arg("minDolumLitre").toFloat();
  if (server.hasArg("kacakEsikDakika")) ayar.kacakEsikDakika = server.arg("kacakEsikDakika").toInt();
  if (server.hasArg("depoYatay")) ayar.depoYatay = server.arg("depoYatay").toInt();
  if (server.hasArg("moistureAutomatic")) ayar.moistureAutomatic = server.arg("moistureAutomatic").toInt() ? 1 : 0;
  if (server.hasArg("moistureThresholdLow")) {
    int v = server.arg("moistureThresholdLow").toInt(); if (v < 0) v = 0; if (v > 100) v = 100; ayar.moistureThresholdLow = v;
  }
  if (server.hasArg("moistureThresholdHigh")) {
    int v = server.arg("moistureThresholdHigh").toInt(); if (v < 0) v = 0; if (v > 100) v = 100; ayar.moistureThresholdHigh = v;
  }
  if (server.hasArg("triggerGunduz")) ayar.alarmTriggerGunduz = server.arg("triggerGunduz").toInt();
  if (server.hasArg("triggerGece")) ayar.alarmTriggerGece = server.arg("triggerGece").toInt();
  if (server.hasArg("alarmMod")) {
    int m = server.arg("alarmMod").toInt();
    if (m >= 1 && m <= 3) { ayar.alarmMod = m; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmOnaySadeceLamba = false; alarmSusturuldu = false; }
  }
  if (server.hasArg("alarmSensorEtkin")) ayar.alarmSensorEtkin = server.arg("alarmSensorEtkin").toInt();
  if (server.hasArg("alarmMaskSesli")) ayar.alarmMaskSesli = server.arg("alarmMaskSesli").toInt();
  if (server.hasArg("alarmMaskSessiz")) ayar.alarmMaskSessiz = server.arg("alarmMaskSessiz").toInt();
  if (server.hasArg("alarmMaskOnayli")) ayar.alarmMaskOnayli = server.arg("alarmMaskOnayli").toInt();
  if (server.hasArg("alarmOutputSesli")) ayar.alarmOutputSesli = server.arg("alarmOutputSesli").toInt();
  if (server.hasArg("alarmOutputSessiz")) ayar.alarmOutputSessiz = server.arg("alarmOutputSessiz").toInt();
  if (server.hasArg("pirPencereSaniye")) {
    int v = server.arg("pirPencereSaniye").toInt(); if (v < 0) v = 0; if (v > 120) v = 120; ayar.pirPencereSaniye = v;
  }
  if (server.hasArg("pirMinTetiklenme")) {
    int v = server.arg("pirMinTetiklenme").toInt(); if (v < 1) v = 1; if (v > PIR_DARBE_GECMISI_BOYUTU) v = PIR_DARBE_GECMISI_BOYUTU; ayar.pirMinTetiklenme = v;
  }
  if (server.hasArg("sirenGecikmeSaniye")) { int v = server.arg("sirenGecikmeSaniye").toInt(); if (v < 0) v = 0; if (v > 120) v = 120; ayar.sirenGecikmeSaniye = v; }
  if (server.hasArg("sirenChirpMs")) { int v = server.arg("sirenChirpMs").toInt(); if (v < 50) v = 50; if (v > 5000) v = 5000; ayar.sirenChirpMs = v; }
  if (server.hasArg("sirenBeklemeSaniye")) { int v = server.arg("sirenBeklemeSaniye").toInt(); if (v < 1) v = 1; if (v > 120) v = 120; ayar.sirenBeklemeSaniye = v; }
  if (server.hasArg("sirenAktifSaniye")) { int v = server.arg("sirenAktifSaniye").toInt(); if (v < 1) v = 1; if (v > 120) v = 120; ayar.sirenAktifSaniye = v; }
  ayarlariKaydet(); olcumYap();
  server.send(200, "application/json", "{\"mesaj\":\"Ayarlar kaydedildi\",\"basarili\":true}");
}

void handleKayitListesi() {
  KayitOzeti o; kayitOzet(o);
  String j = "{\"kayitlar\":" + kayitlariOkuJSON(100) + ",\"ozet\":{\"toplamLitre\":" + String(o.toplamLitre,1) + ",\"toplamUcret\":" + String(o.toplamUcret,2) + ",\"ayLitre\":" + String(o.ayLitre,1) + ",\"ayUcret\":" + String(o.ayUcret,2) + ",\"ayYagmur\":" + String(o.ayYagmur,1) + ",\"aySebeke\":" + String(o.aySebeke,1) + ",\"ayTanker\":" + String(o.ayTanker,1) + ",\"kayitSayisi\":" + String(o.kayitSayisi) + ",\"belirsizSayisi\":" + String(o.belirsizSayisi) + "}}";
  server.send(200, "application/json", j);
}
void handleKayitEkle() {
  if (!server.hasArg("litre") || !server.hasArg("tarih")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Eksik\"}"); return; }
  String t = server.arg("tarih"), k = server.hasArg("kisi")?server.arg("kisi"):"-", y = server.hasArg("kaynak")?server.arg("kaynak"):"Sebeke";
  float l = server.arg("litre").toFloat(), u = server.hasArg("ucret")?server.arg("ucret").toFloat():0;
  if (l <= 0) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Litre hata\"}"); return; }
  server.send(200, "application/json", kayitEkle(t,k,l,u,y) ? "{\"basarili\":true,\"mesaj\":\"OK\"}" : "{\"basarili\":false,\"mesaj\":\"Hata\"}");
}
void handleKayitGuncelle() {
  if (!server.hasArg("index")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"index eksik\"}"); return; }
  int idx = server.arg("index").toInt();
  String t = server.hasArg("tarih")?server.arg("tarih"):simdikiTarihISO(), k = server.hasArg("kisi")?server.arg("kisi"):"-", y = server.hasArg("kaynak")?server.arg("kaynak"):"Belirsiz";
  float l = server.hasArg("litre")?server.arg("litre").toFloat():0, u = server.hasArg("ucret")?server.arg("ucret").toFloat():0;
  server.send(200, "application/json", kayitGuncelle(idx,t,k,l,u,y) ? "{\"basarili\":true,\"mesaj\":\"OK\"}" : "{\"basarili\":false,\"mesaj\":\"Bulunamadi\"}");
}
void handleKayitSil() {
  if (!server.hasArg("index")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"index eksik\"}"); return; }
  server.send(200, "application/json", kayitSil(server.arg("index").toInt()) ? "{\"basarili\":true,\"mesaj\":\"OK\"}" : "{\"basarili\":false,\"mesaj\":\"Bulunamadi\"}");
}
void handleKayitCSV() {
  File f = LittleFS.open(KAYIT_DOSYASI, "r");
  if (!f) { server.send(404, "text/plain", "Yok"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=kayitlar.csv");
  server.streamFile(f, "text/csv"); f.close();
}
void handleKayitTemizle() {
  if (server.arg("onay") != "evet") { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Onay\"}"); return; }
  LittleFS.remove(KAYIT_DOSYASI); File f = LittleFS.open(KAYIT_DOSYASI, "w"); if (f) f.close();
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Tumunu silindi\"}");
}
void handleRoleAyarla() {
  if (!server.hasArg("aktif")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"param eksik\"}"); return; }
  ayar.alarmRoleAktif = server.arg("aktif").toInt()?1:0; ayarlariKaydet();
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(ayar.alarmRoleAktif?"Aktif":"Pasif") + "\"}");
}
void handleRolePanic() {
  panicRoleAktif = !panicRoleAktif;  // Toggle - panik butonu her basış toggle yapar
  bool ok = nanoRoleKontrol(panicRoleAktif);
  server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"mesaj\":\"" + String(panicRoleAktif?"Panik Acik":"Panik Kapali") + "\",\"panic\":" + String(panicRoleAktif?"true":"false") + "}");
}
void handleRolePolarite() {
  if (!server.hasArg("aktif")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"param eksik\"}"); return; }
  bool aktifHigh = server.arg("aktif").toInt() != 0;
  nanoRolePolariteKontrol(aktifHigh);
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Polarite: " + String(aktifHigh ? "HIGH=Aktif" : "LOW=Aktif") + "\"}");
}
void handleAlarmSustur() {
  // Susturma = "sireni kapat": tetikleyici hala aktif olsa da rölenin
  // sese/tetiklenmeye devam etmesini durdurur. Yeni bir tetikleyici
  // (durum degisikligi) gelene kadar susturulmus kalir.
  alarmSusturuldu = !alarmSusturuldu;
  server.send(200, "application/json", "{\"basarili\":true,\"susturuldu\":" + String(alarmSusturuldu ? "true" : "false") + "}");
}
void handleAlarmOnayla() {
  // Mod 3 (Onayli): kullanici tetiklenmeyi onaylar, siren sesli mod gibi calismaya baslar.
  alarmOnaylandi = true; alarmOnayBekliyor = false; alarmOnaySadeceLamba = false;
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Onaylandi\"}");
}
void handleAlarmOnaylaLamba() {
  // Mod 3 (Onayli): kullanici sadece lamba flasoru ister - siren/role calismaz.
  alarmOnaySadeceLamba = true; alarmOnayBekliyor = false; alarmOnaylandi = false;
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Sadece lamba flasoru aktif\"}");
}
String jsonKacir(const String& s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  return out;
}
void handleAlarmLog() {
  String j = "[";
  for (uint8_t i = 0; i < alarmLogRAMDolu; i++) {
    if (i) j += ",";
    j += "{\"zaman\":\"" + jsonKacir(alarmLogRAM[i].zaman) + "\",\"baslik\":\"" + jsonKacir(alarmLogRAM[i].baslik) + "\",\"tetikleyen\":\"" + jsonKacir(alarmLogRAM[i].tetikleyen) + "\"}";
  }
  j += "]";
  server.send(200, "application/json", j);
}
// ESP32 Merkez Kontrol panelindeki handleAPI_AlarmLogTam ile AYNI mantik -
// "tetikleyen" alani virgul icerebildigi icin sadece ILK IKI virgule kadar bolunur.
void handleAlarmLogTam() {
  String j = "[";
  bool ilk = true;
  if (LittleFS.exists(ALARM_LOG_DOSYASI)) {
    File f = LittleFS.open(ALARM_LOG_DOSYASI, "r");
    if (f) {
      while (f.available()) {
        String satir = f.readStringUntil('\n');
        satir.trim();
        if (satir.length() == 0) continue;
        int v1 = satir.indexOf(',');
        int v2 = (v1 >= 0) ? satir.indexOf(',', v1 + 1) : -1;
        if (v1 < 0 || v2 < 0) continue;
        if (!ilk) j += ",";
        ilk = false;
        j += "{\"zaman\":\"" + jsonKacir(satir.substring(0, v1)) + "\",\"baslik\":\"" + jsonKacir(satir.substring(v1 + 1, v2)) + "\",\"tetikleyen\":\"" + jsonKacir(satir.substring(v2 + 1)) + "\"}";
      }
      f.close();
    }
  }
  j += "]";
  server.send(200, "application/json", j);
}
void handleWifiDurum() {
  bool b = (WiFi.status() == WL_CONNECTED);
  server.send(200, "application/json", "{\"tanimli\":" + String(strlen(wifiAyar.ssid)>0?"true":"false") + ",\"ssid\":\"" + String(wifiAyar.ssid) + "\",\"bagli\":" + String(b?"true":"false") + ",\"ip\":\"" + (b?WiFi.localIP().toString():"-") + "\",\"sifreVar\":" + String(strlen(wifiAyar.sifre)>0?"true":"false") + "}");
}
void handleWifiKaydet() {
  String ssid = "";
  String pw = "";
  // Accept either long names (ssid/sifre) or short names (s/p) from the JS/UI
  if (server.hasArg("ssid")) ssid = server.arg("ssid");
  else if (server.hasArg("s")) ssid = server.arg("s");
  if (server.hasArg("sifre")) pw = server.arg("sifre");
  else if (server.hasArg("p")) pw = server.arg("p");

  // If no SSID provided at all, reject the request
  if (!server.hasArg("ssid") && !server.hasArg("s")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"ssid eksik\"}"); return; }

  // Empty SSID => remove saved WiFi
  if (ssid.length() == 0) {
    wifiAyar.ssid[0]='\0'; wifiAyar.sifre[0]='\0'; wifiAyarlariKaydet(); WiFi.disconnect();
    server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Kaldirildi\"}");
    return;
  }

  ssid.toCharArray(wifiAyar.ssid, 32);
  // FIX: Sifre bos birakildiysa eski kayitli sifre korunur (silinmez).
  // Ayrica bozuk DEBUG_PRINTF satiri duzeltildi.
  if (pw.length() > 0) {
    pw.toCharArray(wifiAyar.sifre, 32);
  }
  wifiAyarlariKaydet();
  DEBUG_PRINTF("[WIFI] Kaydedilen SSID: %s, sifreVar=%d\n", wifiAyar.ssid, strlen(wifiAyar.sifre) > 0);
  WiFi.disconnect(); delay(100);
  WiFi.begin(wifiAyar.ssid, wifiAyar.sifre);
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Kaydedildi\"}");
}

void handleWifiScan() {
  delay(100); // give radio a moment
  DEBUG_PRINTLN("[WIFI] Scan basliyor...");
  int n = WiFi.scanNetworks();
  DEBUG_PRINTF("[WIFI] Bulunan ag sayisi: %d\n", n);
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    String ss = WiFi.SSID(i);
    int r = WiFi.RSSI(i);
    bool sec = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
    DEBUG_PRINTF("[WIFI] %d: %s (%d dBm) secured=%d\n", i, ss.c_str(), r, sec);
    // FIX: Tirnak/backslash iceren SSID'ler JSON'u bozuyordu
    ss.replace("\\", "\\\\");
    ss.replace("\"", "\\\"");
    j += "{\"ssid\":\"" + ss + "\",\"rssi\":" + String(r) + ",\"secured\":" + String(sec ? "true" : "false") + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

// https://host[:port]/path formatindan host/port ayiklar (probeMaxFragmentLength icin).
static bool parseHttpsHost(const String& url, String& host, uint16_t& port) {
  if (!url.startsWith("https://")) return false;
  int hostStart = 8; // strlen("https://")
  int pathStart = url.indexOf('/', hostStart);
  String hostPort = (pathStart == -1) ? url.substring(hostStart) : url.substring(hostStart, pathStart);
  int colon = hostPort.indexOf(':');
  if (colon == -1) { host = hostPort; port = 443; }
  else { host = hostPort.substring(0, colon); port = (uint16_t)hostPort.substring(colon + 1).toInt(); }
  return host.length() > 0;
}

// ============ JS ENDPOINT ============
// FIX: Eskiden yanit indirme/yazma BASLAMADAN ONCE "Guncelleniyor" diye
// gonderiliyordu - basarili da olsa (kutuphane ESP.restart() ile yanit asla
// gonderilemeden yeniden baslatiyordu) basarisiz da olsa (hata sadece
// Serial'e yaziliyordu) buton metni sonsuza kadar "Guncelleniyor" yazili
// kaliyordu. Simdi rebootOnUpdate(false) ile otomatik restart kapatilip
// yanit GERCEK sonucla gonderiliyor, restart ondan SONRA yapiliyor.
void performOTA(const String& url) {
  ESPhttpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return ret;
  if (url.startsWith("https://")) {
    // https:// (orn. raw.githubusercontent.com) icin BearSSL gerekir - sertifika
    // zincirini dogrulamadan kabul eder (ESP8266'da tam zincir dogrulama pahali).
    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15000);
    // FIX: BearSSL'in varsayilan TLS tampon boyutlari (~16KB+) ESP8266'nin
    // ~80KB RAM'inde (WebServer/LittleFS/RS485/EEPROM zaten kullanimda)
    // ayrilamiyordu - ("connection failed"). Once sabit 512 bayta
    // zorlandi, ama bu da YANLIS: sunucu o boyutu gercekte kabul etmeyip
    // daha buyuk bir kayit gonderdiginde BearSSL'in tampona sigdiramayip
    // baglantiyi ortadan kesmesine yol acti ("connection lost"). Dogrusu:
    // once probeMaxFragmentLength ile sunucunun GERCEKTEN destekledigi en
    // kucuk boyutu sormak, sadece o boyutu kullanmak - sunucu desteklemezse
    // varsayilan (buyuk) tampona geri donulur.
    String host; uint16_t port;
    if (parseHttpsHost(url, host, port)) {
      int mfln = client.probeMaxFragmentLength(host.c_str(), port, 512);
      if (mfln > 0) client.setBufferSizes(mfln, mfln);
    }
    ret = ESPhttpUpdate.update(client, url);
  } else {
    WiFiClient client;
    ret = ESPhttpUpdate.update(client, url);
  }

  String mesaj;
  bool basarili = (ret == HTTP_UPDATE_OK);
  if (basarili) {
    mesaj = "Basarili - yeniden baslatiliyor";
  } else if (ret == HTTP_UPDATE_NO_UPDATES) {
    mesaj = "Guncelleme yok";
  } else {
    // Teshis icin: "connection failed" hem dusuk heap (BearSSL) hem de STA'nin
    // gercekte internete cikamamasi (sadece yerel ag/AP) yuzunden olabilir -
    // ikisini ayirt etmek icin heap + WiFi durumu hataya ekleniyor.
    mesaj = "Hata: " + ESPhttpUpdate.getLastErrorString() +
            " [heap:" + String(ESP.getFreeHeap()) +
            " wifi:" + (WiFi.isConnected() ? (WiFi.SSID() + "/" + WiFi.localIP().toString()) : "baglanti-yok") + "]";
  }
  // FIX: /ota?url=X her tiklamada AYNI URL ile GET istegi - tarayici
  // Cache-Control gonderilmezse bunu onbellekleyip cihaza hic gitmeden eski
  // yaniti (orn. eski firmware'den kalma "Guncelleniyor: X" metnini) tekrar
  // tekrar gosterebiliyordu.
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send(200, "application/json", "{\"basarili\":" + String(basarili ? "true" : "false") + ",\"mesaj\":\"" + mesaj + "\"}");
  // FIX (kullanici sikayeti, 2026-08-27, bkz handleRestart yorumu): 200ms
  // gecikme de yetersiz kalabiliyordu - yanit tarayiciya tam ulasmadan
  // ESP.restart() baglantiyi kesiyor, buton sonsuza kadar "Guncelleniyor..."
  // yazili kaliyordu (basarili=false/hata durumunda BU SORUN YOK - orada
  // restart hic cagrilmiyor, yaniti okuyup gosteren normal fetch akisi calisiyor).
  if (basarili) { server.client().flush(); delay(500); ESP.restart(); }
}

void handleOTAUpdate() {
  if (!server.hasArg("url")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"URL eksik\"}"); return; }
  performOTA(server.arg("url"));
}

// Kalburum'daki "GitHub'dan Guncelle" tek-tikla butonunun aynisi - internet
// varsa (orn. telefon hotspot'u) dogrudan GitHub'dan ceker, Kalburum'un
// yerel deposuna ihtiyac duymaz.
void handleOTAGithub() {
  performOTA(GITHUB_FIRMWARE_URL);
}

// ============ DOSYADAN OTA (bin dosyasi web'den yuklenir) ============
void handleFileUploadUpdate() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  server.client().flush();
  delay(500); // bkz handleRestart yorumu - yanit tarayiciya ulasmadan restart etmeme
  ESP.restart();
}

void handleFileUploadProgress() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    DEBUG_PRINTF("[OTA-FILE] Basliyor: %s\n", upload.filename.c_str());
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) { Update.printError(Serial); }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      DEBUG_PRINTF("[OTA-FILE] Basarili: %u byte\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

// /app.js firmware'e gomulu (bkz web_content.h)
void handleJS() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "application/javascript", APP_JS);
}

// /config.js dinamik - sadece const K=... degerini icerir
void handleConfigJS() {
  server.send(200, "application/javascript",
    "const K=" + String(ayar.depoKapasiteLitre, 0) + ";");
}

// ============ WiFi KURULUMU ============
void setupWiFi() {
  WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); delay(200);
  WiFi.mode(WIFI_AP_STA); WiFi.setSleepMode(WIFI_NONE_SLEEP); delay(100);
  bool ok = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, WIFI_AP_HIDDEN);
  if (ok) { DEBUG_PRINT("AP: "); DEBUG_PRINTLN(WIFI_AP_SSID); DEBUG_PRINT("IP: "); DEBUG_PRINTLN(WiFi.softAPIP()); }
  if (strlen(wifiAyar.ssid) > 0) {
    String host = mdnsHostname();
    WiFi.hostname(host.c_str());
    DEBUG_PRINT("STA: "); DEBUG_PRINTLN(wifiAyar.ssid);
    WiFi.begin(wifiAyar.ssid, wifiAyar.sifre);
  }
}

// Genel amacli /pin/mode,/pin/write,/pin/read GPIO API'si (Nano'ya dogrudan
// serial komut gonderir) icin donanima ONCEDEN AYRILMIS pinleri korur.
// ONCEDEN BUG: bu API hicbir blacklist olmadan pin numarasini oldugu gibi
// Nano'ya iletiyordu - orn. /pin/write?pin=4&val=1 dogrudan alarm rolesini
// (nano_io/include/config.h: RELAY_PIN=4) manipule edebiliyordu,
// /pin/mode?pin=6&mod=OUTPUT ise PIR pinini (PIR_PIN=6) surucu cikisina
// cevirip surekli yanlis "hareket" bildirimine yol acabiliyordu -
// hicbir kimlik dogrulama da yok. Pin numaralari nano_io/include/config.h
// ile SENKRON tutulmali: DOOR1_PIN=2, DOOR2_PIN=3, RELAY_PIN=4,
// MOISTURE_PIN=5, PIR_PIN=6, LAMBA_PIN=13.
bool pinKorumali(int pin) {
  return pin == 2 || pin == 3 || pin == 4 || pin == 5 || pin == 6 || pin == 13 || pin == NANO_BUZZER_PIN;
}

// ============ SETUP ============
void setup() {
  // FIX: Serial (UART0) 9600 baud'da başlatılır - Nano ile aynı hız
  // NOT: ESP8266 UART0 (TX=GPIO1, RX=GPIO3) HEM debug hem Nano için kullanılır.
  // Debug mesajları Nano tarafından "UNKNOWN_COMMAND" olarak yanıtlanır, işlevsel etkisi yoktur.
  Serial.begin(NANO_BAUDRATE); delay(500);
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("[SISTEM] Basliyor...");
  DEBUG_PRINTF("[SISTEM] Nano baglanti: %d baud\n", NANO_BAUDRATE);
  DEBUG_PRINTF("📶 STA IP: %s\n", WiFi.localIP().toString().c_str());
  
  ayarlariYukle();
  wifiAyarlariYukle();
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(RS485_DE_PIN, OUTPUT); digitalWrite(RS485_DE_PIN, LOW);
  // NOT: LAMBA_PIN Nano üzerinde (D13), ESP8266'da değil
  swSerial.begin(RS485_BAUDRATE);
  // FIX: LittleFS.begin() hatasi + format sonrasi ikinci begin kontrol edilmiyordu.
  // FS acilamazsa dosya okuma/yazma islemleri sessizce basarisiz oluyordu.
  if (!LittleFS.begin()) {
    DEBUG_PRINTLN("[LittleFS] begin basarisiz, formatliyorum...");
    LittleFS.format();
    if (!LittleFS.begin()) {
      DEBUG_PRINTLN("[LittleFS] HATA: format sonrasi begin de basarisiz!");
    } else {
      DEBUG_PRINTLN("[LittleFS] format ve begin tamam");
    }
  }
  if (!LittleFS.exists(KAYIT_DOSYASI)) { File f = LittleFS.open(KAYIT_DOSYASI, "w"); if (f) f.close(); }
  if (!LittleFS.exists(TUKETIM_DOSYASI)) { File f = LittleFS.open(TUKETIM_DOSYASI, "w"); if (f) f.close(); }
  if (!LittleFS.exists(TUKETIM_GECMIS_DOSYASI)) { File f = LittleFS.open(TUKETIM_GECMIS_DOSYASI, "w"); if (f) f.close(); }
  Wire.begin(RTC_SDA, RTC_SCL);
  delay(100);
  if (!rtc.begin()) {
    rtcHazir = false;
    DEBUG_PRINTLN("[RTC] DS1307 bulunamadi - pil bitmis olabilir!");
  } else {
    rtcHazir = true;
    // TANI LOGU: "RTC yine kayboldu" sikayeti tekrarlarsa, gercekten pil/
    // osilator mu durmus (donanim) yoksa isrunning() tek seferlik I2C
    // gurultusuyle mi yanlis "durmus" okumus (yazilim) ayirt edebilmek icin -
    // adjust() ile UZERINE YAZMADAN ONCE cipte fiilen ne kayitli oldugunu
    // logla. Eger burada makul/yakin bir tarih goruluyorsa (ornegin bugune
    // yakin), pil/osilator sorunu degil, tek seferlik yanlis okuma demektir.
    DateTime oncekiDeger = rtc.now();
    char tanibuf[32];
    snprintf(tanibuf, sizeof(tanibuf), "%02d/%02d/%04d %02d:%02d:%02d", oncekiDeger.day(), oncekiDeger.month(), oncekiDeger.year(), oncekiDeger.hour(), oncekiDeger.minute(), oncekiDeger.second());
    DEBUG_PRINT("[RTC] Boot - isrunning="); DEBUG_PRINT(rtc.isrunning() ? "evet" : "HAYIR");
    DEBUG_PRINT(", cipteki mevcut deger="); DEBUG_PRINTLN(tanibuf);
    if (!rtc.isrunning()) {
      DEBUG_PRINTLN("[RTC] DS1307 calismiyor - build tarihine resetleniyor (yukaridaki 'cipteki mevcut deger' kaybolan zamandi)");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  // FIX: HC-SR04 boot aninda (guc rayi henuz tam oturmamisken, WiFi radyosu
  // daha baslamadan) ilk olcumde bazen gecerli yaniti vermiyor, "sensor
  // hatasi" alarmini gereksiz yere tetikliyordu ("her ac/kapatta" sikayeti).
  // Ilk olcum basarisiz olursa donanim gercekten arizali degilse birkac
  // deneme icinde duzelir - kisa aralarla birkac kez daha denenir.
  olcumYap();
  for (int deneme = 0; deneme < 3 && sensorHatasi; deneme++) {
    delay(200);
    olcumYap();
  }
  setupWiFi();

  // FIX: STA baglantisini bekle - mDNS icin gecerli IP gerekli.
  // WiFi.begin() asenkron calisir; IP almadan MDNS.begin() yapilirsa
  // sudepo.local cozulemez.
  if (strlen(wifiAyar.ssid) > 0) {
    unsigned long baslaMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - baslaMs < 8000) {
      delay(100);
      yield();  // watchdog reset
    }
  }

  // FIX: mDNS'i WiFi baglantisi SONRASINDA baslat (gecerli IP ile).
  // Hostname = WIFI_AP_SSID (kucuk harf). Orn: "SuDepo" -> sudepo.local
  String host = mdnsHostname();
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsAktif = true;
    DEBUG_PRINTF("[mDNS] Baslatildi: %s.local\n", host.c_str());
  } else {
    mdnsAktif = false;
    DEBUG_PRINTLN("[mDNS] Baslatma basarisiz!");
  }
  ArduinoOTA.setHostname(host.c_str()); ArduinoOTA.setPassword(OTA_PASSWORD); ArduinoOTA.begin();

  server.on("/", handleRoot); server.on("/style.css", handleCSS);
  server.on("/app.js", handleJS); server.on("/config.js", handleConfigJS);
  server.on("/events", handleSSE);
  server.on("/olc", handleMeasure); server.on("/durum", handleStatus); server.on("/zaman", handleTime); server.on("/ayarla", handleSetTime);
  server.on("/ayarlar", HTTP_GET, handleGetSettings); server.on("/ayarlar/kaydet", handleSaveSettings);
  server.on("/kayit/liste", handleKayitListesi); server.on("/kayit/ekle", handleKayitEkle); server.on("/kayit/guncelle", handleKayitGuncelle); server.on("/kayit/sil", handleKayitSil); server.on("/kayit/csv", handleKayitCSV); server.on("/kc", handleKayitCSV); server.on("/kayit/temizle", handleKayitTemizle);
  server.on("/role/ayarla", handleRoleAyarla); server.on("/role/panic", handleRolePanic);
  server.on("/alarm/sustur", handleAlarmSustur); server.on("/alarm/onayla", handleAlarmOnayla); server.on("/alarm/onayla_lamba", handleAlarmOnaylaLamba);
  server.on("/alarm/log", handleAlarmLog);
  server.on("/alarm/log/tam", handleAlarmLogTam);
  server.on("/role/polarite", handleRolePolarite);
  server.on("/wifi/durum", handleWifiDurum); server.on("/wifi/kaydet", handleWifiKaydet);
  server.on("/wifi/scan", handleWifiScan);
  server.on("/lamba", []() { if (!server.hasArg("durum")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"param eksik\"}"); return; } int y = server.arg("durum").toInt(); // Buffer temizle, komutu gönder, ACK bekle (max 300ms)
  while (Serial.available()) Serial.read();
  Serial.println(y ? "LAMBA_ON" : "LAMBA_OFF");
  unsigned long t = millis(); bool ok = false;
  while (millis() - t < 300) {
    if (Serial.available()) {
      String r = Serial.readStringUntil('\n'); r.trim();
      if (r.indexOf("ACK:LAMBA") >= 0) { ok = true; break; }
    }
    yield();
  }
  if (ok) lambaAcik = (y == 1); server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"mesaj\":\"" + String(ok?(lambaAcik?"Acik":"Kapali"):"Hatali") + "\"}");});  
  server.on("/nem", []() { if (!server.hasArg("durum")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"param eksik\"}"); return; } int y = server.arg("durum").toInt(); while (Serial.available()) Serial.read(); Serial.println(y ? "MOISTURE_ON" : "MOISTURE_OFF"); unsigned long t = millis(); bool ok = false; while (millis() - t < 300) { if (Serial.available()) { String r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("ACK:MOISTURE") >= 0) { ok = true; break; } } yield(); }
  if (ok) moistureOutputActive = (y == 1); server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"mesaj\":\"" + String(ok?(moistureOutputActive?"Acik":"Kapali"):"Hatali") + "\"}");});
  server.on("/nem/mod", []() { if (!server.hasArg("otomatik")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"param eksik\"}"); return; } ayar.moistureAutomatic = server.arg("otomatik").toInt()?1:0; ayarlariKaydet(); if (ayar.moistureAutomatic) { olcumYap(); } server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(ayar.moistureAutomatic?"Otomatik":"Manuel") + "\"}"); });
  // ===== NANO GENEL GPIO API (PIN_MODE/PIN_WRITE/PIN_READ) =====
  // Nano'ya seri komut gonderir, yaniti bekler ve JSON olarak dondurur.
  // Ornek: /pin/mode?pin=6&mod=OUTPUT  |  /pin/write?pin=6&val=1  |  /pin/read?pin=6
  server.on("/pin/mode", []() {
    if (!server.hasArg("pin") || !server.hasArg("mod")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"pin ve mod gerekli\"}"); return; }
    int pin = server.arg("pin").toInt();
    if (pinKorumali(pin)) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Bu pin donanima ayrilmis (kapi/role/PIR/lamba), genel GPIO API ile degistirilemez\"}"); return; }
    String mod = server.arg("mod"); mod.toUpperCase();
    if (mod != "OUTPUT" && mod != "INPUT" && mod != "INPUT_PULLUP") { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"mod: OUTPUT/INPUT/INPUT_PULLUP\"}"); return; }
    while (Serial.available()) Serial.read();
    Serial.print("PIN_MODE:"); Serial.print(pin); Serial.print(","); Serial.println(mod);
    unsigned long t = millis(); String r = ""; bool ok = false;
    while (millis() - t < 300) {
      if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("ACK:PIN_MODE") >= 0) { ok = true; break; } }
      yield();
    }
    server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"mesaj\":\"" + String(ok?("Pin "+String(pin)+" -> "+mod):"Nano yanit vermedi") + "\",\"reply\":\"" + r + "\"}");
  });
  server.on("/pin/write", []() {
    if (!server.hasArg("pin") || !server.hasArg("val")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"pin ve val gerekli\"}"); return; }
    int pin = server.arg("pin").toInt();
    if (pinKorumali(pin)) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Bu pin donanima ayrilmis (kapi/role/PIR/lamba), genel GPIO API ile degistirilemez\"}"); return; }
    int val = server.arg("val").toInt();
    while (Serial.available()) Serial.read();
    Serial.print("PIN_WRITE:"); Serial.print(pin); Serial.print(","); Serial.println(val);
    unsigned long t = millis(); String r = ""; bool ok = false;
    while (millis() - t < 300) {
      if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("ACK:PIN_WRITE") >= 0) { ok = true; break; } }
      yield();
    }
    server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"mesaj\":\"" + String(ok?("Pin "+String(pin)+" = "+String(val)):"Nano yanit vermedi") + "\",\"reply\":\"" + r + "\"}");
  });
  server.on("/pin/read", []() {
    if (!server.hasArg("pin")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"pin gerekli\"}"); return; }
    int pin = server.arg("pin").toInt();
    if (pinKorumali(pin)) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Bu pin donanima ayrilmis (kapi/role/PIR/lamba), genel GPIO API ile okunamaz\"}"); return; }
    while (Serial.available()) Serial.read();
    Serial.print("PIN_READ:"); Serial.println(pin);
    unsigned long t = millis(); String r = ""; bool ok = false;
    while (millis() - t < 300) {
      if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("PIN:") >= 0) { ok = true; break; } }
      yield();
    }
    int deger = -1;
    if (ok) { int eq = r.indexOf('='); if (eq >= 0) deger = r.substring(eq+1).toInt(); }
    server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"pin\":" + String(pin) + ",\"deger\":" + String(deger) + ",\"reply\":\"" + r + "\"}");
  });
  // Buzzer'i (D12/NANO_BUZZER_PIN) elle test etmek icin - PIR'i tetiklemeden
  // "ses geliyor mu" diye aninda kontrol edebilmek icin (2026-08-27, kullanici
  // "ses gelmiyor" bulgusu sonrasi eklendi). Ornek: /buzzer/test?freq=3000&ms=500
  server.on("/buzzer/test", []() {
    int freq = server.hasArg("freq") ? server.arg("freq").toInt() : BUZZER_CHIRP_FREKANS_HZ;
    int sure = server.hasArg("ms") ? server.arg("ms").toInt() : BUZZER_CHIRP_SURE_MS;
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(freq); Serial.print(","); Serial.println(sure);
    unsigned long t = millis(); String r = ""; bool ok = false;
    while (millis() - t < 300) {
      if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("ACK:TONE_PLAY") >= 0) { ok = true; break; } if (r.indexOf("NACK") >= 0) break; }
      yield();
    }
    server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"freq\":" + String(freq) + ",\"ms\":" + String(sure) + ",\"reply\":\"" + r + "\"}");
  });
  server.on("/pin/readall", []() {
    while (Serial.available()) Serial.read();
    Serial.println("PIN_READ_ALL");
    unsigned long t = millis(); String r = ""; bool ok = false;
    while (millis() - t < 500) {
      if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("PIN:") >= 0) { ok = true; break; } }
      yield();
    }
    server.send(200, "application/json", "{\"basarili\":" + String(ok?"true":"false") + ",\"durum\":\"" + r + "\"}");
  });
  server.on("/rs485/debug", []() { String j = "{\"sonMsj\":\"" + sonRS485AlinanMsj + "\",\"yas_ms\":" + String(millis() - sonRS485AlinanMs) + ",\"lamba\":" + String(lambaAcik ? "true" : "false") + ",\"nanoBagli\":" + String(nanoBaglantiVar ? "true" : "false") + "}"; server.send(200, "application/json", j); });
  server.on("/ota", handleOTAUpdate);
  server.on("/ota/github", handleOTAGithub);
  server.on("/restart", handleRestart);
  server.on("/update", HTTP_POST, handleFileUploadUpdate, handleFileUploadProgress);
  server.begin();
  sonOtomatikOlcumMs = millis();
  
  // ===== BILGILER =====
  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("✅ SISTEM BILGILERI");
  DEBUG_PRINTLN("========================================");
  DEBUG_PRINTF("📡 AP SSID: %s\n", WIFI_AP_SSID);
  DEBUG_PRINTF("📡 AP Sifre: %s\n", WIFI_AP_PASSWORD);
  DEBUG_PRINTF("📡 AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  if (strlen(wifiAyar.ssid) > 0) {
    DEBUG_PRINTF("📶 STA SSID: %s\n", wifiAyar.ssid);
    DEBUG_PRINTF("📶 STA Sifre: %s\n", wifiAyar.sifre);
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTF("📶 STA IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
      DEBUG_PRINTLN("📶 STA: Baglanamadi");
    }
  } else {
    DEBUG_PRINTLN("📶 STA: Tanimli degil");
  }
  DEBUG_PRINTF("🌐 mDNS: %s.local\n", host.c_str());
  DEBUG_PRINTF("🔧 OTA Host: %s\n", host.c_str());
  DEBUG_PRINTF("🔧 OTA Sifre: %s\n", OTA_PASSWORD);
  DEBUG_PRINTF("🔧 OTA Port: 3232\n");
  DEBUG_PRINTF("🌍 Web: http://%s.local\n", host.c_str());
  DEBUG_PRINTF("🌍 Web AP: %s\n", WiFi.softAPIP().toString().c_str());
  DEBUG_PRINTLN("========================================\n");
}

// ============ LOOP ============
void loop() {
  ArduinoOTA.handle(); server.handleClient();

  // AP her zaman acik olmali (STA baglantisindan bagimsiz). Nadiren heap
  // parcalanmasi/RF sorunu ile AP dusebiliyor - periyodik kontrol edip
  // gerekirse yeniden baslatiyoruz.
  static unsigned long sonApKontrolMs = 0;
  unsigned long simdiMs = millis();
  if (simdiMs - sonApKontrolMs >= 30000UL) {
    sonApKontrolMs = simdiMs;
    if (!(WiFi.getMode() & WIFI_AP)) {
      DEBUG_PRINTLN("[WIFI] AP kapaliydi, yeniden baslatiliyor");
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, WIFI_AP_HIDDEN);
    }
  }

  // RTC bagliliğini periyodik yeniden dene. Sadece setup()'ta bir kez
  // denenirse, boot anindaki gecici bir I2C hazirsizlik/gecikme sorunu
  // rtcHazir'i kalici olarak false'ta biraktiriyordu - sonraki reboot'a
  // kadar hicbir zaman duzelmiyordu ("bazen calisiyor bazen calismiyor"
  // davranisinin sebebi buydu). Simdi 30sn'de bir tekrar denenir.
  static unsigned long sonRtcDenemeMs = 0;
  if (!rtcHazir && simdiMs - sonRtcDenemeMs >= 30000UL) {
    sonRtcDenemeMs = simdiMs;
    Wire.begin(RTC_SDA, RTC_SCL);
    if (rtc.begin()) {
      rtcHazir = true;
      DEBUG_PRINTLN("[RTC] Yeniden baglanildi");
    }
  }

  // FIX: WiFi koparsa mDNS'i yeniden baslat (STA modu icin)
  if (strlen(wifiAyar.ssid) > 0) {
    if (WiFi.status() == WL_CONNECTED && !mdnsAktif) {
      String host = mdnsHostname();
      if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsAktif = true;
        DEBUG_PRINTF("[mDNS] Yeniden baslatildi: %s.local\n", host.c_str());
      }
    } else if (WiFi.status() != WL_CONNECTED) {
      mdnsAktif = false;
    }
  }

  MDNS.update();
  nanoPoll();
  server.handleClient();  // FIX: Bloklayıcı nanoPoll sonrası web isteklerini işle
  rs485KomutDinle();
  server.handleClient();  // FIX: RS485 dinleme sonrası web isteklerini işle
  // ALARM KONTROLÜ - nanoMesgul kontrolü blink'i önler (komut beklerken yeni gönderme)
  if (!nanoMesgul) {
    // PANIK MODU - toggle, sürekli komut gönderme (her seyin onunde, mod/susturma etkilemez)
    if (panicRoleAktif) {
      alarmTetikleyenMask = 0; // panikte sensor tetikleyicisi yok, elle acildi
      alarmCikisLambaIstenen = true; // panikte her zaman hem siren hem lamba
      sirenEpisodeBaslangicMs = 0; sirenFaz = 0; sirenFazBaslangicMs = 0; lambaSurekliBaslangicMs = 0; // panik kademeli zamanlama desenini atlar - sonraki normal tetiklenme sifirdan baslasin
      if (!roleFizikselDurum) nanoRoleKontrol(true);
    } else if (!ayar.alarmRoleAktif) {
      // Alarm sistemi kapali: hicbir tetikleyici sirene/roleye yansimamali
      alarmSusturuldu = false; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmOnaySadeceLamba = false;
      alarmTetikleyenMask = 0;
      alarmCikisLambaIstenen = false;
      sirenEpisodeBaslangicMs = 0; sirenFaz = 0; sirenFazBaslangicMs = 0; lambaSurekliBaslangicMs = 0;
      if (roleFizikselDurum) nanoRoleKontrol(false);
    } else {
      // Zaman bazli (gunduz/gece) VE secili modun kendi senaryosu (hangi sensorler
      // bu modu tetikler) birlikte degerlendirilir.
      uint8_t zamanMask = geceModuMu() ? ayar.alarmTriggerGece : ayar.alarmTriggerGunduz;
      uint8_t modMask;
      switch (ayar.alarmMod) {
        case ALARM_MOD_SESSIZ: modMask = ayar.alarmMaskSessiz; break;
        case ALARM_MOD_ONAYLI: modMask = ayar.alarmMaskOnayli; break;
        default:                modMask = ayar.alarmMaskSesli; break;
      }
      // alarmSensorEtkin: mod/zamandan BAGIMSIZ genel ac/kapa - kablosuz/arizali
      // bir sensoru buradan tamamen devre disi birakmak icin (kullanici talebi,
      // Konteyner Zonu'ndaki sensor anahtarlariyla ayni mantik).
      uint8_t mask = zamanMask & modMask & ayar.alarmSensorEtkin;
      bool triggerActive = false;
      uint8_t tetikleyenMask = 0;
      if ((mask & ALARM_TRIGGER_KAPI1) && kapi1Acik) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KAPI1; }
      if ((mask & ALARM_TRIGGER_KAPI2) && kapi2Acik) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KAPI2; }
      if ((mask & ALARM_TRIGGER_PIR) && pirTetikleyici) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_PIR; }
      if ((mask & ALARM_TRIGGER_SU_SEVIYE) && alarmAktif) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_SU_SEVIYE; }
      if ((mask & ALARM_TRIGGER_KACAK) && kacakAlarmi) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KACAK; }
      if ((mask & ALARM_TRIGGER_SENSOR) && sensorHatasi) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_SENSOR; }
      alarmTetikleyenMask = tetikleyenMask;

      bool lambaHedefHam = false; // bu turun ham lamba hedefi - MIN sure asagida uygulanir
      if (!triggerActive) {
        // Tetikleyici temizlendi - susturma/onay durumlari sifirlanir (bir sonraki
        // tetiklenmede modun varsayilan davranisi yeniden gecerli olsun)
        alarmSusturuldu = false; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmOnaySadeceLamba = false;
        sirenEpisodeBaslangicMs = 0; sirenFaz = 0; sirenFazBaslangicMs = 0; lambaSurekliBaslangicMs = 0;
        if (roleFizikselDurum) nanoRoleKontrol(false);
      } else {
        bool sirenSeciliHam; // susturma/chirp'ten BAGIMSIZ, sadece mod/cikis secimine gore siren istenip istenmedigi
        // Sesli/Sessiz modda hangi cikislarin (siren/lamba) aktif olacagi artik
        // kullanicinin Alarm Ayarlari'ndaki "Mod Senaryolari" cikis secimlerinden
        // (ayar.alarmOutputSesli/Sessiz) geliyor - eskiden Sessiz mod icin siren
        // VE lamba sabit/hardcoded kapaliydi, "sadece bildirim" hic gorunmuyordu.
        if (ayar.alarmMod == ALARM_MOD_SESSIZ) {
          sirenSeciliHam = (ayar.alarmOutputSessiz & ALARM_OUTPUT_SIREN) != 0;
          lambaHedefHam = (ayar.alarmOutputSessiz & ALARM_OUTPUT_LAMBA) != 0;
        } else if (ayar.alarmMod == ALARM_MOD_ONAYLI) {
          if (alarmOnaySadeceLamba) {
            sirenSeciliHam = false; lambaHedefHam = true; // sadece lamba flaşörü - siren/role calismaz
          } else if (!alarmOnaylandi) {
            alarmOnayBekliyor = true; sirenSeciliHam = false; lambaHedefHam = false;
          } else {
            // BUG (kullanici talebi): lamba onceden dogrudan sirenIstenen'i
            // (yani susturmayi) izliyordu - Sustur basilinca lamba da
            // sonuyordu. Simdi Sesli/Sessiz ile TUTARLI: susturma SADECE
            // sireni etkiler, lamba eskale surdukce yanmaya devam eder.
            sirenSeciliHam = true; lambaHedefHam = true;
          }
        } else {
          // Sesli mod
          sirenSeciliHam = (ayar.alarmOutputSesli & ALARM_OUTPUT_SIREN) != 0;
          lambaHedefHam = (ayar.alarmOutputSesli & ALARM_OUTPUT_LAMBA) != 0;
        }
        // Su seviyesi dusuk bir guvenlik tehdidi degil (kullanici talebi) -
        // siren/lamba TETIKLEMESIN, sadece banner/mesaj olarak gorunsun.
        // Ayni anda baska bir tetikleyici (kapi/PIR/kacak/sensor) da aktifse
        // o normal siren/lamba davranisini tetiklemeye devam eder.
        if ((tetikleyenMask & ~ALARM_TRIGGER_SU_SEVIYE) == 0) {
          sirenSeciliHam = false;
          lambaHedefHam = false;
        }
        // Kademeli zamanlama deseni (kullanici talebi, "yanlis tetiklerden
        // etkilenmemek icin", Konteyner/ESP32 ile ayni mantik): tetik
        // baslangicindan ayar.sirenGecikmeSaniye sonra kisa ayar.sirenChirpMs'lik
        // bir "chirp" calar, sonra ayar.sirenBeklemeSaniye sessiz kalir - tetik
        // hala suruyorsa ayar.sirenAktifSaniye boyunca SUREKLI calar; bu
        // bekleme/aktif dongusu tetik bitene kadar TEKRARLANIR.
        bool sirenIstenen;
        if (sirenSeciliHam) {
          unsigned long simdiMs = millis();
          if (sirenEpisodeBaslangicMs == 0) {
            sirenEpisodeBaslangicMs = simdiMs;
            sirenFaz = 0;
            sirenFazBaslangicMs = simdiMs;
          }
          unsigned long gecikmeMs = (unsigned long)ayar.sirenGecikmeSaniye * 1000UL;
          unsigned long chirpMs = (unsigned long)ayar.sirenChirpMs;
          unsigned long beklemeMs = (unsigned long)ayar.sirenBeklemeSaniye * 1000UL;
          unsigned long aktifMs = (unsigned long)ayar.sirenAktifSaniye * 1000UL;
          switch (sirenFaz) {
            case 0: // ilk gecikme - sessiz
              sirenIstenen = false;
              if (simdiMs - sirenFazBaslangicMs >= gecikmeMs) { sirenFaz = 1; sirenFazBaslangicMs = simdiMs; }
              break;
            case 1: // chirp
              sirenIstenen = true;
              if (simdiMs - sirenFazBaslangicMs >= chirpMs) { sirenFaz = 2; sirenFazBaslangicMs = simdiMs; }
              break;
            case 2: // bekleme (chirp sonrasi VEYA aktif-periyot sonrasi)
              sirenIstenen = false;
              if (simdiMs - sirenFazBaslangicMs >= beklemeMs) { sirenFaz = 3; sirenFazBaslangicMs = simdiMs; }
              break;
            default: // 3: tam aktif
              sirenIstenen = true;
              if (simdiMs - sirenFazBaslangicMs >= aktifMs) { sirenFaz = 2; sirenFazBaslangicMs = simdiMs; }
              break;
          }
        } else {
          sirenEpisodeBaslangicMs = 0;
          sirenFaz = 0; sirenFazBaslangicMs = 0;
          sirenIstenen = false;
        }
        sirenIstenen = sirenIstenen && !alarmSusturuldu;
        // Sensor arizasi/unutulmus tetiklenmede siren SINIRSIZ calmasin diye
        // EPIZOT BASLANGICINDAN itibaren gecen TOPLAM sure SIREN_MAX_SURE_MS'i
        // asarsa otomatik susturulur (panik haric - panik zaten bu blogun
        // disinda, kendi ayri dalinda). Kademeli zamanlama nedeniyle
        // sirenIstenen bekleme fazlarinda sik sik false oldugundan, olcum
        // "kesintisiz calma" yerine sirenEpisodeBaslangicMs referans alinarak
        // yapilir (aksi halde 2dk'ya asla ulasilamaz).
        if (sirenEpisodeBaslangicMs != 0 && millis() - sirenEpisodeBaslangicMs > SIREN_MAX_SURE_MS) {
          alarmSusturuldu = true;
          sirenIstenen = false;
        }
        if (sirenIstenen && !roleFizikselDurum) nanoRoleKontrol(true);
        else if (!sirenIstenen && roleFizikselDurum) nanoRoleKontrol(false);
      }
      // Lamba MIN sure: Sustur basilsin ya da tetikleyici erken temizlensin,
      // lamba en az LAMBA_MIN_SURE_MS boyunca acik kalmaya devam eder
      // (kullanici talebi: "en az 60sn lambalar yansın").
      if (lambaHedefHam) {
        lambaMinSureBaslangicMs = millis();
        alarmCikisLambaIstenen = true;
      } else if (lambaMinSureBaslangicMs != 0 && millis() - lambaMinSureBaslangicMs < LAMBA_MIN_SURE_MS) {
        alarmCikisLambaIstenen = true;
      } else {
        lambaMinSureBaslangicMs = 0;
        alarmCikisLambaIstenen = false;
      }
      // Lamba MAX sure (kullanici talebi, enerji butcesi): alarm-tetikli
      // lamba kesintisiz LAMBA_MAX_SURE_MS'i (10dk) asarsa zorla soner -
      // siren'deki 2dk oto-sustur deseniyle simetrik. Manuel lamba acma
      // (lambaAcik/toggleLamba) ayri bir kod yolundan gittigi icin
      // alarmCikisLambaIstenen'e hic girmez, bu bloktan etkilenmez.
      if (alarmCikisLambaIstenen) {
        if (lambaSurekliBaslangicMs == 0) lambaSurekliBaslangicMs = millis();
        else if (millis() - lambaSurekliBaslangicMs > LAMBA_MAX_SURE_MS) alarmCikisLambaIstenen = false;
      } else {
        lambaSurekliBaslangicMs = 0;
      }
    }
  }
  // Buzzer - bkz nanoBuzzerKontrolPoll yorumu: banner (alarmGenelKutu) YENI
  // gorunur oldugunda tek bir bip. app.js'deki alarmSimdiVar ile AYNI kosul.
  {
    bool bannerAlarmVar = (alarmTetikleyenMask != 0) || alarmOnayBekliyor || panicRoleAktif;
    if (bannerAlarmVar && !bannerAlarmOncekiDurum) buzzerChirpBekliyor = true;
    bannerAlarmOncekiDurum = bannerAlarmVar;
  }
  alarmLoguKontrolEt();
  // ALARM LAMBA FLASI - role fiziksel olarak tetikliyken (panik dahil) lamba
  // yanip soner; role kapaninca lamba tetiklenmeden onceki durumuna doner.
  // Kendi !nanoMesgul kontrolünü ayrı yapar ki yukarıdaki role komutu
  // kuyruğu meşgul ettiğinde bu döngüde beklesin, bir sonrakinde devam etsin.
  // Lamba, artik yukarida hesaplanan alarmCikisLambaIstenen'e gore yanip
  // sonuyor (mod cikis ayarindan geliyor) - roleFizikselDurum'a bagimli
  // DEGIL, boylece "siren acik, lamba kapali" gibi kombinasyonlar da
  // dogru calisir (eskiden lamba sadece siren fiziksel olarak tetiklenince
  // yanip soner, Sessiz modda hicbir zaman yanmazdi).
  if (alarmCikisLambaIstenen) {
    if (!lambaFlashAktif) {
      lambaFlashAktif = true;
      lambaFlashOncekiManuel = lambaAcik;
      lambaFlashSonDegisimMs = millis();
    } else if (!nanoMesgul && millis() - lambaFlashSonDegisimMs >= LAMBA_FLASH_ARALIK_MS) {
      lambaFlashSonDegisimMs = millis();
      nanoLambaKontrol(!lambaAcik);
    }
  } else if (lambaFlashAktif) {
    // FIX: lambaFlashAktif eskiden burada kosulsuz false yapiliyordu - alarm
    // tam bu anda role kapatma komutuyla (yukarida) ayni donguye denk gelip
    // nanoMesgul mesgul ciktiginda geri donus komutu hic gonderilmiyor ve bir
    // daha da denenmiyordu (lambaFlashAktif zaten false, bu blok tekrar
    // calismiyordu) - lamba flaşin son anindaki (rastgele acik/kapali)
    // durumunda kilitli kaliyordu. Simdi nanoMesgul bosalana kadar her
    // dongude tekrar denenir, basarili gonderilince flag temizlenir.
    if (!nanoMesgul) {
      nanoLambaKontrol(lambaFlashOncekiManuel);
      lambaFlashAktif = false;
    }
  }
  server.handleClient();  // FIX: Röle kontrol sonrası web isteklerini işle
  unsigned long s = millis();
  uint16_t a = geceModuMu() ? ayar.olcumAraligiGece : ayar.olcumAraligiGunduz;
  if (s - sonOtomatikOlcumMs >= (unsigned long)a * 1000UL) { olcumYap(); sonOtomatikOlcumMs = s; ssePush(); server.handleClient(); }
  // SSE periyodik durum guncelleme - onceden 1500ms'de bir moistureOku()
  // (gercek analogRead) cagiriyordu, yani nem sensoru gereksiz sik
  // okunuyordu (yorumun soyledigi "olcum yapmadan sadece durum iter"in
  // aksine). Kullanici istegiyle 4000ms'e yavaslatildi - ekranda hala
  // akici hissettirecek kadar sik, ama analogRead sıklığı ~2.7 kat azaldi.
  static unsigned long sseGonderMs = 0;
  if (s - sseGonderMs >= 4000UL) { sseGonderMs = s; moistureOku(); ssePush(); }
  // KALDIRILDI: Periyodik (1000ms) istem-disi masterGonder() gonderimi.
  // ESP32 zaten kendi 600ms'lik dongusunde GET_STATUS ile surekli soruyor
  // (bkz esp32_master rs485_poll) - bu "yedek" gonderim aslinda ESP32'nin
  // istegiyle SENKRONIZE OLMADAN, paylasilan yari-cift-yonlu RS485 hattina
  // ikinci, kontrolsuz bir vericiydi. ESP32 tarafinda [RS485_TIMING] tani
  // loguyla dogrulandi: SET_LAMBA gibi komutlar bazen tek denemede 40ms'de
  // basariyorken, bazen 1-3 saniyeye (2-3 yeniden deneme) uzuyordu - suresi
  // rastgele degisen bu orunun, iki bagimsiz zamanlayicinin hatta cakismasiyla
  // (bu gonderim + ESP32'nin sorgusu/komutu ayni ana denk gelince) uyustugu
  // gorulmustu. ESP32'nin poll'u zaten 600ms'de bir taze veri getirdigi icin
  // bu "yedek" gereksizdi - kaldirilmasi hem cakismayi hem gecikmeyi azaltmali.
  server.handleClient();
}