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
#include <Updater.h>
#include <SoftwareSerial.h>
#include "config.h"

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
  uint8_t moistureAutomatic;
  uint8_t moistureThresholdLow;
  uint8_t moistureThresholdHigh;
  uint8_t pirOnaySaniye;  // PIR bu kadar saniye KESINTISIZ aktif kalirsa tetiklenir (dal sallanmasi vb. filtrelenir)
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
  ayar.moistureAutomatic = 0;
  ayar.moistureThresholdLow = 40;
  ayar.moistureThresholdHigh = 70;
  ayar.pirOnaySaniye = 2;
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
  }
}

// ============ OLCUM DEGISKENLERI ============
float sonSeviyeCm = 0, sonYuzde = 0, sonLitre = 0;
String sonOlcumZamani = "-";

// ============ DOLUM TESPITI ============
#define NOISE_ESIK_LITRE 3.0
bool ilkOlcumTamamlandi = false;
float oncekiSonLitre = 0;
bool dolumDevamEdiyor = false;
float dolumBaslangicLitre = 0, dolumSonPikLitre = 0;

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
bool alarmSusturuldu = false;   // Susturma - tetikleyici aktifken siren susturulur (mesaj/banner kalir)
bool alarmOnayBekliyor = false; // Mod 3 (Onayli): tetiklendi, onay bekleniyor
bool alarmOnaylandi = false;    // Mod 3 (Onayli): onaylandi, sesli mod gibi davranir

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

// ============ YAGMUR TAHMINI (ESP32-S3 master'dan) ============
// Kalici EEPROM alani degil - bilincli tercih: master zaten periyodik olarak
// tazeler, ESP8266 resetlenirse varsayilan "sulama normal calissin" (false)
// guvenli taraftir. Bkz. config.h RAIN_SKIP_STALE_MS.
bool yagmurSulamaAtla = false;
unsigned long yagmurSonGuncellemeMs = 0;

// ============ SSE ============
WiFiClient sseClient;
bool sseAktif = false;

// ============ NANO VERISI ============
bool kapi1Acik = false, kapi2Acik = false;
bool pirAcik = false;           // PIR sensörü hareket algısı (ham deger)
// PIR "onay suresi": ham deger kesintisiz en az ayar.pirOnaySaniye kadar
// aktif kalirsa pirTetikleyici true olur. Kisa/ani sinyalleri (ruzgarda dal
// sallanmasi, kus vb.) filtreler; gercek bir kisi/hayvan hareketi genelde
// PIR'i saniyelerce (modulun kendi hold-time'i kadar) aktif tutar.
bool pirTetikleyici = false;
unsigned long pirYukselmeMs = 0;
bool pirHamOnceki = false;
bool roleFizikselDurum = false;
bool roleTestAktif = false;
unsigned long roleTestBitisMs = 0;
bool nanoBaglantiVar = false;
bool lambaAcik = false;
bool moistureOutputActive = false;
int moistureRaw = 0;
float moisturePercent = 0.0;

// ============ RS485 ============
unsigned long sonRS485GonderMs = 0;
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
      // NOT: pirAcik burada degil, nanoPoll() icinde ayri PIN_READ ile guncellenir
      // (Nano firmware'i GET_STATUS yanitina PIR eklemez, D6 genel GPIO'dur)
      roleFizikselDurum  = (yanit.indexOf("RELE=1") >= 0);
      lambaAcik          = (yanit.indexOf("LAMBA=1") >= 0);
      moistureOutputActive = (yanit.indexOf("MOISTURE=1") >= 0);
  int raw_idx = yanit.indexOf("MOISTURE_RAW=");
  if (raw_idx >= 0) {
    int end_idx = yanit.indexOf(',', raw_idx);
    String raw_str = (end_idx >= 0) ? yanit.substring(raw_idx + 13, end_idx) : yanit.substring(raw_idx + 13);
    moistureRaw = raw_str.toInt();
  }
  int pct_idx = yanit.indexOf("MOISTURE_PCT=");
  if (pct_idx >= 0) {
    int end_idx = yanit.indexOf(',', pct_idx);
    String pct_str = (end_idx >= 0) ? yanit.substring(pct_idx + 13, end_idx) : yanit.substring(pct_idx + 13);
    moisturePercent = pct_str.toFloat();
  }
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

  // --- Periyodik GET_STATUS ---
  if (simdi - son < NANO_POLL_INTERVAL) return;
  son = simdi;

  while (Serial.available()) Serial.read(); // Temiz başla
  Serial.print("GET_STATUS\n");

  String yanit = nanoYanitOku(80);
  if (yanit.length() > 0) {
    nanoStatusAyristir(yanit);

    // PIR sensörü Nano D6'da (genel yedek GPIO) - Nano firmware'i degistirmeden
    // mevcut PIN_READ mekanizmasiyla okunur. Nano varsayilan olarak butun pinler
    // INPUT durumunda acilir, PIN_MODE ayarina gerek yok.
    while (Serial.available()) Serial.read();
    Serial.print("PIN_READ:"); Serial.println(PIR_NANO_PIN);
    String pirYanit = nanoYanitOku(80);
    int esit = pirYanit.indexOf('=');
    if (pirYanit.startsWith("PIN:") && esit >= 0) {
      pirAcik = (pirYanit.substring(esit + 1).toInt() == 1);
      // Onay suresi: ham deger yukselirken zaman damgasi al, kesintisiz
      // yeterince uzun kaldiysa tetikleyiciyi aktif et. 0 saniye = filtre yok.
      if (pirAcik && !pirHamOnceki) pirYukselmeMs = millis();
      if (!pirAcik) {
        pirTetikleyici = false;
      } else if (ayar.pirOnaySaniye == 0) {
        pirTetikleyici = true;
      } else if (millis() - pirYukselmeMs >= (unsigned long)ayar.pirOnaySaniye * 1000UL) {
        pirTetikleyici = true;
      }
      pirHamOnceki = pirAcik;
    }
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

void moistureOku() {
  moistureRaw = analogRead(A0);
  moisturePercent = 100.0 - (moistureRaw * 100.0 / 1023.0);
  if (moisturePercent < 0) moisturePercent = 0;
  if (moisturePercent > 100) moisturePercent = 100;
}

bool yagmurSulamaAtlaGecerli() {
  return yagmurSulamaAtla && (millis() - yagmurSonGuncellemeMs < RAIN_SKIP_STALE_MS);
}

void applyMoistureControl() {
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
  // FIX: Mesaj ~230 byte, 160 byte buffer'a sığmıyordu - RS485 verisi kesiliyordu
  char buf[320];
  snprintf(buf, sizeof(buf),
    "ESP8266:LEVEL=%.1f,PCT=%.1f,LITRE=%.0f,TEMP=%.1f,MODE=%s,K1=%d,K2=%d,R=%d,LAMBA=%d,ALARM=%d,ERR=%d,RTC=%d,LEAK=%d,LEAK_DK=%lu,FILL=%d,MOISTURE_RAW=%d,MOISTURE_PCT=%.1f,MOISTURE_OUTPUT=%d,MOISTURE_AUTO=%d,MOISTURE_LOW=%d,MOISTURE_HIGH=%d,ALARM_MOD=%d,ALARM_MUTE=%d,ALARM_PENDING=%d,PANIC=%d,TRIG_MASK=%d,RAIN_SKIP=%d\n",
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
    yagmurSulamaAtlaGecerli() ? 1 : 0
  );
  rs485Gonder(buf);
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
        } else if (komut == "ROLE_TEST") {
          nanoRoleKontrol(true);
          delay(2000);
          nanoRoleKontrol(false);
          DEBUG_PRINTLN("[RS485] Role test OK");
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
          bool ok = nanoMoistureKontrol(durum == 1);
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
        } else if (komut.startsWith("SET_KAPI=")) {
          bool kapiDurum = (komut.substring(9) == "1");
          bool ok = nanoRoleKontrol(kapiDurum);
          response = (ok ? "ACK:" : "NACK:") + komut;
        } else if (komut.startsWith("SET_ALARM_MOD=")) {
          int m = komut.substring(14).toInt();
          if (m >= 1 && m <= 3) {
            ayar.alarmMod = m; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmSusturuldu = false;
            ayarlariKaydet();
            response = "ACK:" + komut;
          } else {
            response = "NACK:" + komut;
          }
        } else if (komut == "ALARM_MUTE") {
          alarmSusturuldu = !alarmSusturuldu;
          response = "ACK:" + komut;
        } else if (komut == "ALARM_ONAYLA") {
          alarmOnaylandi = true; alarmOnayBekliyor = false;
          response = "ACK:" + komut;
        } else if (komut == "PANIC") {
          // ESP32'nin /api/panic'i bunu bekler: web /role/panic ile ayni
          // toggle davranisi, ACK icinde yeni durumu da doner (PANIC=1/0).
          panicRoleAktif = !panicRoleAktif;
          bool ok = nanoRoleKontrol(panicRoleAktif);
          response = (ok ? "ACK:" : "NACK:") + String("PANIC=") + (panicRoleAktif ? "1" : "0");
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
        } else if (komut == "ROLE_TEST") {
          bool ok1 = nanoRoleKontrol(true);
          delay(2000);
          bool ok2 = nanoRoleKontrol(false);
          response = ((ok1 && ok2) ? "ACK:" : "NACK:") + komut;
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
      float fark = sonLitre - oncekiSonLitre;
      if (fark > NOISE_ESIK_LITRE) {
        if (!dolumDevamEdiyor) { dolumDevamEdiyor = true; dolumBaslangicLitre = oncekiSonLitre; }
        dolumSonPikLitre = sonLitre;
        kacakSuruyor = false; kacakAlarmi = false;
      } else {
        if (dolumDevamEdiyor) {
          float toplamArtis = dolumSonPikLitre - dolumBaslangicLitre;
          if (toplamArtis >= ayar.minDolumLitre) {
            File f = LittleFS.open(KAYIT_DOSYASI, "a");
            if (f) {
              f.print(simdikiTarihISO()); f.print(",");
              f.print("Otomatik Tespit,"); f.print(toplamArtis, 1); f.print(",0,Belirsiz\n");
              f.close();
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
      tuketimYukle();
    }
    oncekiSonLitre = sonLitre;

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

bool kayitEkle(String tarih, String kisi, float litre, float ucret, String kaynak) {
  File f = LittleFS.open(KAYIT_DOSYASI, "a");
  if (!f) return false;
  f.print(csvTemizle(tarih)); f.print(",");
  f.print(csvTemizle(kisi)); f.print(",");
  f.print(litre, 1); f.print(",");
  f.print(ucret, 2); f.print(",");
  f.println(csvTemizle(kaynak));
  f.close();
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
  String j = "{";
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
   j += "\"alarmTetikleyenMask\":" + String(alarmTetikleyenMask) + ",";
   j += "\"triggerGunduz\":" + String(ayar.alarmTriggerGunduz) + ",";
   j += "\"triggerGece\":" + String(ayar.alarmTriggerGece) + ",";
   j += "\"alarmMod\":" + String(ayar.alarmMod) + ",";
   j += "\"alarmSusturuldu\":" + String(alarmSusturuldu ? "true" : "false") + ",";
   j += "\"alarmOnayBekliyor\":" + String(alarmOnayBekliyor ? "true" : "false") + ",";
   j += "\"alarmOnaylandi\":" + String(alarmOnaylandi ? "true" : "false") + ",";
   j += "\"yagmurSulamaAtla\":" + String(yagmurSulamaAtlaGecerli() ? "true" : "false");
   j += "}";
  return j;
}

// ============ CSS ============
void handleCSS() {
  String css = ":root{--bg1:#e3f2fd;--bg2:#bbdefb;--card:#fff;--text:#222;--muted:#666;--primary:#1976d2;--accent:#4CAF50;--warn:#FF9800;--danger:#e53935;--danger-bg:#ffebee;--input-bg:#fff;--input-border:#ddd;--tab-bg:#eef3f8;--shadow:rgba(0,0,0,.1)}";
  css += ".dark{--bg1:#0f1720;--bg2:#182430;--card:#1c2733;--text:#e8eef4;--muted:#9aa7b4;--primary:#64b5f6;--accent:#66bb6a;--warn:#ffb74d;--danger:#ef5350;--danger-bg:#3a2222;--input-bg:#243140;--input-border:#3a4a5a;--tab-bg:#243140;--shadow:rgba(0,0,0,.4)}";
  css += "*{margin:0;padding:0;box-sizing:border-box}";
  css += "body{font-family:-apple-system,sans-serif;background:linear-gradient(135deg,var(--bg1),var(--bg2));min-height:100vh;padding:16px;color:var(--text);max-width:460px;margin:0 auto;transition:background .3s,color .3s}";
  css += ".topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px}";
  css += ".topbar h1{color:var(--primary);font-size:22px}";
  css += ".tema-btn{background:var(--card);border:none;border-radius:50%;width:42px;height:42px;font-size:18px;cursor:pointer;box-shadow:0 2px 8px var(--shadow)}";
  css += ".sekmeler{display:flex;background:var(--tab-bg);border-radius:14px;padding:4px;margin-bottom:16px}";
  css += ".sekme-btn{flex:1;border:none;background:transparent;padding:10px 4px;border-radius:10px;font-size:13px;color:var(--muted);cursor:pointer}";
  css += ".sekme-btn.aktif{background:var(--card);color:var(--primary);font-weight:bold;box-shadow:0 2px 6px var(--shadow)}";
  css += ".card{background:var(--card);border-radius:18px;padding:20px;margin-bottom:16px;box-shadow:0 6px 20px var(--shadow)}";
  css += ".card h3{color:var(--primary);margin-bottom:14px;font-size:16px}";
  css += ".zaman-bilgisi{background:var(--tab-bg);padding:10px;border-radius:10px;margin-bottom:14px;font-size:12px;color:var(--muted);text-align:center}";
  css += ".alarm-kutu{background:var(--danger-bg);color:var(--danger);border:2px solid var(--danger);padding:10px;border-radius:10px;margin-bottom:14px;font-weight:bold;text-align:center;animation:pulse 1.5s infinite}";
  css += "@keyframes pulse{0%{opacity:1}50%{opacity:.6}100%{opacity:1}}";
  css += ".depo-container{display:flex;justify-content:center;margin:10px 0 20px}";
  css += ".depo{width:170px;height:230px;background:var(--tab-bg);border-radius:20px 20px 10px 10px;position:relative;overflow:hidden;border:3px solid var(--primary)}";
  css += ".su{position:absolute;bottom:0;left:0;right:0;background:linear-gradient(180deg,#42a5f5,#1976d2);transition:height 1s;border-radius:0 0 8px 8px}";
  css += ".su-dalga{position:absolute;bottom:100%;left:-50%;width:200%;height:15px;background:rgba(255,255,255,.3);border-radius:50%;animation:dalga 3s ease-in-out infinite}";
  css += "@keyframes dalga{0%{transform:translateX(0)}50%{transform:translateX(25%) translateY(-5px)}100%{transform:translateX(0)}}";
  css += ".seviye-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);font-size:22px;font-weight:bold;color:#fff;text-shadow:0 2px 10px rgba(0,0,0,.5);z-index:2}";
  css += ".info{padding:14px;background:var(--tab-bg);border-radius:12px;margin:10px 0}";
  css += ".info p{margin:6px 0;font-size:15px}";
  css += ".info strong{color:var(--primary)}";
  css += ".btn-satir{display:flex;gap:8px;margin:16px 0 6px}";
  css += ".btn{flex:1;color:white;padding:12px 10px;border-radius:12px;border:none;font-size:14px;cursor:pointer;font-weight:600}";
  css += ".btn:active{transform:scale(.96)}";
  css += ".btn-yesil{background:var(--accent)}.btn-turuncu{background:var(--warn)}.btn-mavi{background:var(--primary);width:100%;margin-top:12px}.btn-kirmizi{background:var(--danger);width:100%;margin-top:10px}";
  css += ".btn-satir .btn-mavi,.btn-satir .btn-kirmizi{width:auto;margin-top:0}";
  css += "label{display:block;font-size:12px;color:var(--muted);margin-top:10px}";
  css += "input,select{padding:11px;border:2px solid var(--input-border);border-radius:10px;font-size:14px;width:100%;margin-top:4px;background:var(--input-bg);color:var(--text)}";
  css += ".sonuc-metni{margin-top:10px;font-size:13px;text-align:center;color:var(--muted);min-height:16px}";
  css += ".muted{color:var(--muted);font-size:13px;text-align:center;padding:10px 0}";
  css += "table{width:100%;border-collapse:collapse;font-size:12px}";
  css += "th{text-align:left;color:var(--muted);padding:6px 4px;border-bottom:2px solid var(--tab-bg)}";
  css += "td{padding:8px 4px;border-bottom:1px solid var(--tab-bg)}";
  css += ".belirsiz-satir{background:rgba(255,152,0,.12)}";
  css += ".uyari-metni{color:var(--warn);font-weight:bold}";
  css += ".duzenle-form{display:flex;flex-wrap:wrap;gap:6px;padding:8px 0}";
  css += ".duzenle-form select,.duzenle-form input{flex:1;min-width:90px;margin-top:0;padding:8px}";
  css += ".duzenle-form button{width:auto;margin-top:0;padding:8px 14px}";
  css += ".btn-sil{background:none;border:none;cursor:pointer;font-size:14px}";
  server.send(200, "text/css", css);
}

// ============ JS (LittleFS'ten servis edilir) ============
// JS artik RAM'de String olarak kurulmuyor; LittleFS'teki /app.js
// dosyasindan serveStatic ile dogrudan istemciye gonderilir.
// Dinamik deger (const K=...) /config.js endpoint'inden gelir.

// ============ HTML (LittleFS'ten streamFile ile gonderilir) ============
// HTML artik RAM'de String olarak kurulmuyor; LittleFS'teki /index.html
// dosyasindan streamFile() ile dogrudan istemciye gonderilir.
// Dinamik degerler JS tarafinda /olc, /durum, /ayarlar API'lerinden cekilir.
// LittleFS'e yuklemek icin: pio run -t uploadfs
void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (f) {
    server.streamFile(f, "text/html");
    f.close();
  } else {
    server.send(404, "text/plain", "index.html yok! pio run -t uploadfs");
  }
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
  j += "\"bosMesafe\":" + String(ayar.bosMesafe,1) + ",\"doluMesafe\":" + String(ayar.doluMesafe,1) + ",\"kapasite\":" + String(ayar.depoKapasiteLitre,0) + ",\"alarmYuzde\":" + String(ayar.alarmSeviyeYuzde,0) + ",\"geceBaslangic\":" + String(ayar.geceBaslangicSaat) + ",\"geceBitis\":" + String(ayar.geceBitisSaat) + ",\"minDolumLitre\":" + String(ayar.minDolumLitre,0) + ",\"kacakEsikDakika\":" + String(ayar.kacakEsikDakika) + ",\"depoYatay\":" + String(ayar.depoYatay) + ",\"moistureAutomatic\":" + String(ayar.moistureAutomatic ? "true" : "false") + ",\"moistureThresholdLow\":" + String(ayar.moistureThresholdLow) + ",\"moistureThresholdHigh\":" + String(ayar.moistureThresholdHigh) + ",\"triggerGunduz\":" + String(ayar.alarmTriggerGunduz) + ",\"triggerGece\":" + String(ayar.alarmTriggerGece) + ",\"alarmMod\":" + String(ayar.alarmMod) + ",\"alarmMaskSesli\":" + String(ayar.alarmMaskSesli) + ",\"alarmMaskSessiz\":" + String(ayar.alarmMaskSessiz) + ",\"alarmMaskOnayli\":" + String(ayar.alarmMaskOnayli) + ",\"pirOnaySaniye\":" + String(ayar.pirOnaySaniye) + "}";
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
    if (m >= 1 && m <= 3) { ayar.alarmMod = m; alarmOnayBekliyor = false; alarmOnaylandi = false; alarmSusturuldu = false; }
  }
  if (server.hasArg("alarmMaskSesli")) ayar.alarmMaskSesli = server.arg("alarmMaskSesli").toInt();
  if (server.hasArg("alarmMaskSessiz")) ayar.alarmMaskSessiz = server.arg("alarmMaskSessiz").toInt();
  if (server.hasArg("alarmMaskOnayli")) ayar.alarmMaskOnayli = server.arg("alarmMaskOnayli").toInt();
  if (server.hasArg("pirOnaySaniye")) {
    int v = server.arg("pirOnaySaniye").toInt(); if (v < 0) v = 0; if (v > 30) v = 30; ayar.pirOnaySaniye = v;
  }
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
void handleRoleTest() { nanoRoleKontrol(true); delay(2000); nanoRoleKontrol(false); server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Role testi OK\"}"); }
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
  alarmOnaylandi = true; alarmOnayBekliyor = false;
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Onaylandi\"}");
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

void handleWifiKart() {
  // FIX: WiFi kartı ayrı endpoint'ten yüklenir - ana HTML'in heap taşması
  // nedeniyle WiFi bölümünün görünmemesi sorunu çözülür.
  String h = "<div class=card><h3>WiFi Ayarlari</h3><p class=muted>AP: " + String(WIFI_AP_SSID) + " / " + String(WIFI_AP_PASSWORD) + "</p><div class=info id=wifiDurumKutu>Yukleniyor...</div><label>SSID Sec</label><div style='display:flex;gap:8px'><select id=staSSID style='flex:1'></select><button class='btn btn-mavi' onclick=wifiScan()>Aglari Tara</button></div><label>Ya da elle girin</label><input type=text id=staSSIDManual placeholder='SSID girin'><label>Sifre</label><div style='position:relative'><input type=password id=staSifre style='width:100%;padding-right:52px'><button type=button id=pwToggleBtn onclick=togglePw() style='position:absolute;right:6px;top:6px;border:1px solid var(--input-border);border-radius:6px;background:var(--input-bg);color:var(--primary);font-size:11px;padding:6px 8px;cursor:pointer;font-weight:600'>Goster</button></div><div class=btn-satir><button class=btn-mavi onclick=wifiKaydet()>Baglan & Kaydet</button><button class=btn-kirmizi onclick=wifiKaldir()>Temizle</button></div><div id=wifiSonuc class=sonuc-metni></div></div>";
  server.send(200, "text/html", h);
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

// ============ JS ENDPOINT ============
void handleOTAUpdate() {
  if (!server.hasArg("url")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"URL eksik\"}"); return; }
  String url = server.arg("url");
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Guncelleniyor: "+url+"\"}");
  delay(100);
  WiFiClient client;
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);
  if (ret == HTTP_UPDATE_OK) {
    DEBUG_PRINTLN("OTA OK");
  } else {
    DEBUG_PRINTLN(String("OTA Hata: ") + String(ESPhttpUpdate.getLastErrorString()));
  }
}

// ============ DOSYADAN OTA (bin dosyasi web'den yuklenir) ============
void handleFileUploadUpdate() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  delay(100);
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

// /app.js LittleFS'ten servis edilir (statik dosya)
void handleJS() {
  File f = LittleFS.open("/app.js", "r");
  if (f) {
    server.streamFile(f, "application/javascript");
    f.close();
  } else {
    server.send(404, "text/plain", "app.js yok! pio run -t uploadfs");
  }
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
    if (!rtc.isrunning()) {
      DEBUG_PRINTLN("[RTC] DS1307 calismiyor - degistirin!");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  olcumYap();
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
  server.on("/role/test", handleRoleTest); server.on("/role/ayarla", handleRoleAyarla); server.on("/role/panic", handleRolePanic);
  server.on("/alarm/sustur", handleAlarmSustur); server.on("/alarm/onayla", handleAlarmOnayla);
  server.on("/role/polarite", handleRolePolarite);
  server.on("/wifi/durum", handleWifiDurum); server.on("/wifi/kaydet", handleWifiKaydet);
  server.on("/wifi/scan", handleWifiScan); server.on("/wifi/kart", handleWifiKart);
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
      if (!roleFizikselDurum) nanoRoleKontrol(true);
    } else if (!ayar.alarmRoleAktif) {
      // Alarm sistemi kapali: hicbir tetikleyici sirene/roleye yansimamali
      alarmSusturuldu = false; alarmOnayBekliyor = false; alarmOnaylandi = false;
      alarmTetikleyenMask = 0;
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
      uint8_t mask = zamanMask & modMask;
      bool triggerActive = false;
      uint8_t tetikleyenMask = 0;
      if ((mask & ALARM_TRIGGER_KAPI1) && kapi1Acik) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KAPI1; }
      if ((mask & ALARM_TRIGGER_KAPI2) && kapi2Acik) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KAPI2; }
      if ((mask & ALARM_TRIGGER_PIR) && pirTetikleyici) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_PIR; }
      if ((mask & ALARM_TRIGGER_SU_SEVIYE) && alarmAktif) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_SU_SEVIYE; }
      if ((mask & ALARM_TRIGGER_KACAK) && kacakAlarmi) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_KACAK; }
      if ((mask & ALARM_TRIGGER_SENSOR) && sensorHatasi) { triggerActive = true; tetikleyenMask |= ALARM_TRIGGER_SENSOR; }
      alarmTetikleyenMask = tetikleyenMask;

      if (!triggerActive) {
        // Tetikleyici temizlendi - susturma/onay durumlari sifirlanir (bir sonraki
        // tetiklenmede modun varsayilan davranisi yeniden gecerli olsun)
        alarmSusturuldu = false; alarmOnayBekliyor = false; alarmOnaylandi = false;
        if (roleFizikselDurum) nanoRoleKontrol(false);
      } else {
        bool sirenIstenen;
        if (ayar.alarmMod == ALARM_MOD_SESSIZ) {
          // Sessiz mod: role/siren hic calismaz, sadece web/SSE bildirimi (durumJson) gosterilir
          sirenIstenen = false;
        } else if (ayar.alarmMod == ALARM_MOD_ONAYLI) {
          if (!alarmOnaylandi) { alarmOnayBekliyor = true; sirenIstenen = false; }
          else sirenIstenen = !alarmSusturuldu;
        } else {
          // Sesli mod
          sirenIstenen = !alarmSusturuldu;
        }
        if (sirenIstenen && !roleFizikselDurum) nanoRoleKontrol(true);
        else if (!sirenIstenen && roleFizikselDurum) nanoRoleKontrol(false);
      }
    }
  }
  // ALARM LAMBA FLASI - role fiziksel olarak tetikliyken (panik dahil) lamba
  // yanip soner; role kapaninca lamba tetiklenmeden onceki durumuna doner.
  // Kendi !nanoMesgul kontrolünü ayrı yapar ki yukarıdaki role komutu
  // kuyruğu meşgul ettiğinde bu döngüde beklesin, bir sonrakinde devam etsin.
  if (roleFizikselDurum) {
    if (!lambaFlashAktif) {
      lambaFlashAktif = true;
      lambaFlashOncekiManuel = lambaAcik;
      lambaFlashSonDegisimMs = millis();
    } else if (!nanoMesgul && millis() - lambaFlashSonDegisimMs >= LAMBA_FLASH_ARALIK_MS) {
      lambaFlashSonDegisimMs = millis();
      nanoLambaKontrol(!lambaAcik);
    }
  } else if (lambaFlashAktif) {
    lambaFlashAktif = false;
    if (!nanoMesgul) nanoLambaKontrol(lambaFlashOncekiManuel);
  }
  server.handleClient();  // FIX: Röle kontrol sonrası web isteklerini işle
  unsigned long s = millis();
  uint16_t a = geceModuMu() ? ayar.olcumAraligiGece : ayar.olcumAraligiGunduz;
  if (s - sonOtomatikOlcumMs >= (unsigned long)a * 1000UL) { olcumYap(); sonOtomatikOlcumMs = s; ssePush(); server.handleClient(); }
  // SSE periyodik durum guncelleme (1500ms) - sensör olcumu yapmadan sadece durum iter
  static unsigned long sseGonderMs = 0;
  if (s - sseGonderMs >= 1500UL) { sseGonderMs = s; moistureOku(); ssePush(); }
  // Periyodik RS485 gonderimi (1000ms) - ESP32 poll'u kacirsa bile veri akisi devam eder
  static unsigned long sonMasterGonderMs = 0;
  if (s - sonMasterGonderMs >= RS485_SEND_INTERVAL) {
    sonMasterGonderMs = s;
    masterGonder();
    server.handleClient();  // FIX: RS485 gönderim sonrası web isteklerini işle
  }
}