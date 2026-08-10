// ============================================================
// SuDepoSis v2 - ESP32-S3 Master
// RS485 Hub + Web Dashboard + MQTT Publisher
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_system.h>

// NOT: IRremote.hpp kendi ic basliklarinda DEBUG_PRINT/DEBUG_PRINTLN adinda
// makrolar tanimlayip config.h'daki (asagida include edilen) bizim
// makrolarimizin UZERINE yaziyordu - butun dosyada bu makrolar tanimsiz hale
// geliyordu. IRremote.hpp'yi config.h'dan ONCE include ederek bizim
// tanimlarimizin (config.h) son/gecerli olanlar olmasini garantiliyoruz.
#include <IRremote.hpp>

#include "../include/config.h"

#if ENABLE_BLE
#include <NimBLEDevice.h>
#endif

// ============================================================
// GLOBAL NESNELER
// ============================================================

WebServer server(WEB_PORT);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============ WiFi (STA) - NVS'de kalici, ESP8266'daki gibi ============
Preferences wifiPrefs;
String savedSSID = "";
String savedPass = "";

void wifiCredYukle() {
  wifiPrefs.begin("wifi", true);
  savedSSID = wifiPrefs.getString("ssid", "");
  savedPass = wifiPrefs.getString("pass", "");
  wifiPrefs.end();
}

void wifiCredKaydet(const String& ssid, const String& pass) {
  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", pass);
  wifiPrefs.end();
  savedSSID = ssid;
  savedPass = pass;
}

// RS485 uzerinden esp8266_slave'e komut gonderir (tanimi asagida) -
// hava durumu fonksiyonlari bunu kullanir, bu yuzden ileri bildirim gerekir.
bool rs485_send_wait_ack(const char* data, String& response, unsigned long timeout_ms, uint8_t max_attempts);

// ============ Hava Durumu / Yagmur Tahmini (sabit konum) ============
// Konum GARDEN_LATITUDE/GARDEN_LONGITUDE (config.h) - secim/geocode yok,
// bahce sabit. Kalburum'un kendi RTC'si olmadigi ve reboot'larda millis()
// sifirlandigi icin "ne zaman cekildi" gercek takvim gunu olarak
// ESP8266'nin RTC'sinden (mevcut GET_ZAMAN RS485 komutu) okunup gun-sayisina
// cevrilir (bkz gunSayisi()) ve SPIFFS'e yazilir - "N gunden eski" kontrolu
// boylece reboot'lara dayanikli olur, NTP/epoch senkronizasyonuna gerek kalmaz.
#define WEATHER_DOSYASI "/hava_tahmini.json"
String weatherForecastDates[WEATHER_FORECAST_DAYS];
float weatherForecastMm[WEATHER_FORECAST_DAYS];
int weatherForecastCount = 0;
long weatherFetchGunSayisi = 0;      // son basarili cekimin gun-sayisi degeri (0 = hic yok)
String weatherFetchTarihStr = "-";   // ayni bilgi, insan-okunur (UI icin, YYYY-MM-DD)
bool weatherSkipOneri = false;       // guncel tahmine gore "bugun sulamayi atla" onerisi
String weatherDurum = "Henuz denenmedi";
unsigned long lastWeatherCheckMs = 0;
bool weatherWifiOncekiDurum = false; // WiFi baglanti gecisini (rising edge) yakalamak icin

// Howard Hinnant'in bilinen "days_from_civil" algoritmasi - takvim tarihini
// (yil/ay/gun) sabit bir baslangica gore tek bir tamsayiya cevirir, boylece
// iki tarih arasindaki gun farki basit bir cikarma ile bulunur (ay/yil
// tasmalarini elle hesaplamaya gerek kalmaz).
long gunSayisi(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

// "DD/MM/YYYY HH:MM:SS" formatindan (ESP8266'nin simdikiZamanStr() ciktisi)
// gun/ay/yil ayiklar.
bool zamanTarihAyristir(const String& zaman, int& gun, int& ay, int& yil) {
  if (zaman.length() < 10) return false;
  gun = zaman.substring(0, 2).toInt();
  ay = zaman.substring(3, 5).toInt();
  yil = zaman.substring(6, 10).toInt();
  return (gun >= 1 && gun <= 31 && ay >= 1 && ay <= 12 && yil > 2000);
}

// ESP8266'nin RTC'sinden guncel gun-sayisini okur (RS485 uzerinden).
// Basarisiz olursa false doner - cagiran taraf temkinli davranmali.
bool simdikiGunSayisi(long& out) {
  String zamanReply;
  if (!rs485_send_wait_ack("MASTER:GET_ZAMAN\n", zamanReply, 1000, 3)) return false;
  int eq = zamanReply.indexOf("GET_ZAMAN=");
  if (eq < 0) return false;
  int g, a, y;
  if (!zamanTarihAyristir(zamanReply.substring(eq + 10), g, a, y)) return false;
  out = gunSayisi(y, a, g);
  return true;
}

void weatherYukle() {
  File f = SPIFFS.open(WEATHER_DOSYASI, "r");
  if (!f) return;
  DynamicJsonDocument doc(1536);
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    weatherFetchGunSayisi = doc["gunSayisi"] | 0L;
    weatherFetchTarihStr = doc["tarih"] | "-";
    weatherForecastCount = doc["sayi"] | 0;
    JsonArray arr = doc["gunler"].as<JsonArray>();
    int i = 0;
    for (JsonObject g : arr) {
      if (i >= WEATHER_FORECAST_DAYS) break;
      weatherForecastDates[i] = g["t"].as<String>();
      weatherForecastMm[i] = g["mm"].as<float>();
      i++;
    }
  }
  f.close();
}

void weatherKaydet() {
  DynamicJsonDocument doc(1536);
  doc["gunSayisi"] = weatherFetchGunSayisi;
  doc["tarih"] = weatherFetchTarihStr;
  doc["sayi"] = weatherForecastCount;
  JsonArray arr = doc.createNestedArray("gunler");
  for (int i = 0; i < weatherForecastCount; i++) {
    JsonObject g = arr.createNestedObject();
    g["t"] = weatherForecastDates[i];
    g["mm"] = weatherForecastMm[i];
  }
  File f = SPIFFS.open(WEATHER_DOSYASI, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

// Open-Meteo'dan 7 gunluk yagmur tahminini ceker (sabit konum). Basarili
// olursa gercek cekim gununu ESP8266'nin RTC'sinden alip SPIFFS'e kaydeder.
bool weatherTahminCek() {
  DEBUG_PRINTLN("[Weather] weatherTahminCek() basladi");
  if (WiFi.status() != WL_CONNECTED) {
    weatherDurum = "WiFi bagli degil";
    DEBUG_PRINTLN("[Weather] " + weatherDurum);
    return false;
  }

  String url = String(WEATHER_FORECAST_API) + "?latitude=" + String(GARDEN_LATITUDE, 6) +
               "&longitude=" + String(GARDEN_LONGITUDE, 6) +
               "&daily=precipitation_sum&forecast_days=" + String(WEATHER_FORECAST_DAYS) + "&timezone=auto";
  DEBUG_PRINTLN("[Weather] GET " + url);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  DEBUG_PRINTLN("[Weather] HTTP kod: " + String(code));
  if (code != HTTP_CODE_OK) {
    // errorToString() sadece HTTPClient'in KENDI (negatif) baglanti hatalari
    // icin anlamli metin dondurur - pozitif HTTP durum kodlarinda (400/403/
    // 500 vb.) genelde bos string donup mesaji "HTTP hata: 500 ()" gibi
    // yaristirir. Sadece negatif kodlarda ekleniyor.
    weatherDurum = "HTTP hata: " + String(code) + (code < 0 ? (" (" + http.errorToString(code) + ")") : "");
    DEBUG_PRINTLN("[Weather] " + weatherDurum);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  DEBUG_PRINTLN("[Weather] Yanit boyutu: " + String(payload.length()) + " byte");

  DynamicJsonDocument doc(3072);
  DeserializationError parseErr = deserializeJson(doc, payload);
  if (parseErr != DeserializationError::Ok) {
    weatherDurum = "JSON parse hatasi: " + String(parseErr.c_str());
    DEBUG_PRINTLN("[Weather] " + weatherDurum);
    return false;
  }
  JsonArray dates = doc["daily"]["time"].as<JsonArray>();
  JsonArray precip = doc["daily"]["precipitation_sum"].as<JsonArray>();
  int n = min((int)precip.size(), WEATHER_FORECAST_DAYS);
  DEBUG_PRINTLN("[Weather] Parse edilen gun sayisi: " + String(n));
  if (n < 2) { weatherDurum = "API yanitinda gun verisi eksik"; DEBUG_PRINTLN("[Weather] " + weatherDurum); return false; }

  for (int i = 0; i < n; i++) {
    weatherForecastDates[i] = dates[i].as<String>();
    weatherForecastMm[i] = precip[i].as<float>();
    DEBUG_PRINTLN("[Weather]   " + weatherForecastDates[i] + ": " + String(weatherForecastMm[i], 1) + "mm");
  }
  weatherForecastCount = n;

  long gs;
  if (simdikiGunSayisi(gs)) {
    weatherFetchGunSayisi = gs;
    weatherFetchTarihStr = weatherForecastDates[0]; // Open-Meteo'nun ilk gunu = bugun, ayni YYYY-MM-DD formati
  }
  // RTC'ye ulasilamadiysa onceki bilinen gun-sayisi/tarih degeri korunur -
  // yeni cekilen tahmin verisi yine de kullanilabilir durumda kalir.
  weatherKaydet();
  weatherDurum = "OK";
  DEBUG_PRINTLN("[Weather] Basarili, " + String(n) + " gunluk tahmin kaydedildi");
  return true;
}

// Tahmin su an gecerli/guncel mi (WEATHER_STALE_DAYS icinde mi)? RTC'ye
// ulasilamazsa temkinli davranip false doner (fail-open: sulama engellenmez).
bool weatherGuncelMi() {
  if (weatherForecastCount == 0 || weatherFetchGunSayisi == 0) return false;
  long gs;
  if (!simdikiGunSayisi(gs)) return false;
  long fark = gs - weatherFetchGunSayisi;
  return (fark >= 0 && fark <= WEATHER_STALE_DAYS);
}

// WiFi yeni baglandiginda veya WEATHER_CHECK_INTERVAL_MS'de bir calisir:
// gerekirse tahmini tazeler, "bugun sulamayi atla" onerisini hesaplayip
// ESP8266'ya RS485 ile bildirir (periyodik yeniden gonderim ayni zamanda
// ESP8266'nin kendi guvenlik-agi bayatlama suresine karsi tazeleme gorevi
// gorur - bkz esp8266_slave rs485KomutDinle() SET_RAIN_SKIP).
void weatherKontrolEt() {
  bool wifiVar = (WiFi.status() == WL_CONNECTED);
  bool yeniBaglandi = wifiVar && !weatherWifiOncekiDurum;
  weatherWifiOncekiDurum = wifiVar;

  unsigned long simdi = millis();
  bool zamanGeldi = (lastWeatherCheckMs == 0) || (simdi - lastWeatherCheckMs >= WEATHER_CHECK_INTERVAL_MS);
  if (!yeniBaglandi && !zamanGeldi) return;
  lastWeatherCheckMs = simdi;

  // TLS (HTTPS) istegi ~35-45KB heap tuketebiliyor; BLE+WebServer+MQTT+SPIFFS
  // ile ayni anda dusuk heap'te tetiklenirse cokme/reset riskini artirir -
  // bu da BLE baglantisini telefon tarafinda "kirli" (temiz kapanmayan) bir
  // sekilde koparir. Heap yetersizse bu turu atla, bir sonraki dongude tekrar
  // dene (guncellik zaten WEATHER_CHECK_INTERVAL_MS ile toleransli).
  if (wifiVar && (yeniBaglandi || weatherForecastCount == 0 || zamanGeldi)) {
    if (ESP.getFreeHeap() < BLE_SAFE_MIN_HEAP) {
      // ONEMLI: bu atlama ONCEDEN sessizdi (sadece Serial log) - UI'daki
      // "Durum:" kutusu guncellenmedigi icin kullanicinin gorebilecegi hicbir
      // iz birakmiyordu. Artik weatherDurum da guncelleniyor ki bu engel
      // gercekten devredeyse "Ayarlar" sekmesinden (reflash/Serial olmadan)
      // gorulebilsin.
      weatherDurum = "Heap dusuk (" + String(ESP.getFreeHeap()) + " byte), sonraki denemede tekrar denenecek";
      DEBUG_PRINTLN("[Weather] " + weatherDurum);
    } else {
      weatherTahminCek();
    }
  }

  bool guncel = weatherGuncelMi();
  weatherSkipOneri = guncel && weatherForecastCount >= 2 && (weatherForecastMm[1] >= WEATHER_RAIN_THRESHOLD_MM);

  String reply;
  rs485_send_wait_ack(weatherSkipOneri ? "MASTER:SET_RAIN_SKIP=1\n" : "MASTER:SET_RAIN_SKIP=0\n", reply, 1000, 3);
}

// ============================================================
// VERI YAPILARI
// ============================================================

struct SensorData {
  float level_cm = 0.0;
  float level_percent = 0.0;
  float level_liters = 0.0;
  float temperature = 0.0;
  bool night_mode = false;
  int moisture_raw = 0;
  float moisture_percent = 0.0;
  bool moisture_output = false;
  bool moisture_auto = false;
  int moisture_low = 0;
  int moisture_high = 0;
  bool rtc_ok = true;      // ESP8266'daki DS1307 RTC gecerli tarih/saat veriyor mu
  bool sensor_err = false; // ESP8266'daki HC-SR04 mesafe sensoru hata veriyor mu
  unsigned long last_update_ms = 0;
};

struct NanoIOStatus {
  bool door1_open = false;
  bool door2_open = false;
  bool relay_active = false;
  bool lamp_on = false;
  String status = "OK";
  unsigned long last_update_ms = 0;
};

struct AlarmStatus {
  bool leak_alarm = false;
  bool low_level_alarm = false;
  bool door_alarm = false;
  bool enabled = true;  // Alarm toggle state
  bool panic_mode = false;  // Panik butonu durumu (ESP8266 ile senkron)
  uint8_t mode = 1;      // 1=Sesli 2=Sessiz 3=Onayli (ESP8266 ile senkron)
  bool muted = false;    // Susturuldu mu (ESP8266 ile senkron)
  bool pending = false;  // Mod 3: onay bekliyor mu (ESP8266 ile senkron)
  uint8_t trigger_mask = 0; // Alarmi tetikleyen sensor(ler) - bitmask, ESP8266 ile ayni kodlama
};

// Global Veri
SensorData sensorData;
NanoIOStatus nanoStatus;
AlarmStatus alarmStatus;
String esp8266_id = "UNKNOWN";
String nano_id = "UNKNOWN";

unsigned long last_rs485_update_ms = 0;
unsigned long last_mqtt_publish_ms = 0;

// ============================================================
// KONTEYNER DONANIMI (IR kumanda, alarm LED, ikinci PIR, kapi reed)
// ============================================================
// PIR2/kapi reed okumasi ESP8266/RS485/BLE'deki ana alarm karar mekanizmasina
// (mod/onay/susturma) BAGLANMADI - bu bilincli bir tercih, oradaki state
// machine'i RS485 gecikmesi/guvenlik riskiyle ugrastirmamak icin. Bunun
// yerine PIR2 kendi basina, tamamen yerel bir debounce ile (Sudepo'daki PIR
// pencere/min-tetiklenme ayarlarinin ayni mantigi) kirmizi LED+buzzer'i
// tetikler - RS485/telefon/Telegram'a hicbir etkisi yok, sadece konteynerda
// sesli/gorsel yerel uyari.
// Ayarlarin (Telegram ac/kapa, Konteyner PIR pencere/min-tetiklenme) NVS'de
// kalici saklanmasi icin - dosyada asagida (Telegram bolumu) de kullanilir.
Preferences ayarPrefs;

bool kapi2Acik = false;       // Konteyner reed switch - true = kapi acik
bool pir2HareketVar = false;  // Konteyner PIR - true = hareket var (ham)
bool konteynerPirAlarmVar = false; // Hareket VAR veya tutma suresi icinde (ham "bolum" durumu)
unsigned long konteynerPirSonHareketMs = 0; // pir2HareketVar'in en son true goruldugu an

// PIR HASSASIYET (2 kademeli) - HC-SR505'in potansiyometresi olmadigindan
// (sabit ~8sn HIGH, hareketin buyuklugune bakmaksizin) "ufak kipirdama"yi
// donanimsal degil, sureye dayali yazilim mantigiyla eliyoruz:
//  1) ON UYARI: her yeni hareket "bolumu" basladiginda 1sn'lik kisa bip/LED
//     darbesi - alarm DEGIL, sadece yerel/duyulur "sensor gordu" isareti.
//  2) ESKALASYON: hareket (tutma suresiyle kopruleniyor) kesintisiz Onay
//     Suresi'ni (varsayilan 10sn) gecerse GERCEK alarm sayilir - bundan sonrasi
//     genel Alarm Modu'na (Sesli/Sessiz/Onayli) uyar, bkz alarmLedGuncelle().
bool konteynerPirEskalasyonOldu = false;   // bu "bolum"de Onay Suresi asildi mi
unsigned long konteynerPirBolumBaslangicMs = 0; // 0 = aktif bolum yok
bool konteynerOnBipCiksin = false;         // loop'a "yeni bolum basladi, on bip ver" sinyali
bool konteynerOnayBekleniyor = false;      // mode==3 + eskalasyon oldu + henuz onaylanmadi
bool konteynerOnayVerildi = false;         // mode==3 + bu bolum icin onaylandi (Sesli onay ile)
bool konteynerLambaOnayVerildi = false;    // mode==3 + bu bolum icin "Sessiz (Lamba)" onayi verildi
bool konteynerSirenAktif = false;          // KONTEYNER_SIREN_PIN'in guncel durumu (durumJson icin)
bool konteynerLambaAktif = false;          // KONTEYNER_LAMBA_PIN'in guncel durumu (durumJson icin)

// Konteyner PIR ayari (NVS'de kalici).
// ONEMLI (HC-SR505 datasheet farki, HC-SR501'den FARKLI calisiyor - bkz
// kullanici uyarisi): HC-SR505'te potansiyometre/jumper YOK, cikis suresi
// SABIT ~6-12sn (datasheet: 8sn +-%30), tetiklenebilir modda hareket
// surdukce HIGH'ta kalir - yani sinyal SEVIYE tabanlidir, kesikli/darbeli
// degildir (HC-SR501'deki gibi ust uste ayrik kenarlar vermez).
// ONCEKI TASARIM HATASI: "pencere icinde en az N kez tetiklenme" (Sudepo'daki
// PIR ayarlarinin ayni mantigi kopyalanmisti) bu sinyal tipine uymuyordu -
// sadece YUKSELEN KENARDA kayit tutuyordu, surekli 20sn'lik kesintisiz bir
// harekette TEK kayit olusuyordu; pencere suresi dolunca (hareket hala
// suruyorken) alarm yanlislikla FALSE'a donuyordu (kullanicinin bulgusu).
// DUZELTME: artik kenar saymiyoruz, en son hareket gorulen ani takip
// ediyoruz - alarm, hareket VAR oldugu surece VE bittikten sonra "Tutma
// Suresi" kadar daha aktif kalir (basit, dogru, seviye-tabanli mantik).
uint16_t konteynerPirTutmaSaniye = 5;
uint16_t konteynerPirOnaySaniye = 10; // Eskalasyon esigi (bkz yukaridaki aciklama)

void konteynerPirAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerPirTutmaSaniye = ayarPrefs.getUShort("k_pir_tut", 5);
  konteynerPirOnaySaniye = ayarPrefs.getUShort("k_pir_onay", 10);
  ayarPrefs.end();
}
void konteynerPirAyarKaydet(uint16_t tutmaSaniye, uint16_t onaySaniye) {
  konteynerPirTutmaSaniye = tutmaSaniye;
  konteynerPirOnaySaniye = onaySaniye;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putUShort("k_pir_tut", tutmaSaniye);
  ayarPrefs.putUShort("k_pir_onay", onaySaniye);
  ayarPrefs.end();
}

// Konteyner'in KENDI bagimsiz alarm ac/kapa anahtari - Sudepo Zonu'nun
// alarmStatus.enabled'i ESP8266'dan RS485 ile aynalanan (o zona ait) bir
// deger, Konteyner tamamen ESP32-yerel donanim oldugundan kendi ayri, NVS'de
// kalici bir bayragi var. Boylece iki zon birbirinden BAGIMSIZ acilip
// kapatilabilir (bkz alarmLedGuncelle/telegramAlarmKontrolEt).
bool konteynerAlarmEtkin = true;

void konteynerAlarmAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerAlarmEtkin = ayarPrefs.getBool("k_alarm_en", true);
  ayarPrefs.end();
}
void konteynerAlarmAyarKaydet(bool etkin) {
  konteynerAlarmEtkin = etkin;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putBool("k_alarm_en", etkin);
  ayarPrefs.end();
}

void konteynerDonanimiInit() {
  pinMode(ALARM_LED_PIN, OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);
  pinMode(KONTEYNER_SIREN_PIN, OUTPUT);
  digitalWrite(KONTEYNER_SIREN_PIN, LOW);
  pinMode(KONTEYNER_LAMBA_PIN, OUTPUT);
  digitalWrite(KONTEYNER_LAMBA_PIN, LOW);
  pinMode(PIR2_PIN, INPUT);
  // Reed switch: kablolamaya gore kapali/acik seviyesi degisebilir -
  // ilk kurulumda gercek davranisi /api/durum -> konteyner.kapi_acik'tan
  // gozlemleyip gerekirse asagidaki karsilastirmayi (==HIGH) ters cevir.
  pinMode(KAPI_REED_PIN, INPUT_PULLUP);
  // ENABLE_LED_FEEDBACK KULLANMA: bu kartta gecerli bir varsayilan LED_BUILTIN
  // tanimli degil, kutuphane bu durumda USE_DEFAULT_FEEDBACK_LED_PIN=0xFF
  // (GECERSIZ bir GPIO numarasi) kullanmaya calisiyor - bu, cihazda tekrarlayan
  // Brownout resetlerine (kararsizliga) yol acti, DISABLE ile duzeldi/dogrulandi.
  IrReceiver.begin(IR_RECV_PIN, DISABLE_LED_FEEDBACK);
  DEBUG_PRINTLN("[KONTEYNER] IR/LED/PIR2/Reed hazir");
}

// Kirmizi alarm LED'i + buzzer (ikisi ayni pine paralel bagli, bkz config.h) -
// mevcut alarm durumunu okur (banner'in gorunurlugüyle ayni mantik) VE yerel
// Konteyner sensorlerini (kapi reed + ESKALE OLMUS PIR) de hesaba katar -
// genel alarm sistemine adapte edildi (bkz telegramAlarmKontrolEt, ayni mantik
// orada da tekrarlanir). ESP8266'daki Sesli/Sessiz/Onayli mod ayrimiyla ARTIK
// AYNI: Sesli'de hemen calar, Sessiz'de hic calmaz (Telegram/banner yine de
// bildirir), Onayli'da banner'dan "Sesli" ile onaylanana kadar sessiz kalir.
// Ayrica her yeni PIR hareket "bolumu" basladiginda (konteynerOnBipCiksin),
// gercek bir alarm calmiyorsa 1sn'lik kisa bir "on uyari" darbesi verir.
void alarmLedGuncelle() {
  static bool ledDurum = false;
  static unsigned long sonDegisimMs = 0;
  static bool onBipAktif = false;
  static unsigned long onBipBaslangicMs = 0;

  // Panik (elle acilan), sistemdeki DIGER tum panik kontrolleriyle (yerel
  // LED+buzzer, Telegram, banner) TUTARLI olarak enabled/mod'dan BAGIMSIZ -
  // "her seyin onunde calisir, elle ac/kapat anahtari gibi" davranisiyla
  // Konteyner siren+lambasini da dogrudan (Sesli gibi) tetikler.
  bool konteynerEskaleVar = alarmStatus.panic_mode || (konteynerAlarmEtkin && (konteynerPirEskalasyonOldu || kapi2Acik));
  bool konteynerBuzzerVar = false;
  if (konteynerEskaleVar) {
    if (alarmStatus.panic_mode || alarmStatus.mode == 1) konteynerBuzzerVar = true; // Panik veya Sesli - hemen cal
    else if (alarmStatus.mode == 3) konteynerBuzzerVar = konteynerOnayVerildi; // Onayli - onaydan sonra cal
    // mode==2 (Sessiz) ve panik degilse: konteynerBuzzerVar false kalir, Telegram/banner yine de calisir
  }

  // Siren + Lamba (ALARM_LED_PIN'deki kucuk LED+buzzer'dan AYRI, gercek role
  // uzerinden calisan donanim - bkz config.h). Siren, kucuk buzzer ile AYNI
  // kosulda aktif ama SABIT/surekli (role oldugu icin 400ms yanip-sonme YOK,
  // sik ac/kapa roleye zarar verir). Lamba, siren ile birlikte VEYA Onayli
  // modda "Sessiz (Lamba)" onayi verildiginde (konteynerLambaOnayVerildi) TEK
  // BASINA aktif - Sudepo Zonu'ndaki "Onayli - sadece lamba flasoru" secenegiyle
  // ayni mantik, artik Konteyner'de de gercek bir fiziksel karsiligi var.
  konteynerSirenAktif = konteynerBuzzerVar;
  konteynerLambaAktif = konteynerBuzzerVar || konteynerLambaOnayVerildi;
  digitalWrite(KONTEYNER_SIREN_PIN, konteynerSirenAktif ? HIGH : LOW);
  digitalWrite(KONTEYNER_LAMBA_PIN, konteynerLambaAktif ? HIGH : LOW);

  bool alarmVar = (alarmStatus.enabled && alarmStatus.trigger_mask != 0) || alarmStatus.panic_mode || alarmStatus.pending || konteynerBuzzerVar;

  // On uyari darbesi - sadece gercek bir alarm CALMIYORSA baslat (cakismasin diye)
  if (konteynerOnBipCiksin) {
    konteynerOnBipCiksin = false;
    if (!alarmVar && !onBipAktif) {
      onBipAktif = true;
      onBipBaslangicMs = millis();
      digitalWrite(ALARM_LED_PIN, HIGH);
    }
  }
  if (onBipAktif) {
    if (millis() - onBipBaslangicMs >= 1000) {
      onBipAktif = false;
      if (!alarmVar) digitalWrite(ALARM_LED_PIN, LOW);
    } else if (!alarmVar) {
      return; // bip suruyor, asagidaki surekli-alarm darbe mantigina girme
    }
  }

  if (!alarmVar) {
    if (ledDurum) { ledDurum = false; digitalWrite(ALARM_LED_PIN, LOW); }
    return;
  }
  unsigned long simdi = millis();
  if (simdi - sonDegisimMs >= 400) {
    sonDegisimMs = simdi;
    ledDurum = !ledDurum;
    digitalWrite(ALARM_LED_PIN, ledDurum ? HIGH : LOW);
  }
}

void konteynerSensorleriOku() {
  kapi2Acik = (digitalRead(KAPI_REED_PIN) == HIGH);
  pir2HareketVar = (digitalRead(PIR2_PIN) == HIGH);

  // Yeni hareket "bolumu" mu basliyor? (bolum = kesintisiz/tutma-suresiyle-
  // kopruli hareket suresi). Basliyorsa on uyari bipini tetikle ve eskalasyon
  // sayacini sifirla.
  if (pir2HareketVar && konteynerPirBolumBaslangicMs == 0) {
    konteynerPirBolumBaslangicMs = millis();
    konteynerPirEskalasyonOldu = false;
    konteynerOnBipCiksin = true;
  }

  // Seviye tabanli: hareket VAR oldugu her anda "son hareket" zamani
  // guncellenir (sadece yukselen kenarda degil) - boylece surekli/uzun
  // sureli hareket boyunca alarm hic dusmez. Hareket bittikten sonra da
  // Tutma Suresi kadar aktif kalmaya devam eder (kisa kesintileri/aninda
  // sonlanmayi tolere eder).
  if (pir2HareketVar) konteynerPirSonHareketMs = millis();
  unsigned long tutmaMs = (unsigned long)konteynerPirTutmaSaniye * 1000UL;
  konteynerPirAlarmVar = pir2HareketVar || (millis() - konteynerPirSonHareketMs <= tutmaMs);

  // ESKALASYON: bolum, Onay Suresi'ni kesintisiz astiysa artik GERCEK alarm.
  if (konteynerPirAlarmVar && konteynerPirBolumBaslangicMs != 0 && !konteynerPirEskalasyonOldu) {
    unsigned long onaySuresiMs = (unsigned long)konteynerPirOnaySaniye * 1000UL;
    if (millis() - konteynerPirBolumBaslangicMs > onaySuresiMs) {
      konteynerPirEskalasyonOldu = true;
      if (alarmStatus.mode == 3) konteynerOnayBekleniyor = true;
    }
  }

  // Bolum tamamen bitti (tutma suresi de doldu) - her seyi sifirla.
  if (!konteynerPirAlarmVar && konteynerPirBolumBaslangicMs != 0) {
    konteynerPirBolumBaslangicMs = 0;
    konteynerPirEskalasyonOldu = false;
    konteynerOnayBekleniyor = false;
    konteynerOnayVerildi = false;
    konteynerLambaOnayVerildi = false;
  }
}

// IR kumanda - HAM YAKALAMA. Gercek eslesme/ogrenme-modu isleme (asagidaki
// irEslesmeler tablosu ve komutCalistir()) dosyada daha ileride tanimli
// oldugundan (komutCalistir *Ayarla fonksiyonlarindan sonra gelir), burada
// sadece kodu paylasilan degiskenlere yazip birakiyor - loop() hemen
// ardindan irKomutIsleVeCalistir()'i cagirir.
volatile bool irYeniKodVar = false;
uint32_t irSonKod = 0;
void irKumandaIsle() {
  if (!IrReceiver.decode()) return;
  irSonKod = IrReceiver.decodedIRData.decodedRawData;
  irYeniKodVar = true;
  DEBUG_PRINT("[IR] Kod alindi: 0x");
  DEBUG_PRINTLN(String(irSonKod, HEX));
  IrReceiver.resume();
}

// ============ TELEGRAM ALARM BILDIRIMLERI ============
// Alarm YENI basladiginda (surekli degil, sadece "yok -> var" gecisinde)
// kullanicinin zaten sahip oldugu Telegram bota (ev kapisi/ruzgar alarmlariyla
// ayni bot - onek ile ayirt edilir) HTTPS ile mesaj gonderir. Bahcede kalici
// internet olmadigindan bu sadece kullanici hotspot'uyla bagliyken calisir -
// bilerek kabul edilen bir kisitlama (bkz kullanici talebi: "zaten ben
// oradayken almak istiyorum").
bool telegramOncekiAlarmVar = false;
bool telegramBekleyenVar = false;
String telegramBekleyenMetin = "";
unsigned long telegramIlkDenemeMs = 0;
#define TELEGRAM_RETRY_SURESI_MS (2UL * 60UL * 1000UL) // basarisizsa bu kadar sure tekrar denenir, sonra vazgecilir

// Kullanici talebiyle: Telegram alarm bildirimi ac/kapa ayari (Ayarlar
// sekmesi) - NVS'de kalici, varsayilan acik (eski davranisla ayni).
// (ayarPrefs nesnesi Konteyner Donanimi bolumunde, dosyanin daha yukarisinda tanimli)
bool telegramBildirimAktif = true;
void telegramAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  telegramBildirimAktif = ayarPrefs.getBool("tg_aktif", true);
  ayarPrefs.end();
}
void telegramAyarKaydet(bool aktif) {
  telegramBildirimAktif = aktif;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putBool("tg_aktif", aktif);
  ayarPrefs.end();
}

const char* alarmTetikleyiciAdlari[6] = {"Sol Kapi", "Sag Kapi", "PIR (Hareket)", "Su Seviyesi", "Kacak", "Sensor Hatasi"};

// konteynerPir/konteynerKapi: ESP8266'nin trigger_mask'inden BAGIMSIZ,
// ESP32'nin yerel Konteyner sensorleri (PIR2 + kapi reed) - ESP8266
// bitmask'ina karistirilmiyor (o mask ESP8266'nin kendi kodlamasi, ileride
// cakisma riski olmasin diye ayri tutuluyor).
String alarmTetikleyenMetni(uint8_t mask, bool panik, bool konteynerPir = false, bool konteynerKapi = false) {
  if (panik) return "Panik (elle acildi)";
  String s = "";
  for (int i = 0; i < 6; i++) {
    if (mask & (1 << i)) {
      if (s.length() > 0) s += ", ";
      s += alarmTetikleyiciAdlari[i];
    }
  }
  if (konteynerPir) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner PIR (Hareket)";
  }
  if (konteynerKapi) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner Kapı";
  }
  return s.length() > 0 ? s : "Bilinmiyor";
}

// RFC3986 percent-encode - Telegram mesaj metni (Turkce karakter, bosluk,
// emoji) icin gerekli.
String telegramUrlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// Telegram Bot API'ye mesaj gonderir. Basarisiz olursa false doner (WiFi
// yok, token/chat ID bos, HTTP hatasi vb.) - cagiran taraf yeniden deneme
// mantigina sahip.
bool telegramMesajGonder(const String& metin) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (String(TELEGRAM_BOT_TOKEN).length() == 0) return false;
  // bkz weatherKontrolEt yanindaki heap notu - ayni sebep.
  if (ESP.getFreeHeap() < BLE_SAFE_MIN_HEAP) {
    DEBUG_PRINTLN("[Telegram] Heap dusuk, gonderim atlandi (tekrar denenecek)");
    return false;
  }

  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  String body = "chat_id=" + String(TELEGRAM_CHAT_ID) + "&text=" + telegramUrlEncode(metin);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST(body);
  http.end();
  return (code == HTTP_CODE_OK);
}

// Alarm durumunu (ESP8266'dan RS485 ile zaten cekilen alarmStatus) izler;
// "yok -> var" gecisinde bir kez mesaj gonderir. Gonderim o an basarisiz
// olursa (internet henuz yoksa) TELEGRAM_RETRY_SURESI_MS boyunca her loop'ta
// tekrar denenir, sonra vazgecilir.
void telegramAlarmKontrolEt() {
  uint8_t mask = alarmStatus.trigger_mask;
  // Konteyner sensorleri (PIR2 + kapi reed), ESP8266'nin trigger_mask'inden
  // bagimsiz yerel kaynaklar - KENDI bagimsiz ac/kapa anahtarina
  // (konteynerAlarmEtkin, Sudepo'nun alarmStatus.enabled'inden AYRI) uyarlar,
  // panik/onay bekleme ESP8266'ya ozgu oldugundan onlara karismazlar.
  bool konteynerPirVar = konteynerAlarmEtkin && konteynerPirEskalasyonOldu;
  bool konteynerKapiVar = konteynerAlarmEtkin && kapi2Acik;
  bool anyAlarm = (alarmStatus.enabled && mask != 0) || alarmStatus.panic_mode || konteynerPirVar || konteynerKapiVar;
  bool alarmVar = anyAlarm || alarmStatus.pending;

  if (!telegramBildirimAktif) {
    telegramOncekiAlarmVar = alarmVar; // kapaliyken de takip et ki acilinca eski alarm icin mesaj atmasin
    telegramBekleyenVar = false;
    return;
  }

  if (alarmVar && !telegramOncekiAlarmVar) {
    // Yeni alarm basladi - mesaj hazirla, ilk denemeyi hemen yap.
    String baslik = alarmStatus.panic_mode ? "PANIK AKTIF" :
                     (alarmStatus.pending ? "ALARM - Onay Bekliyor" : "ALARM TETIKLENDI");
    telegramBekleyenMetin = "🌱 SuDepo: " + baslik + " | Tetikleyen: " + alarmTetikleyenMetni(mask, alarmStatus.panic_mode, konteynerPirVar, konteynerKapiVar);
    telegramBekleyenVar = true;
    telegramIlkDenemeMs = millis();
  }
  telegramOncekiAlarmVar = alarmVar;

  if (telegramBekleyenVar) {
    if (telegramMesajGonder(telegramBekleyenMetin)) {
      telegramBekleyenVar = false;
    } else if (millis() - telegramIlkDenemeMs > TELEGRAM_RETRY_SURESI_MS) {
      telegramBekleyenVar = false; // vazgecildi - internet gelmedi
    }
  }
}

// ============================================================
// RS485 UART1 AYARLARI
// ============================================================

// BLE (NimBLE, "nimble_host" adinda AYRI bir FreeRTOS gorevinde calisir) ve
// Arduino loop() gorevi (rs485_poll) ayni RS485 hattina (Serial1 + DE pini)
// ES ZAMANLI erisebiliyordu. rs485_api_busy sadece kooperatif bir bayrakti -
// loop() zaten rs485_read_line() icinde (~400ms'e kadar) bloke olmus haldeyken
// BLE tarafi bu bayragi henuz gormeden kendi gonderimini baslatabiliyordu.
// Sonuc: iki gorev ayni anda Serial1'e yazip/okuyordu - bu oturum boyunca
// gorulen "ESP8266 no response"/"Partial message"/bozuk "NACK" satirlarinin
// VE BLE komutlarinin (LAMBA_AC vb.) sik sik 3-4 saniyeye uzayan gecikmesinin
// (3 deneme x 1sn timeout'un neredeyse tamamina carpmasi) kok nedeni buydu.
// Gercek bir mutex ile iki gorevin RS485 hattina asla ayni anda dokunmamasi
// garanti ediliyor.
SemaphoreHandle_t rs485Mutex = nullptr;
struct RS485Kilit {
  RS485Kilit() { xSemaphoreTake(rs485Mutex, portMAX_DELAY); }
  ~RS485Kilit() { xSemaphoreGive(rs485Mutex); }
};

void rs485_init() {
  DEBUG_PRINTLN("[RS485] Initializing UART1...");

  rs485Mutex = xSemaphoreCreateMutex();

  // Varsayilan RX tamponu (256 byte) ~270 byte'lik status mesaji icin
  // sinirdaydi; begin()'den once buyutulmesi gerekiyor.
  Serial1.setRxBufferSize(1024);
  Serial1.begin(RS485_BAUDRATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);  // Başta Receiver mode

  DEBUG_PRINT("[RS485] UART1 ready at ");
  DEBUG_PRINTLN(RS485_BAUDRATE);
}

void rs485_send(const char* data) {
  digitalWrite(RS485_DE_PIN, HIGH);  // Transmitter mode
  delayMicroseconds(100);
  
  Serial1.print(data);
  Serial1.flush();
  
  delayMicroseconds(100);
  digitalWrite(RS485_DE_PIN, LOW);   // Receiver mode
}

bool rs485_send_wait_ack(const char* data, String& response, unsigned long timeout_ms = 1000, uint8_t max_attempts = 2) {
  RS485Kilit kilit; // bkz RS485Kilit tanimi - loop() ile BLE gorevinin hatta ayni anda dokunmasini engeller
  // GECICI TANI LOGU: genel (BLE'ye ozel olmayan - web dahil) komut gecikmesi
  // sikayeti icin - her komutun kacinci denemede/ne kadar surede basardigini
  // gorup gercek darbogazi (retry mi, tek seferlik yavaslik mi) bulmak icin.
  unsigned long fonksiyon_baslangic_ms = millis();
  for (uint8_t attempt = 0; attempt < max_attempts; attempt++) {
    // Drop any stale data before sending, to avoid reading older lines as the ACK.
    while (Serial1.available()) {
      Serial1.read();
    }

    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(200);
    Serial1.print(data);
    Serial1.flush();
    delayMicroseconds(200);
    digitalWrite(RS485_DE_PIN, LOW);

    unsigned long start_ms = millis();
    response = "";
    while (millis() - start_ms < timeout_ms) {
      if (Serial1.available()) {
        char c = Serial1.read();
        if (c == '\n') {
          if (response.length() > 0) {
            DEBUG_PRINT("[RS485] ACK reply: ");
            DEBUG_PRINTLN(response);
            if (response.startsWith("ACK:")) {
              // Sadece yeniden deneme gerektiyse logla - normal (ilk denemede
              // basarili) durumda gereksiz log gurultusu yaratmasin. ESP8266'nin
              // istem disi otomatik gonderiminin kaldirilmasindan sonra bu artik
              // neredeyse hic tetiklenmiyor - tekrar sik gorulmeye baslarsa
              // RS485 hattinda yeni bir catisma kaynagi olustugunun isaretidir.
              if (attempt > 0) {
                DEBUG_PRINT("[RS485_TIMING] yeniden denemeyle basarili, deneme=");
                DEBUG_PRINT(String(attempt + 1));
                DEBUG_PRINT("/");
                DEBUG_PRINT(String(max_attempts));
                DEBUG_PRINT(", sure_ms=");
                DEBUG_PRINTLN(String(millis() - fonksiyon_baslangic_ms));
              }
              return true;
            }
            response = "";
          }
        } else if (c != '\r' && c >= 32) {
          response += c;
        }
      }
      yield();
    }

    if (response.length() > 0) {
      DEBUG_PRINT("[RS485] Last partial reply: ");
      DEBUG_PRINTLN(response);
    }

    DEBUG_PRINTLN("[RS485] ACK timeout, retrying...");
    delay(25);
  }

  DEBUG_PRINTLN("[RS485] ACK failed after retries");
  return false;
}

String rs485_read_line() {
  String buffer = "";
  unsigned long start_ms = millis();

  // Buffer size limit - ESP8266'nin status satiri (alarm mod/mute/pending
  // alanlari eklendikten sonra) ~270 karaktere ulasiyor; 256 sinir cok
  // dardi ve mesaji ortadan kesip komple dusuruyordu (anlik degerlerin
  // "sifirlanip duzelmesi" buradan kaynaklaniyordu). ESP8266 tarafi 320
  // byte'lik buffer kullaniyor, burada da ayni paye ile 400'e cikarildi.
  while (millis() - start_ms < RS485_TIMEOUT_MS && buffer.length() < 400) {
    if (Serial1.available()) {
      char c = Serial1.read();
      
      if (c == '\n') {
        return buffer;
      } else if (c != '\r' && c >= 32) {  // Printable chars only
        buffer += c;
      }
    }
    yield();  // ESP32 other tasks
  }
  
  // Timeout - return empty or partial
  if (buffer.length() > 0) {
    DEBUG_PRINT("[RS485] Partial message: ");
    DEBUG_PRINTLN(buffer);
  }
  
  return "";
}

// ============================================================
// RS485 PROTOKOL - Veri Parse
// ============================================================

void parse_esp8266_data(String payload);
void ssePush();

void parse_rs485_message(String msg) {
  if (msg.length() == 0) return;

  int colon_idx = msg.indexOf(':');

  if (colon_idx >= 0) {
    // Format: "DEVICE_ID:payload"
    String device_id = msg.substring(0, colon_idx);
    String payload = msg.substring(colon_idx + 1);

    DEBUG_PRINT("[RS485] Received from ");
    DEBUG_PRINT(device_id);
    DEBUG_PRINT(": ");
    DEBUG_PRINTLN(payload);

    // ESP8266 ID varyasyonları: ESP8266, EIP8266, EKP8266 vs.
    // NOT: Nano bilgileri (kapi/role/lamba) her zaman ESP8266'nin tek
    // birlesik mesaji icinde (K1/K2/R/LAMBA alanlari) gelir - ESP32 Nano ile
    // RS485 uzerinden dogrudan konusmaz. Eskiden "NANO:" prefix'li veya
    // prefix'siz mesajlari ayri isleyen bir parse_nano_data() yolu vardi;
    // bu yol hicbir zaman tetiklenmiyordu (ESP8266 her zaman "ESP8266:" ile
    // yanit verir) ve kaldirildi.
    if (device_id.indexOf("ESP8266") >= 0 || device_id.indexOf("EIP8266") >= 0 || device_id.indexOf("PKP8266") >= 0) {
      parse_esp8266_data(payload);
    }
  }
}

void parse_esp8266_data(String payload) {
  // Format: "LEVEL=49.2,PCT=60.0,LITRE=1801,MODE=day,K1=0,K2=0,R=0"
  
  int pos = 0;
  while (pos < payload.length()) {
    int eq_idx = payload.indexOf('=', pos);
    if (eq_idx < 0) break;
    
    int next_comma = payload.indexOf(',', eq_idx);
    if (next_comma < 0) next_comma = payload.length();
    
    String key = payload.substring(pos, eq_idx);
    String value = payload.substring(eq_idx + 1, next_comma);
    
    if (key == "LEVEL") {
      sensorData.level_cm = value.toFloat();
    } else if (key == "PCT") {
      sensorData.level_percent = value.toFloat();
    } else if (key == "LITRE") {
      sensorData.level_liters = value.toFloat();
    } else if (key == "TEMP") {
      sensorData.temperature = value.toFloat();
    } else if (key == "MODE") {
      sensorData.night_mode = (value == "night");
    } else if (key == "K1") {
      nanoStatus.door1_open = (value == "1");
    } else if (key == "K2") {
      nanoStatus.door2_open = (value == "1");
    } else if (key == "R") {
      nanoStatus.relay_active = (value == "1");
    } else if (key == "LAMBA") {
      nanoStatus.lamp_on = (value == "1");
    } else if (key == "MOISTURE_RAW") {
      sensorData.moisture_raw = value.toInt();
    } else if (key == "MOISTURE_PCT") {
      sensorData.moisture_percent = value.toFloat();
    } else if (key == "MOISTURE_OUTPUT") {
      sensorData.moisture_output = (value == "1");
    } else if (key == "MOISTURE_AUTO") {
      sensorData.moisture_auto = (value == "1");
    } else if (key == "MOISTURE_LOW") {
      sensorData.moisture_low = value.toInt();
    } else if (key == "MOISTURE_HIGH") {
      sensorData.moisture_high = value.toInt();
    } else if (key == "ALARM") {
      // ESP8266'da bu alan "alarm sistemi etkin mi" anlamina gelir (kapi
      // durumuyla ilgisi yok). Eskiden yanlislikla door_alarm'a yaziliyordu;
      // kapi alarmi asagida K1/K2'den ayrica dogru hesaplaniyor zaten - o
      // deger burada ezildigi icin ssePush() aninda yanlis "kapi acik"
      // gonderiyordu, sonraki cevrimde duzeliyordu. Bu "yanip-sonme" hatasiydi.
      alarmStatus.enabled = (value == "1");
    } else if (key == "ERR") {
      sensorData.sensor_err = (value == "1");
    } else if (key == "RTC") {
      sensorData.rtc_ok = (value == "1");
    } else if (key == "LEAK") {
      alarmStatus.leak_alarm = (value == "1");
    } else if (key == "FILL") {
      // dolum durumu - log
    } else if (key == "PANIC") {
      // ESP8266'dan panik durumu - senkronize et
      alarmStatus.panic_mode = (value == "1");
    } else if (key == "ALARM_MOD") {
      alarmStatus.mode = (uint8_t)value.toInt();
    } else if (key == "ALARM_MUTE") {
      alarmStatus.muted = (value == "1");
    } else if (key == "ALARM_PENDING") {
      alarmStatus.pending = (value == "1");
    } else if (key == "TRIG_MASK") {
      alarmStatus.trigger_mask = (uint8_t)value.toInt();
    }
    
    pos = next_comma + 1;
  }
  
  sensorData.last_update_ms = millis();
  // Nano bilgileri (K1/K2/R/LAMBA) bu mesajin icinde geldigi icin Nano'nun
  // "online" damgasi da burada guncellenir (ayri bir Nano mesaji hic gelmez).
  nanoStatus.last_update_ms = millis();

  // Kapı alarmı - ssePush()'tan ONCE hesaplanmali ki push her zaman tutarli
  // (dogru) durumu gondersin.
  if (nanoStatus.door1_open || nanoStatus.door2_open) {
    alarmStatus.door_alarm = true;
  } else {
    alarmStatus.door_alarm = false;
  }

  ssePush(); // ESP8266'dan taze veri geldi - baglı istemcilere aninda pushla
}

// ============================================================
// RS485 POLLING (Ana Döngü) - Non-blocking state machine
// ============================================================
// FIX: ESP8266'ya GET_STATUS komutu gönder, MASTER:REQUEST_ESP8266 değil.
// ESP8266 yanıtı: "D0=1,D1=1,RELE=1,LAMBA=1" (Nano format, prefix'siz)

enum Rs485State { RS485_IDLE, RS485_WAIT_ESP };
Rs485State rs485_state = RS485_IDLE;
unsigned long rs485_state_start_ms = 0;
String rs485_pending_msg = "";

// FIX: API komutları (handleAPI_Lamba vb.) bu flag'i true yapar,
// böylece rs485_poll periyodik GET_STATUS'u o döngüde atlar.
// Race condition: poll + API komutu aynı anda RS485'e yazmasın.
bool rs485_api_busy = false;

void rs485_poll() {
  // API komutu işleniyorsa periyodik poll'u atla
  if (rs485_api_busy) return;

  unsigned long now = millis();
  
  switch (rs485_state) {
    case RS485_IDLE:
      if (now - last_rs485_update_ms >= RS485_UPDATE_INTERVAL) {
        last_rs485_update_ms = now;
        {
          RS485Kilit kilit;
          // KRITIK: ESP8266 kendi periyodik durumunu da bagimsiz bir zamanlayicida
          // (RS485_SEND_INTERVAL) istem disi gonderiyor. Bu eski/bekleyen mesaj
          // burada temizlenmezse, az sonra gonderecegimiz GET_STATUS'un YANITI
          // yerine bu BAYAT veriyi okuyabiliyorduk - ekranin "gec guncellenmesi"
          // hissinin buyuk kismi buradan geliyordu.
          while (Serial1.available()) Serial1.read();
          rs485_send("GET_STATUS\n");
        }
        rs485_state = RS485_WAIT_ESP;
        rs485_state_start_ms = now;
      }
      break;

    case RS485_WAIT_ESP:
      // Onceden sabit 200ms "kor bekleme" sonra okumaya baslardi - bu, buyuk
      // mesajlarda (~270 byte, 9600 baud'da ~280ms) okuma penceresini
      // gereksiz yere kisaltiyordu. Simdi hemen okumaya baslaniyor;
      // rs485_read_line zaten kendi ici RS485_TIMEOUT_MS kadar bekliyor.
      if (now - rs485_state_start_ms >= 10) {
        String msg;
        {
          RS485Kilit kilit;
          msg = rs485_read_line();
        }
        if (msg.length() > 0) {
          parse_rs485_message(msg);
        } else {
          DEBUG_PRINTLN("[RS485] ESP8266 no response");
        }
        rs485_state = RS485_IDLE;
      }
      break;
  }
}

// ============================================================
// MQTT YAYIMLAMA
// ============================================================

void mqtt_connect() {
  if (!ENABLE_MQTT) return;
  
  static unsigned long last_mqtt_attempt_ms = 0;
  
  // Reconnect attempt interval: 10 saniye (spam engellemek için)
  if (mqttClient.connected()) {
    last_mqtt_attempt_ms = millis();
    return;
  }
  
  if (millis() - last_mqtt_attempt_ms < 10000) {
    return;  // Retry'ı delay et
  }
  
  last_mqtt_attempt_ms = millis();
  
  DEBUG_PRINTLN("[MQTT] Connecting...");
  
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    DEBUG_PRINTLN("[MQTT] Connected!");
    
    // Subscribe
    mqttClient.subscribe(MQTT_TOPIC_COMMAND);
    
  } else {
    DEBUG_PRINT("[MQTT] Connection failed, rc=");
    DEBUG_PRINTLN(mqttClient.state());
  }
}

void mqtt_publish() {
  if (!ENABLE_MQTT || !mqttClient.connected()) return;
  
  if (millis() - last_mqtt_publish_ms < 5000) {  // 5 saniyede bir
    return;
  }
  
  last_mqtt_publish_ms = millis();
  
  // Stack buffer instead of String (avoid heap fragmentation)
  char jsonBuffer[512];
  
  // Manual JSON (DynamicJsonDocument heap kullanım azalt)
  snprintf(jsonBuffer, sizeof(jsonBuffer),
    "{"
    "\"level_cm\":%.1f,"
    "\"level_percent\":%.1f,"
    "\"level_liters\":%.0f,"
    "\"temperature\":%.1f,"
    "\"night_mode\":%s,"
    "\"nano\":{\"door1\":%s,\"door2\":%s,\"relay\":%s},"
    "\"alarm\":{\"leak\":%s,\"low_level\":%s,\"door\":%s,\"panic\":%s}"
    "}",
    sensorData.level_cm,
    sensorData.level_percent,
    sensorData.level_liters,
    sensorData.temperature,
    sensorData.night_mode ? "true" : "false",
    nanoStatus.door1_open ? "true" : "false",
    nanoStatus.door2_open ? "true" : "false",
    nanoStatus.relay_active ? "true" : "false",
    alarmStatus.leak_alarm ? "true" : "false",
    alarmStatus.low_level_alarm ? "true" : "false",
    alarmStatus.door_alarm ? "true" : "false",
    alarmStatus.panic_mode ? "true" : "false"
  );
  
  mqttClient.publish(MQTT_TOPIC_LEVEL, jsonBuffer);
}

// ============================================================
// WEB SUNUCU - JSON API
// ============================================================

void handleRoot() {
  // ONEMLI: bu sayfa buyudukce (~120KB+) "String html = R"html(...)" seklinde
  // heap'e KOPYALAMAK riskli - BLE+WiFi+MQTT+SPIFFS aktifken tek seferde bu
  // kadar buyuk kesintisiz bir heap bloğu bulunamayabilir (String::operator=
  // sessizce basarisiz olur, sonuc BOS/beyaz sayfa). Bunun yerine 'static
  // const char[]' olarak flash'ta (.rodata) tutup send_P/sendContent_P ile
  // DOGRUDAN client'a akitiyoruz - hic heap kopyasi olmuyor.
  static const char PAGE_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>SuDepoSis - Merkez Kontrol</title>
<style>
:root{--bg:#f6f8fa;--card:#fff;--text:#1f2937;--muted:#6b7280;--border:#e5e7eb;--primary:#2563eb;--accent:#10b981;--warn:#f59e0b;--danger:#ef4444;--danger-bg:#ffebee;--shadow:0 1px 3px rgba(0,0,0,.1)}
@media(prefers-color-scheme:dark){:root{--bg:#0b1220;--card:#111827;--text:#e5e7eb;--muted:#9ca3af;--border:#374151;--primary:#60a5fa;--accent:#34d399;--warn:#fbbf24;--danger:#f87171;--danger-bg:#3a2222;--shadow:0 1px 3px rgba(0,0,0,.4)}}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:var(--bg);color:var(--text);padding:12px;-webkit-tap-highlight-color:transparent}
.container{max-width:1100px;margin:0 auto}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
.header h1{font-size:20px}
.header .meta{font-size:12px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:16px}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:16px;box-shadow:var(--shadow)}
.card h3,.card summary{font-size:15px;font-weight:700;color:var(--text);margin-bottom:8px;letter-spacing:.2px}
.card summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px}
.card summary::-webkit-details-marker{display:none}
.card summary::before{content:'▸';display:inline-block;font-size:12px;color:var(--muted);transition:transform .15s}
.card[open]>summary::before{transform:rotate(90deg)}
.card.zone-sudepo{border-left:4px solid #3b82f6}
.card.zone-konteyner{border-left:4px solid var(--warn)}
.subdet{margin-top:16px;border-top:1px solid var(--border);padding-top:10px}
.subdet summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px;font-size:14px;font-weight:700;color:var(--text)}
.subdet summary::-webkit-details-marker{display:none}
.subdet summary::before{content:'▸';display:inline-block;font-size:11px;color:var(--muted);transition:transform .15s}
.subdet[open]>summary::before{transform:rotate(90deg)}
.remote-body{background:linear-gradient(180deg,var(--card),var(--bg));border:1px solid var(--border);border-radius:20px;padding:20px 12px;max-width:340px;margin:12px auto 0}
.remote-key{width:56px;height:56px;border-radius:50%;border:1px solid var(--border);background:var(--bg);color:var(--text);font-size:11px;font-weight:700;cursor:pointer;display:flex;align-items:center;justify-content:center;text-align:center;line-height:1.1;padding:2px}
.remote-key:hover{border-color:var(--primary)}
.remote-key.power{background:#dc2626;border-color:#dc2626;color:#fff}
.remote-key.assigned{border-color:var(--accent);box-shadow:0 0 0 2px rgba(16,185,129,.35)}
.remote-key.active-learning{border-color:var(--primary);box-shadow:0 0 0 3px rgba(37,99,235,.4);animation:pulse 1s infinite}
.remote-top{display:flex;justify-content:center;gap:28px;align-items:flex-start;flex-wrap:wrap;margin-bottom:14px}
.remote-left-col{display:flex;flex-direction:column;gap:10px;align-items:center}
.remote-dpad{display:grid;grid-template-columns:repeat(3,56px);grid-template-rows:repeat(3,56px);gap:6px}
.remote-dpad .rk-chp{grid-column:2;grid-row:1}
.remote-dpad .rk-volm{grid-column:1;grid-row:2}
.remote-dpad .rk-menu{grid-column:2;grid-row:2}
.remote-dpad .rk-volp{grid-column:3;grid-row:2}
.remote-dpad .rk-chm{grid-column:2;grid-row:3}
.remote-numpad{display:grid;grid-template-columns:repeat(4,56px);gap:8px;justify-content:center;margin-top:16px}
.remote-legend{display:flex;gap:14px;flex-wrap:wrap;margin-top:14px;font-size:11px;color:var(--muted)}
.remote-legend span{display:inline-flex;align-items:center;gap:5px}
.remote-legend i{width:12px;height:12px;border-radius:50%;display:inline-block;border:1px solid var(--border)}
.remote-legend i.assigned-dot{border-color:var(--accent);box-shadow:0 0 0 2px rgba(16,185,129,.35)}
.kpi{font-size:28px;font-weight:700}
.kpi small{font-size:12px;color:var(--muted);font-weight:400}
.bar{background:var(--border);height:18px;border-radius:999px;overflow:hidden;margin-top:8px}
.bar>div{height:100%;background:linear-gradient(90deg,var(--primary),var(--accent));width:0%;transition:width .4s}
.row{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}
.btn{padding:10px 12px;border:none;border-radius:8px;cursor:pointer;font-weight:600;flex:1;min-width:120px}
.btn-primary{background:var(--primary);color:#fff}
.btn-accent{background:var(--accent);color:#fff}
.btn-danger{background:var(--danger);color:#fff}
.btn-warn{background:var(--warn);color:#fff}
.btn:disabled{opacity:.6;cursor:not-allowed}
.status{display:inline-flex;align-items:center;gap:6px;font-weight:600}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;background:var(--muted)}
.dot.active{background:var(--accent)}
.dot.alarm{background:var(--danger);animation:pulse 1.2s infinite}
@keyframes pulse{0%{opacity:1}50%{opacity:.5}100%{opacity:1}}
#alarm-banner{background:var(--danger-bg);color:var(--danger);border:2px solid var(--danger);padding:16px;border-radius:12px;margin-bottom:14px;font-weight:700;font-size:16px;text-align:center;animation:pulse 1.2s infinite}
.table{width:100%;border-collapse:collapse;font-size:13px}
.table th,.table td{padding:8px 6px;border-bottom:1px solid var(--border);text-align:left}
.table th{color:var(--muted);font-weight:600}
.input{padding:10px 12px;border:1px solid var(--border);border-radius:8px;background:var(--card);color:var(--text);width:100%}
.sz-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px 12px;margin-top:8px}
.sz-label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}
.sz-radio{display:block;font-size:13px;padding:6px 0}
.sz-cbgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:6px 10px;margin-top:4px}
.sz-cbgrid label{display:flex;align-items:center;gap:5px;font-size:12px;font-weight:normal}
.sz-cbgrid input{width:auto}
.section{display:none}
.section.active{display:block}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.nav button{padding:8px 12px;border:1px solid var(--border);background:var(--card);color:var(--text);border-radius:8px;cursor:pointer}
.nav button.active{background:var(--primary);color:#fff;border-color:var(--primary)}
.alert{padding:10px;border-radius:8px;background:rgba(239,68,68,.15);color:var(--danger);border:1px solid rgba(239,68,68,.3);margin-bottom:10px}
.badge{display:inline-block;padding:4px 8px;border-radius:999px;background:rgba(16,185,129,.15);color:var(--accent);font-weight:700;font-size:12px}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div>
      <h1>SuDepoSis Merkez Kontrol</h1>
      <div class="meta">ESP32-S3 Master • RS485 Hub</div>
    </div>
    <div style="text-align:right">
      <div class="meta" id="clock">--</div>
      <div class="meta" id="wifi-topbar">WiFi: -</div>
    </div>
  </div>

  <div id="alarm-banner" style="display:none"></div>

  <div class="nav">
    <button onclick="show('dashboard')" id="nav-dashboard" class="active">Genel</button>
    <button onclick="show('kontrol')" id="nav-kontrol">Kontrol</button>
    <button onclick="show('ayarlar')" id="nav-ayarlar">Ayarlar</button>
    <button onclick="show('kumanda')" id="nav-kumanda">Kumanda</button>
    <button onclick="show('bilgiler')" id="nav-bilgiler">Bilgiler</button>
  </div>

  <div id="dashboard" class="section active">
    <div class="grid">
      <div class="card"><h3>Depo Doluluk</h3><div class="kpi" id="kpi-pct">--</div><div class="bar"><div id="bar-pct"></div></div></div>
      <div class="card"><h3>Seviye</h3><div class="kpi" id="kpi-cm">--</div><small>cm</small></div>
      <div class="card"><h3>Tahmini Hacim</h3><div class="kpi" id="kpi-litre">--</div><small>Litre</small></div>
      <div class="card"><h3>Sıcaklık</h3><div class="kpi" id="kpi-temp">--</div><small>°C</small></div>
      <div class="card"><h3>Toprak Nem</h3><div class="kpi" id="kpi-moisture">--</div><small>Nem %</small><div style="margin-top:8px;font-size:12px;color:var(--muted)">Ham: <b id="moisture-raw">-</b></div><div style="font-size:12px;color:var(--muted)">Çıkış: <b id="moisture-output">-</b> | Mod: <b id="moisture-mode">-</b></div></div>
    </div>

    <div class="card">
      <h3>Alarmlar</h3>
      <div id="alarm-box">
        <span class="status"><span class="dot active" id="alarm-dot"></span><span id="alarm-text">Sistem Normal</span></span>
      </div>
      <div id="hata-box" style="margin-top:8px;color:var(--warn);font-size:13px"></div>
    </div>

    <div class="card">
      <h3>RS485 Cihaz Durumu</h3>
      <div class="row">
        <div style="flex:1"><span class="badge" id="esp8266-badge">ESP8266: Bekleniyor</span></div>
        <div style="flex:1"><span class="badge" id="nano-badge">Nano: Bekleniyor</span></div>
      </div>
      <div class="row" style="gap:12px">
        <div><small>Kapı 1:</small> <b id="d1">-</b></div>
        <div><small>Kapı 2:</small> <b id="d2">-</b></div>
        <div><small>Alarm Röle:</small> <b id="rl">-</b></div>
        <div><small>Lamba:</small> <b id="lm">-</b></div>
        <div><small>Nem Röle:</small> <b id="mr">-</b></div>
      </div>
    </div>
  </div>

  <div id="kontrol" class="section">
    <div class="card">
      <h3>Lamba</h3>
      <div class="row">
        <button class="btn btn-primary" id="lamba-btn" onclick="toggleLamba()">Aç</button>
      </div>
      <div id="lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Alarm</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">Aç/Kapat anahtarları her zon için AYRI - birini kapatmak sadece o zonun tetikleyicilerini (siren/lamba/Telegram) susturur. Alarm Modu ve Sustur ise ortak (Sesli/Sessiz/Onaylı ikisi için de aynı).</p>
      <div class="row">
        <button class="btn btn-accent" id="alarm-btn" onclick="toggleAlarm()">Sudepo Zonu: Alarmı Kapat</button>
        <button class="btn btn-accent" id="konteyner-alarm-btn" onclick="toggleKonteynerAlarm()">Konteyner Zonu: Alarmı Kapat</button>
      </div>
      <div class="row" style="margin-top:8px">
        <button class="btn btn-primary" onclick="kapiKontrol(1)">Kapıyı Aç/Kapat</button>
      </div>
      <div class="row" style="margin-top:8px">
        <select id="alarm-mod-sel" onchange="setAlarmMod()">
          <option value="1">1 - Sesli</option>
          <option value="2">2 - Sessiz</option>
          <option value="3">3 - Onaylı</option>
        </select>
        <button class="btn btn-warn" id="alarm-mute-btn" onclick="alarmMute()">Sustur/Sireni Kapat</button>
      </div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">Onay bekleyen bir tetiklenme olursa "Sesli"/"Sessiz" secenekleri ekranin ustundeki uyari kutusunda (hangi sekmede olursan ol) cikar.</p>
      <div id="alarm-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="konteyner-alarm-sonuc" style="margin-top:4px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Panik</h3>
      <div class="row">
        <button class="btn btn-danger" id="panic-btn" onclick="togglePanic()">Panik</button>
      </div>
      <div id="panic-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>
  </div>

  <div id="ayarlar" class="section">
    <details class="card">
      <summary>OTA Güncelleme</summary>
      <p style="font-size:12px;color:var(--muted)">GitHub'daki en son firmware'i indirip yazar (main dalı).</p>
      <div class="row">
        <button class="btn btn-primary" onclick="otaGuncelle()">GitHub'dan Güncelle</button>
      </div>
      <div id="ota-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div class="row" style="margin-top:10px">
        <form id="otaDosyaForm" method="POST" action="/update" enctype="multipart/form-data" onsubmit="return otaDosyaOnay()">
          <input type="file" id="otaDosya" name="update" accept=".bin" required>
          <button class="btn btn-primary" type="submit">Dosyadan Yukle</button>
        </form>
      </div>
      <div id="ota-dosya-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>ESP8266 Firmware Deposu</summary>
      <p style="font-size:12px;color:var(--muted)">Bahcede internet olmadigindan, ESP8266'nin "URL'den Guncelle" kutusuna GitHub yerine buradaki adresi yaz - ikisi ayni WiFi agindayken calisir. ESP8266'nin web arayuzu artik firmware'in icine gomulu oldugu icin tek dosya (esp8266.bin) yeterli.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="fw-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <input type="file" id="fwDosya" accept=".bin">
        <button class="btn btn-primary" onclick="firmwareYukle()">Yukle</button>
      </div>
      <div id="fw-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Kayit Yedekleme</summary>
      <p style="font-size:12px;color:var(--muted)">ESP8266'nin kayitlar.csv dosyasinin yedegi - donanim arizasi/factory reset gibi durumlarda buradan geri yukleyebilirsin.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="yedek-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <button class="btn btn-primary" onclick="kayitYedekle()">Simdi Yedekle</button>
        <button class="btn btn-warn" onclick="kayitGeriYukle()">ESP8266'ya Geri Yukle</button>
      </div>
      <div id="yedek-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Hava Durumu / Yağmur Tahmini</summary>
      <p style="font-size:12px;color:var(--muted)">Sabit konum (bahçe) - internet varken (örn. telefon hotspot'u) otomatik çekilir. 7 günden eski tahmin dikkate alınmaz, o durumda sulama normal devam eder.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="weather-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <button class="btn btn-warn" onclick="weatherKontrolEt()">Şimdi Kontrol Et</button>
      </div>
      <div id="weather-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="weather-haftalik" style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap"></div>
    </details>

    <details class="card" open>
      <summary>Telegram Bildirimleri</summary>
      <p style="font-size:12px;color:var(--muted)">Alarm YENİ başladığında (panik, kapı, PIR, kaçak vb. - Kalburum/Konteyner PIR'ı dahil) Telegram'a bildirim gönderir - sadece bu cihazın o an interneti varsa (örn. hotspot bağlıyken) çalışır.</p>
      <div class="row">
        <button class="btn" id="telegram-ac-kapa-btn" onclick="telegramAcKapa()">Yükleniyor...</button>
        <button class="btn btn-primary" onclick="telegramTest()">Test Mesajı Gönder</button>
      </div>
      <div id="telegram-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card zone-sudepo" open>
      <summary>🚰 Su Deposu Zonu - Ayarlar</summary>
      <p style="font-size:12px;color:var(--muted)">Karar/yürütme hâlâ ESP8266+Nano'da yapılır (RS485 gecikmesi olmadan tepki verir) - burası sadece tek panelden yönetebilmen için köprü. Sudepo.local aynı ayarları gösterir.</p>
      <div id="sz-yukleniyor" style="font-size:12px;color:var(--muted)">Yükleniyor...</div>
      <div id="sz-form" style="display:none">
        <h3>Kalibrasyon &amp; Eşikler</h3>
        <p style="font-size:11px;color:var(--muted);margin-top:-4px">Bir alanı değiştirip dışına tıklayınca otomatik kaydedilir.</p>
        <div class="sz-grid">
          <div><label class="sz-label">Boş Mesafe (cm)</label><input class="input" type="number" step="0.1" id="sz_bosMesafe" onchange="szKaydet()"></div>
          <div><label class="sz-label">Dolu Mesafe (cm)</label><input class="input" type="number" step="0.1" id="sz_doluMesafe" onchange="szKaydet()"></div>
          <div><label class="sz-label">Kapasite (L)</label><input class="input" type="number" step="1" id="sz_kapasite" onchange="szKaydet()"></div>
          <div><label class="sz-label">Depo Şekli</label><select class="input" id="sz_depoYatay" onchange="szKaydet()"><option value="1">Yatay</option><option value="0">Dikey</option></select></div>
          <div><label class="sz-label">Alarm Eşiği (%)</label><input class="input" type="number" step="1" id="sz_alarmYuzde" onchange="szKaydet()"></div>
          <div><label class="sz-label">Dolum Eşiği (L)</label><input class="input" type="number" step="1" id="sz_minDolumLitre" onchange="szKaydet()"></div>
          <div><label class="sz-label">Kaçak Eşiği (dk)</label><input class="input" type="number" step="1" id="sz_kacakEsikDakika" onchange="szKaydet()"></div>
        </div>

        <h3 style="margin-top:18px">Alarm Modu, Gece &amp; PIR</h3>
        <div style="margin:8px 0">
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod1" value="1" onchange="szKaydet()"> <b>1 - Sesli</b> (siren hemen çalışır)</label>
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod2" value="2" onchange="szKaydet()"> <b>2 - Sessiz</b> (siren çalışmaz, sadece bildirim)</label>
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod3" value="3" onchange="szKaydet()"> <b>3 - Onaylı</b> (onay verince sesli gibi çalışır)</label>
        </div>
        <p style="font-size:11px;color:var(--muted)">Bu mod Kalburum/Konteyner Zonu için de geçerlidir (ortak Alarm Modu) - Kontrol sekmesindeki seçici ile de değiştirilebilir.</p>
        <div class="sz-grid">
          <div><label class="sz-label">Gece Başlangıç (saat)</label><input class="input" type="number" min="0" max="23" id="sz_geceBaslangic" onchange="szKaydet()"></div>
          <div><label class="sz-label">Gece Bitiş (saat)</label><input class="input" type="number" min="0" max="23" id="sz_geceBitis" onchange="szKaydet()"></div>
          <div><label class="sz-label">PIR Pencere (sn)</label><input class="input" type="number" min="0" max="120" id="sz_pirPencereSaniye" onchange="szKaydet()"></div>
          <div><label class="sz-label">PIR Min. Tetiklenme</label><input class="input" type="number" min="1" max="10" id="sz_pirMinTetiklenme" onchange="szKaydet()"></div>
        </div>

        <details class="subdet">
          <summary>Zamana Bağlı Tetikleyiciler</summary>
          <p class="sz-label">Gündüz</p>
          <div class="sz-cbgrid" id="sz-grid-gunduz"></div>
          <p class="sz-label" style="margin-top:10px">Gece</p>
          <div class="sz-cbgrid" id="sz-grid-gece"></div>
        </details>

        <details class="subdet">
          <summary>Mod Senaryoları</summary>
          <p style="font-size:12px;color:var(--muted)">Her modu hangi sensörlerin tetikleyeceği (girdi) ve neyin çalışacağı (çıkış).</p>
          <p class="sz-label" style="margin-top:8px">Sesli - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-sesli-girdi"></div>
          <p class="sz-label" style="margin-top:6px">Sesli - Çıkış</p>
          <div class="sz-cbgrid" id="sz-grid-sesli-cikis"></div>
          <p class="sz-label" style="margin-top:10px">Sessiz - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-sessiz-girdi"></div>
          <p class="sz-label" style="margin-top:6px">Sessiz - Çıkış</p>
          <div class="sz-cbgrid" id="sz-grid-sessiz-cikis"></div>
          <p class="sz-label" style="margin-top:10px">Onaylı - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-onayli-girdi"></div>
        </details>

        <div id="sz-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      </div>
    </details>

    <details class="card zone-konteyner" open>
      <summary>👁️ Konteyner Zonu - PIR Ayarları</summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Konteynerdaki 2. PIR (HC-SR505) artık genel <b>Alarm Modu</b>'na (Sesli/Sessiz/Onaylı) uyar, Telegram ve büyük uyarı banner'ına da yansır - Sudepo Zonu ile aynı ortak sistemi kullanır.</p>
      <p style="font-size:12px;color:var(--warn);margin-bottom:8px"><b>Not:</b> HC-SR505'in potansiyometresi yok, tetiklenince çıkışı hareketin büyüklüğüne bakmaksızın sabit ~6-12sn HIGH'ta kalır (Sudepo'daki ayarlanabilir HC-SR501'den farklı). Bu yüzden "hassasiyet" iki kademeli süre mantığıyla ayarlanır: <b>1)</b> her yeni harekette hemen 1sn'lik kısa bir bip/LED darbesi verilir (henüz alarm değil, sadece "sensör gördü" işareti); <b>2)</b> hareket kesintisiz "Onay Süresi" kadar sürerse GERÇEK alarm sayılır ve Alarm Modu'na göre tepki verir. Kısa/ufak/tek seferlik kıpırdamalar böylece sadece bir bip ile geçer, Telegram/siren tetiklemez.</p>
      <div class="row" style="gap:16px">
        <div>Hareket: <b id="kz-pir">-</b></div>
        <div>Kapı: <b id="kz-kapi">-</b></div>
        <div>Yerel Uyarı (LED/Buzzer): <b id="kz-alarm">-</b></div>
        <div>Siren: <b id="kz-siren">-</b></div>
        <div>Lamba: <b id="kz-lamba">-</b></div>
      </div>
      <div class="sz-grid" style="margin-top:10px">
        <div><label class="sz-label">Tutma Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirTutma" onchange="konteynerPirKaydet()"></div>
        <div><label class="sz-label">Onay Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirOnay" onchange="konteynerPirKaydet()"></div>
      </div>
      <p style="font-size:11px;color:var(--muted);margin-top:4px">Tutma Süresi: hareket bittikten sonra ne kadar daha aktif sayılsın. Onay Süresi: kesintisiz hareketin kaç saniye sonra GERÇEK alarma dönüşeceği. Değer girip alandan çıkınca otomatik kaydedilir.</p>
      <div id="kz-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card" open>
      <summary>WiFi</summary>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="wifi-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <select class="input" id="staSSIDSel"><option value="">Ağları tara...</option></select>
        <button class="btn btn-warn" onclick="wifiScan()">Ağları Tara</button>
      </div>
      <div class="row">
        <input class="input" id="staSSID" placeholder="Ya da elle SSID girin">
        <input class="input" id="staPASS" placeholder="Şifre" type="password">
      </div>
      <div class="row">
        <button class="btn btn-primary" onclick="wifiKaydet()">Bağlan &amp; Kaydet</button>
        <button class="btn btn-danger" onclick="wifiKaldir()">Kaldır</button>
      </div>
      <div id="wifi-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card" open>
      <summary>Nem Ayarları</summary>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)">
        Nem: <b id="settings-moisture-val">-</b> | Çıkış: <b id="settings-moisture-out">-</b> | Mod: <b id="settings-moisture-mod">-</b>
      </div>
      <div class="row">
        <button class="btn btn-primary" id="moisture-settings-toggle-btn" onclick="toggleMoisture()">Aç/Kapat</button>
        <button class="btn btn-accent" id="moisture-settings-auto-btn" onclick="toggleMoistureAuto()">Otomatik / Manuel</button>
      </div>
      <div style="margin-top:10px;">
        <label style="display:block;font-size:12px;color:var(--muted);">Alt Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-low" min="0" max="100" style="width:120px;display:inline-block;" value="0" onchange="setMoistureThresholds()">
        <label style="display:block;font-size:12px;color:var(--muted);margin-top:8px;">Üst Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-high" min="0" max="100" style="width:120px;display:inline-block;" value="0" onchange="setMoistureThresholds()">
      </div>
      <div id="moisture-settings-msg" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card" open>
      <summary>Sistem</summary>
      <div class="row">
        <button class="btn btn-danger" onclick="restartSistem()">Yeniden Başlat</button>
      </div>
    </details>
  </div>

  <div id="kumanda" class="section">
    <details class="card zone-konteyner" open>
      <summary>🎛️ Kumanda Haritası</summary>
      <p style="font-size:12px;color:var(--muted)">Aşağıdaki görsel, örnek bir evrensel kumandanın tuş düzenini gösterir - gerçek kumandanız farklı görünse de sorun değil. Bir tuşa tıklayın, sonra GERÇEK kumandanızda karşılık gelen tuşa basın: <b>tıklama IR sinyali göndermez</b>, sadece hangi tuşu öğrettiğinizi işaretlemenin bir yolu.</p>
      <div class="remote-body">
        <div class="remote-top">
          <div class="remote-left-col">
            <button class="remote-key power" data-key="POWER" onclick="irTusaTikla('POWER')">POWER</button>
            <button class="remote-key" data-key="MUTE" onclick="irTusaTikla('MUTE')">🔇</button>
            <button class="remote-key" data-key="AV/TV" onclick="irTusaTikla('AV/TV')">AV/TV</button>
          </div>
          <div class="remote-dpad">
            <button class="remote-key rk-chp" data-key="CH+" onclick="irTusaTikla('CH+')">CH+</button>
            <button class="remote-key rk-volm" data-key="VOL-" onclick="irTusaTikla('VOL-')">VOL-</button>
            <button class="remote-key rk-menu" data-key="MENU" onclick="irTusaTikla('MENU')">MENU</button>
            <button class="remote-key rk-volp" data-key="VOL+" onclick="irTusaTikla('VOL+')">VOL+</button>
            <button class="remote-key rk-chm" data-key="CH-" onclick="irTusaTikla('CH-')">CH-</button>
          </div>
        </div>
        <div class="remote-numpad">
          <button class="remote-key" data-key="1" onclick="irTusaTikla('1')">1</button>
          <button class="remote-key" data-key="2" onclick="irTusaTikla('2')">2</button>
          <button class="remote-key" data-key="3" onclick="irTusaTikla('3')">3</button>
          <button class="remote-key" data-key="4" onclick="irTusaTikla('4')">4</button>
          <button class="remote-key" data-key="5" onclick="irTusaTikla('5')">5</button>
          <button class="remote-key" data-key="6" onclick="irTusaTikla('6')">6</button>
          <button class="remote-key" data-key="7" onclick="irTusaTikla('7')">7</button>
          <button class="remote-key" data-key="8" onclick="irTusaTikla('8')">8</button>
          <button class="remote-key" data-key="9" onclick="irTusaTikla('9')">9</button>
          <button class="remote-key" data-key="0" onclick="irTusaTikla('0')">0</button>
          <button class="remote-key" data-key="-/--" onclick="irTusaTikla('-/--')">-/--</button>
          <button class="remote-key" data-key="AUTO" onclick="irTusaTikla('AUTO')">AUTO</button>
        </div>
      </div>
      <div class="remote-legend">
        <span><i class="assigned-dot"></i> Atanmış tuş</span>
        <span><i></i> Boş tuş</span>
      </div>
      <p style="font-size:11px;color:var(--muted);margin-top:8px">Önerilen eşleşmeler: POWER=Alarm Aç/Kapat, MUTE=Sustur, AV/TV=Panik, CH+=Lamba Aç, CH-=Lamba Kapat, VOL+=Kapı Aç, VOL-=Kapı Kapat, MENU=Kapı Aç/Kapat, 1/2/3=Mod Sesli/Sessiz/Onaylı, 0=Onayla, AUTO=Lamba Aç/Kapat. Öğrenirken açılan listede bu öneri otomatik seçili gelir, dilerseniz değiştirebilirsiniz. Boş kalan tuşlar (4-9, -/--) ileride ek komutlar için serbesttir.</p>
    </details>

    <details class="card zone-konteyner" open>
      <summary>📡 Tanımlı Tuşlar</summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Konteynerdaki IR alıcıya kumanda tuşu tanımla - yukarıdaki haritadan bir tuşa tıklayın ya da doğrudan "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa basın, sonra hangi komutu çalıştıracağını seçin. Birden fazla kumanda eklenebilir.</p>
      <div id="ir-liste" style="font-size:13px">Yükleniyor...</div>
      <div class="row" style="margin-top:10px">
        <button class="btn btn-primary" onclick="irOgrenBaslat()">➕ Yeni Tuş Öğren (haritasız)</button>
      </div>
      <div id="ir-ogren-durum" style="margin-top:8px;font-size:13px"></div>
    </details>
  </div>

  <div id="bilgiler" class="section">
    <details class="card zone-konteyner">
      <summary>ESP32 Master Pinout (Konteyner)</summary>
      <table class="table">
        <tr><th>Pin</th><th>GPIO</th><th>Modül</th><th>Fonksiyon / Bağlantı</th></tr>
        <tr><td>RX (UART1)</td><td>37</td><td>MAX485 RS485</td><td>RO (Alıcı)</td></tr>
        <tr><td>TX (UART1)</td><td>38</td><td>MAX485 RS485</td><td>DI (Verici)</td></tr>
        <tr><td>-</td><td>39</td><td>MAX485 RS485</td><td>DE/RE (Enable)</td></tr>
        <tr><td>UART0</td><td>1/3</td><td>Debug Serial</td><td>9600 baud (USB programlama/monitör)</td></tr>
        <tr><td>D4</td><td>4</td><td>IR Alıcı Modülü</td><td>OUT/sinyal ucu bu pine; VCC/GND ayrı (3.3V veya 5V modüle göre) besleme hattından</td></tr>
        <tr><td>D5</td><td>5</td><td>Kırmızı LED + Buzzer</td><td>İkisi PARALEL bu pine bağlı (pin tasarrufu) - LED'e seri direnç (~220-330Ω) şart, buzzer aktif tip olmalı (kendi osilatörü olan, doğrudan HIGH/LOW ile çalışan)</td></tr>
        <tr><td>D6</td><td>6</td><td>PIR HC-SR505 (Konteyner)</td><td>OUT ucu bu pine; VCC/GND sensörün kendi besleme uçlarından (mini tip, 3.3-5V)</td></tr>
        <tr><td>D7</td><td>7</td><td>Kapı Reed Switch</td><td>Bir ucu bu pine, diğer ucu GND'ye (dahili pull-up kullanılıyor, ek direnç gerekmez)</td></tr>
        <tr><td>D8</td><td>8</td><td>Alarm Sireni Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif (Sudepo Zonu'ndaki "Alarm Rölesi" ile aynı mantık)</td></tr>
        <tr><td>D9</td><td>9</td><td>Uyarı Lambası Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif - siren ile birlikte VEYA Onaylı modda "Sessiz (Lamba)" onayında tek başına yanar</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>Not:</b> Reed switch'in "açık/kapalı" okuma yönü (HIGH=açık mı kapalı mı) kablolamaya göre ters olabilir - <code>/api/status</code>'taki <code>konteyner.kapi_acik</code> alanından gerçek davranışı görüp gerekirse kod tarafında (main.cpp, <code>konteynerSensorleriOku()</code>) tek satır değiştirerek düzeltilir. Siren/Lamba röleleriniz aktif-LOW ise aynı şekilde <code>alarmLedGuncelle()</code>'daki <code>digitalWrite</code> satırları ters çevrilir.</p>
      <p style="font-size:12px;color:var(--muted);margin-top:4px"><b>Serbest/kullanılabilir GPIO'lar</b> (ileride yeni eklenti için): 2, 10-15, 16, 17, 18, 21, 33, 34, 36, 40, 41, 42, 47, 48. <b>Asla kullanılmaması gerekenler:</b> 0, 3, 45, 46 (strapping/boot pinleri), 26-32 (Quad Flash için ayrılmış).</p>
    </details>

    <details class="card zone-sudepo">
      <summary>ESP8266 Slave Pinout</summary>
      <table class="table">
        <tr><th>Pin (NodeMCU)</th><th>GPIO</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>D0</td><td>16</td><td>MAX485</td><td>DI (RS485 TX)</td></tr>
        <tr><td>D7</td><td>13</td><td>MAX485</td><td>RO (RS485 RX)</td></tr>
        <tr><td>D3</td><td>0</td><td>MAX485</td><td>DE/RE (Enable)</td></tr>
        <tr><td>D5</td><td>14</td><td>HC-SR04</td><td>TRIG</td></tr>
        <tr><td>D6</td><td>12</td><td>HC-SR04</td><td>ECHO</td></tr>
        <tr><td>D1</td><td>5</td><td>DS1307 RTC</td><td>SCL</td></tr>
        <tr><td>D2</td><td>4</td><td>DS1307 RTC</td><td>SDA</td></tr>
        <tr><td>RX (D9)</td><td>3</td><td>Arduino Nano</td><td>UART0 RX ← Nano TX</td></tr>
        <tr><td>TX (D10)</td><td>1</td><td>Arduino Nano</td><td>UART0 TX → Nano RX</td></tr>
        <tr><td>A0</td><td>ADC0</td><td>Toprak Nem</td><td>Analog nem sensörü</td></tr>
      </table>
    </details>

    <details class="card zone-sudepo">
      <summary>Arduino Nano IO (v2 Pin Planı)</summary>
      <table class="table">
        <tr><th>Pin</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>D0 (RX)</td><td>ESP8266 TX</td><td>Seri haberleşme</td></tr>
        <tr><td>D1 (TX)</td><td>ESP8266 RX</td><td>Seri haberleşme</td></tr>
        <tr><td>D2</td><td>Kapı 1 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D3</td><td>Kapı 2 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D4</td><td>Alarm rölesi</td><td>OUTPUT (HIGH=aktif)</td></tr>
        <tr><td>D5</td><td>Nem rölesi</td><td>OUTPUT</td></tr>
        <tr><td>D6</td><td>PIR HC-SR501 hareket sensörü</td><td>INPUT - ESP8266 PIN_READ:6 ile okur (Nano kodu değişmez)</td></tr>
        <tr><td>D7-D12</td><td>Yedek GPIO</td><td>ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik</td></tr>
        <tr><td>D13</td><td>Depo iç lamba rölesi</td><td>OUTPUT (sadece lamba)</td></tr>
        <tr><td>A0-A5</td><td>Yedek GPIO</td><td>Analog + digital I/O</td></tr>
        <tr><td>A6-A7</td><td>Yedek</td><td>Sadece analog input</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>GPIO Komut Protokolü (ESP→Nano):</b> PIN_MODE:<pin>,<mod> | PIN_WRITE:<pin>,<0/1> | PIN_READ:<pin> | PIN_READ_ALL</p>
    </details>

    <details class="card" open>
      <summary>WiFi & Sistem</summary>
      <div id="bilgi-sistem" style="font-size:13px">Yükleniyor...</div>
      <div id="build-info" style="font-size:12px;color:var(--muted);margin-top:8px">Yükleniyor...</div>
    </details>

    <details class="card" open>
      <summary>Güncelleme Aralıkları</summary>
      <div id="guncelleme-bilgi" style="font-size:13px">Yükleniyor...</div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">SSE (anlık push): ESP8266/Nano'dan yeni veri gelir gelmez, en geç 1sn'de bir yedek olarak. Tarayıcı 5sn'de bir de yedek polling yapar (SSE koparsa fark edilmesin diye).</p>
    </details>

    <details class="card" open>
      <summary>Kullanım Kılavuzu</summary>
      <div style="font-size:13px;line-height:1.6">
        <p><b>Alarm Modları</b> (Kontrol → Alarm): 1-Sesli (tetiklenince siren hemen çalışır), 2-Sessiz (siren çalışmaz, sadece bu sayfada/ESP8266'da bildirim), 3-Onaylı (tetiklenince onay bekler, "Tetiklenmeyi Onayla" ile sesli moda geçer). Bu mod artık hem Sudepo Zonu hem Kalburum/Konteyner için ORTAK - Konteyner PIR'ı eskale olup gerçek alarma dönüştüğünde de aynı moda göre davranır (bkz Ayarlar → Konteyner Zonu PIR Ayarları).</p>
        <p><b>Sustur/Sireni Kapat:</b> Alarm koşulu sürse bile röleyi susturur; koşul temizlenince otomatik sıfırlanır.</p>
        <p><b>Panik:</b> Tetikleyicilerden bağımsız, elle aç/kapat anahtarı gibi çalışır - röleyi zorla açık tutar. Konteyner'in siren+lambasını da (Alarm Modu/etkin-pasif durumundan bağımsız) doğrudan tetikler.</p>
        <p><b>Kapı/PIR/Kaçak/Düşük seviye</b> tetikleyicileri ve gündüz/gece + mod bazlı senaryolar ESP8266 panelinin "Alarm" sekmesinden ayarlanır (bu panel sadece görüntüler ve mod/susturma/onay/panik komutlarını iletir).</p>
        <p><b>WiFi:</b> Ayarlar sekmesinden ev ağını tarayıp kaydedebilirsiniz; AP (<code id="ag-ap-bilgi">-</code>) STA bağlantısından bağımsız her zaman açıktır, ağ ayarları NVS'de kalıcıdır.</p>
        <p><b>Hava Durumu / Yağmur Tahmini:</b> Bahçenin sabit konumu için haftalık tahmin, bu cihazın interneti olduğu anda (örn. telefon hotspotu bağlıyken) otomatik çekilip hafızada saklanır. Bahçede kalıcı internet olmadığı için tahmin bayatlayabilir - 7 günden eski ise dikkate alınmaz ve sulama normal devam eder. Yarın yağmur bekleniyorsa bugünkü sulama otomatik atlanır (su israfını önlemek için).</p>
        <p><b>Telegram Bildirimleri:</b> Bir alarm YENİ tetiklendiğinde (panik, kapı, PIR, kaçak, sensör hatası) sayfa açık olmasa bile telefona Telegram mesajı gider - yalnızca cihazın o an interneti varsa (hotspot bağlıyken) çalışır, aksi halde birkaç dakika tekrar denenir. Bot token/chat ID <code>secrets.h</code> içinde saklanır.</p>
        <p><b>Mimari:</b> Nano (kapı/röle/lamba/PIR/nem çıkışı) ⇄ ESP8266 (sensörler + web) ⇄ RS485 ⇄ ESP32 (bu panel, MQTT yayını). Nano firmware'i genel amaçlı GPIO komutlarıyla (PIN_MODE/WRITE/READ) çalıştığı için donanım eklemelerinde çoğunlukla yeniden flaşlanması gerekmez.</p>
        <p><b>Kumanda:</b> IR kumanda tuş eşleştirmesi ayrı "Kumanda" sekmesinde - görsel kumanda üzerinden tuşa tıklamak IR sinyali göndermez, sadece hangi tuşu öğrettiğinizi işaretler; kod gerçek kumandada o tuşa basılınca yakalanır.</p>
      </div>
    </details>
  </div>
</div>

<script>
const $=s=>document.querySelector(s);

// === SERVER TRUTH MODEL ===
// Tüm state sunucudan gelir. Komut sonrası local state YOK.
// guncelle() her şeyi taze sunucu verisinden çizer.
// Butonlar sadece "işlem sürüyor" sırasında '...' gösterir,
// sonra guncelle() doğru metni yazar.

// Global kilit yerine buton-bazli kilit: bir komut surerken sadece o buton
// kilitlenir, digerleri kullanilabilir kalir. RS485 gercek gonderimi zaten
// ESP32 tarafinda (rs485_api_busy) tek seferde bir komutla sinirlanir, bu
// yuzden ayni anda iki buton basilsa bile fiziksel komutlar siraya girer -
// UI'da hepsini kilitlemeye gerek yok.
let busySet = new Set();

// Bir input "Kaydet" butonu olmadan onchange ile otomatik kaydedilince, kayit
// butonuna tiklamanin aksine input BLUR olur (odak kaybolur) - tam o anda
// gelen bir periyodik guncelleme (SSE/poll) sunucunun henuz eski degerini
// gosterip degeri bir an icin "eski hale donup tekrar gelmis" gibi
// gosterebilirdi (odak koruması artik gecerli degil). Bu, alani kisa bir
// sure (varsayilan 2.5sn) "yakinda kullanici tarafindan degistirildi" olarak
// isaretleyip o sure icinde sunucudan gelen degerle UZERINE YAZMAYI atlayan
// basit bir koruma.
const yakinDuzenlenenler = new Map(); // id -> koruma bitis zamani (ms)
function yakinDuzenlendi(id, ms=2500){ yakinDuzenlenenler.set(id, Date.now()+ms); }
function yakinKorumali(id){ const t=yakinDuzenlenenler.get(id); return !!t && Date.now()<t; }

// Banner butonlari (Sesli/Sessiz(Lamba)/Sustur/Panik Kapat) dinamik olarak
// innerHTML ile her SSE/poll tikinde yeniden yaziliyor - sabit bir #id'leri
// yok, bu yuzden busySet/yakinKorumali ile eslesmiyorlar. bannerIslemSuruyor
// true iken guncelle() banner'i YENIDEN YAZMAZ (bkz asagida), boylece
// tiklanan butonun 'islemde' gorunumu yanit gelene kadar korunur.
let bannerIslemSuruyor = false;

// Alarm tetiklendiginde kisa bip - ESP8266 panelindeki ile ayni desen.
// Sadece "kapali -> acik" gecisinde calar, her renderUI'da degil.
let alarmOncekiDurum = false;
function bipSesi(){
  try{
    const ctx = new (window.AudioContext||window.webkitAudioContext)();
    const osc = ctx.createOscillator(), gain = ctx.createGain();
    osc.type='square'; osc.frequency.value=880;
    gain.gain.setValueAtTime(0.15, ctx.currentTime);
    osc.connect(gain); gain.connect(ctx.destination);
    osc.start(); osc.stop(ctx.currentTime+0.18);
    osc.onended=function(){ctx.close();};
  }catch(e){}
}

const tetikleyiciAdlari=['Sol Kapi','Sag Kapi','PIR','Su Seviyesi','Kacak','Sensor Hatasi'];
function tetikleyenMetni(mask,panicAktif,konteynerPir,konteynerKapi){
  if(panicAktif) return 'Panik (elle acildi)';
  const l=[];
  for(let i=0;i<6;i++) if(mask&(1<<i)) l.push(tetikleyiciAdlari[i]);
  if(konteynerPir) l.push('Konteyner PIR (Hareket)');
  if(konteynerKapi) l.push('Konteyner Kapı');
  return l.length?l.join(', '):'-';
}

function show(id){
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.nav button').forEach(b=>b.classList.remove('active'));
  document.getElementById('nav-'+id).classList.add('active');
}

function renderUI(d){
  const mo = d.moisture||{};
  // Su seviyesi - bu degerler ESP8266'dan geliyor (sensorData struct, RS485
  // ile dolduruluyor). ESP8266 baglantisi koparsa backend bu alanlari
  // SIFIRLAMIYOR, son bilinen degeri struct'ta tutmaya devam ediyor (bkz
  // durumJson main.cpp) - bu yuzden burada esp8266_online kontrolu YAPMAZSAK
  // bağlanti koptuktan cok sonra bile son gorulen (artik yanlis/eski) seviye
  // ekranda "canliymis gibi" kalirdi. FIX: baglanti yoksa "--" goster.
  const esp8266Baglı = d.esp8266_online !== false;
  if(esp8266Baglı){
    $('#kpi-pct').textContent=(d.level_percent||0).toFixed(1)+'%';
    $('#kpi-cm').textContent=(d.level_cm||0).toFixed(1)+' cm';
    $('#kpi-litre').textContent=(d.level_liters||0).toFixed(0)+' L';
    $('#kpi-temp').textContent=(d.temperature||0).toFixed(1)+' °C';
    $('#bar-pct').style.width=Math.max(0,Math.min(100,d.level_percent||0))+'%';
  } else {
    $('#kpi-pct').textContent='--';
    $('#kpi-cm').textContent='--';
    $('#kpi-litre').textContent='--';
    $('#kpi-temp').textContent='--';
    $('#bar-pct').style.width='0%';
  }
  // Alarm
  const ad=$('#alarm-dot'); let at='Sistem Normal';
  // FIX: alarm sistemi kapaliyken (d.alarm.enabled===false) bile ham kapi/
  // kacak/seviye durumuna bakip banner gosteriyordu - esp8266_slave ise
  // sistem kapaliyken hicbir sey gostermiyor (tetikleyici mask'i sifirliyor).
  // Ikisi tutarli olsun diye burada da enabled kontrolu eklendi.
  // FIX: anyAlarm sadece leak/low_level/door bayraklarina bakiyordu - PIR ve
  // sensor hatasi tetikleyicileri bu 3 bayraga hic yansimiyor (sadece
  // trigger_mask'te var), yani PIR ile tetiklenen bir alarmda banner HICBIR
  // modda (sessiz dahil) gorunmuyordu. trigger_mask ESP8266'nin zaten
  // mod+zaman senaryosuna gore filtreledigi otoriter kaynak - artik o
  // kullaniliyor. Panik de ayrica eklendi (eskiden hic banner tetiklemiyordu).
  const alarmMask = (d.alarm && d.alarm.trigger_mask) || 0;
  // Konteyner (ESP32-yerel) sensorleri de genel alarm sistemine dahil - ama
  // KENDI bagimsiz ac/kapa anahtarina (d.konteyner.enabled) uyar, Sudepo'nun
  // d.alarm.enabled'inden AYRI - iki zon birbirinden bagimsiz kapatilabilir.
  const enabledMi = !(d.alarm && d.alarm.enabled === false);
  const kz = d.konteyner||{};
  const konteynerEnabledMi = !(kz.enabled === false);
  const konteynerPirVar = konteynerEnabledMi && !!kz.pir_alarm;
  const konteynerKapiVar = konteynerEnabledMi && !!kz.kapi_acik;
  // Panik, alarm sistemi kapali (enabled===false) olsa bile ESP8266 tarafinda
  // her seyin onunde calisir (bkz esp8266_slave main.cpp panicRoleAktif) - bu
  // yuzden panic iken enabled kontrolunu atlar, aksi halde alarm sistemi
  // kapatilmisken panik basilinca banner hic gorunmuyordu.
  const anyAlarm = !!(d.alarm && ((enabledMi && alarmMask !== 0) || d.alarm.panic || konteynerPirVar || konteynerKapiVar));
  ad.className = anyAlarm ? 'dot alarm' : 'dot active';
  if(d.alarm){
    if(d.alarm.panic) at='PANİK AKTİF';
    else if(d.alarm.leak) at='ALARM: Kaçak!';
    else if(d.alarm.low_level) at='ALARM: Düşük seviye!';
    else if(d.alarm.door) at='ALARM: Kapı açık!';
    else if(alarmMask & 4) at='ALARM: Hareket algılandı!';
    else if(alarmMask & 32) at='ALARM: Sensör hatası!';
    else if(konteynerPirVar) at='ALARM: Konteyner hareket!';
    else if(konteynerKapiVar) at='ALARM: Konteyner kapı açık!';
  }
  $('#alarm-text').textContent=at;
  // Buyuk uyari banner'i - ESP8266'daki gibi, tetiklendiginde sayfanin
  // her sekmesinde gorunur olsun diye header'in hemen altina konuldu.
  const ban=$('#alarm-banner');
  if(ban){
    const bekliyor = (d.alarm && d.alarm.pending) || (d.konteyner && d.konteyner.pending);
    const alarmSimdiVar = !!(anyAlarm || bekliyor);
    if(alarmSimdiVar && !alarmOncekiDurum) bipSesi();
    alarmOncekiDurum = alarmSimdiVar;
    const panikAktif = !!(d.alarm && d.alarm.panic);
    if(anyAlarm || bekliyor){
      // Panik: susturma/tetikleyen bilgisi anlamsiz (esp8266 tarafinda panik
      // susturmayi hic hesaba katmiyor, tetikleyici de yok - elle acildi) -
      // sadece "Panik Kapat" gosterilir. Bkz esp8266_slave data/app.js (ayni
      // duzeltme orada da yapildi, iki panel tutarli olsun diye).
      let msg = panikAktif ? at : (bekliyor ? ('ONAY BEKLIYOR - '+at) : at);
      if(!panikAktif){
        if(d.alarm && d.alarm.muted) msg += ' (Susturuldu)';
        const tk = tetikleyenMetni((d.alarm&&d.alarm.trigger_mask)||0, false, konteynerPirVar, konteynerKapiVar);
        msg += ' | Tetikleyen: '+tk;
      }
      let html = '⚠ '+msg;
      if(panikAktif){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/panic\')">Panik Kapat</button></div>';
      } else if(bekliyor){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/alarm/onayla\')">Sesli</button><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/onayla_lamba\')">Sessiz (Lamba)</button></div>';
      } else if(anyAlarm){
        const susLabel = (d.alarm && d.alarm.muted) ? 'Susturmayi Kaldir' : 'Sustur/Sireni Kapat';
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/mute\')">'+susLabel+'</button></div>';
      }
      // bannerIslemSuruyor: bir banner butonuna basilip yaniti beklenirken
      // (RS485 komutu + retry birkac saniye surebilir) SSE/poll tetikledigi bu
      // fonksiyon banner'i YENIDEN YAZMASIN - yoksa kullanicinin az once
      // tikladigi (disabled/'...' yapilmis) buton, hicbir geri bildirim
      // vermeden sessizce "eski haline" donup TEKRAR tiklanabilir hale
      // gelirdi, tepkinin "cok gec" gelmesi hissi buradan kaynaklaniyordu.
      if(!bannerIslemSuruyor){
        ban.innerHTML = html;
      }
      ban.style.display='block';
    } else {
      ban.style.display='none';
    }
  }
  // WiFi ust bar - ESP8266'daki gibi bagli ag + IP
  const wt=$('#wifi-topbar');
  if(wt){
    if(d.wifi_baglibi && d.wifi_ssid){ wt.textContent='WiFi: '+d.wifi_ssid+' ('+d.wifi_ip+')'; }
    else if(d.wifi_ssid){ wt.textContent='WiFi: '+d.wifi_ssid+' (bağlanamadı, AP: '+(d.ap_ip||'-')+')'; }
    else { wt.textContent='WiFi: tanımsız, AP: '+(d.ap_ip||'-'); }
  }
  const wdk=$('#wifi-durum-kutu');
  if(wdk){
    if(d.wifi_ssid){
      wdk.innerHTML='Ağ: <b>'+d.wifi_ssid+'</b><br>Durum: <b>'+(d.wifi_baglibi?('Bağlı ('+d.wifi_ip+')'):'Bağlı değil')+'</b>';
    } else {
      wdk.innerHTML='Ev WiFi tanımlı değil (AP: '+(d.ap_ip||'-')+')';
    }
  }
  // Cihaz durumları
  const esp8266Ok = d.esp8266_online !== false;
  const nanoOk    = d.nano_online !== false;
  const espBadge=$('#esp8266-badge'), nanoBadge=$('#nano-badge');
  espBadge.textContent='ESP8266: '+(esp8266Ok?'OK':'Offline');
  espBadge.style.background=esp8266Ok?'rgba(16,185,129,.15)':'rgba(239,68,68,.15)';
  espBadge.style.color=esp8266Ok?'var(--accent)':'var(--danger)';
  nanoBadge.textContent='Nano: '+(nanoOk?'OK':'Offline');
  nanoBadge.style.background=nanoOk?'rgba(16,185,129,.15)':'rgba(239,68,68,.15)';
  nanoBadge.style.color=nanoOk?'var(--accent)':'var(--danger)';
  // Cihaz Durumu - genel sorun uyarilari (RTC, sensor, RS485 baglanti)
  const hb=$('#hata-box');
  if(hb){
    const sorunlar=[];
    if(!esp8266Ok) sorunlar.push('ESP8266 (Sudepo) bağlantısı yok');
    if(!nanoOk) sorunlar.push('Nano bağlantısı yok');
    if(d.rtc_ok===false) sorunlar.push('RTC (tarih/saat) geçersiz - ESP8266\'daki DS1307 zamanı kaybetmiş, Sudepo sayfasından zamanı yeniden ayarlayın');
    if(d.sensor_err) sorunlar.push('Mesafe sensörü (HC-SR04) hata veriyor');
    hb.innerHTML = sorunlar.length ? sorunlar.map(s=>'⚠️ '+s).join('<br>') : '';
  }
  // Konteyner Zonu PIR - durum + ayar alanlari (odaklıyken üzerine yazma)
  const kzp=$('#kz-pir'); if(kzp) kzp.textContent = kz.pir?'Var':'Yok';
  const kzk=$('#kz-kapi'); if(kzk) kzk.textContent = kz.kapi_acik?'Açık':'Kapalı';
  const kza=$('#kz-alarm'); if(kza){ const kzAktif=kz.pir_alarm||kz.kapi_acik; kza.textContent = kz.pending?'ONAY BEKLİYOR':(kzAktif?'AKTİF':'Pasif'); kza.style.color = kzAktif?'var(--danger)':''; }
  const kzs=$('#kz-siren'); if(kzs){ kzs.textContent = kz.siren?'AKTİF':'Pasif'; kzs.style.color = kz.siren?'var(--danger)':''; }
  const kzl=$('#kz-lamba'); if(kzl){ kzl.textContent = kz.lamba?'AKTİF':'Pasif'; kzl.style.color = kz.lamba?'var(--danger)':''; }
  const kzpt=$('#kz_pirTutma'); if(kzpt && !kzpt.matches(':focus') && !yakinKorumali('kz_pirTutma') && kz.pir_tutma!=null) kzpt.value=kz.pir_tutma;
  const kzpo=$('#kz_pirOnay'); if(kzpo && !kzpo.matches(':focus') && !yakinKorumali('kz_pirOnay') && kz.pir_onay!=null) kzpo.value=kz.pir_onay;
  // Nano IO - dashboard
  $('#d1').textContent=d.nano.door1?'Açık':'Kapalı';
  $('#d2').textContent=d.nano.door2?'Açık':'Kapalı';
  $('#rl').textContent=d.nano.relay?'AKTİF':'Pasif';
  $('#lm').textContent=d.nano.lamp?'Açık':'Kapalı';
  // Nem röle (RS485 Cihaz Durumu)
  const mr=$('#mr'); if(mr) mr.textContent=(d.moisture&&d.moisture.output)?'Açık':'Kapalı';
  // === BUTON METİNLERİ - SUNUCUDAN GELIR, local state YOK ===
  // Her buton sadece KENDI komutu surerken (busySet'te ise) atlanir; digerleri
  // her zaman taze veriyle guncellenir (bkz. busySet aciklamasi yukarida).
  if(!busySet.has('#lamba-btn')) $('#lamba-btn').textContent = d.nano.lamp ? 'Kapat' : 'Aç';
  if(!busySet.has('#alarm-btn')) $('#alarm-btn').textContent = 'Sudepo Zonu: ' + ((d.alarm&&d.alarm.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç');
  if(!busySet.has('#konteyner-alarm-btn')){ const kab=$('#konteyner-alarm-btn'); if(kab) kab.textContent = 'Konteyner Zonu: ' + ((d.konteyner&&d.konteyner.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç'); }
  if(!busySet.has('#panic-btn')){ const pb=$('#panic-btn'); if(pb) pb.textContent = (d.alarm&&d.alarm.panic) ? 'Panik Açık' : 'Panik'; }
  if(!busySet.has('#alarm-mod-sel')){ const ams=$('#alarm-mod-sel'); if(ams && d.alarm && d.alarm.mode) ams.value=String(d.alarm.mode); }
  if(!busySet.has('#alarm-mute-btn')){ const amb=$('#alarm-mute-btn'); if(amb) amb.textContent = (d.alarm&&d.alarm.muted) ? 'Susturma Kaldir' : 'Sustur/Sireni Kapat'; }
  if(!busySet.has('#moisture-settings-toggle-btn')){ const msb=$('#moisture-settings-toggle-btn'); if(msb) msb.textContent = mo.output ? 'Kapat' : 'Aç'; }
  if(!busySet.has('#moisture-settings-auto-btn')){ const sab=$('#moisture-settings-auto-btn'); if(sab) sab.textContent = mo.auto ? 'Manuel' : 'Otomatik'; }
  if(typeof d.telegram_aktif==='boolean'){ telegramAktifBilinen=d.telegram_aktif; const tb=$('#telegram-ac-kapa-btn'); if(tb) tb.textContent=telegramAktifBilinen?'🔔 Bildirimler Açık':'🔕 Bildirimler Kapalı'; }
  // Nem verileri
  const mkpi=$('#kpi-moisture'); if(mkpi) mkpi.textContent=(mo.percent||0).toFixed(1)+'%';
  const mraw=$('#moisture-raw'); if(mraw) mraw.textContent=mo.raw||0;
  const mout=$('#moisture-output'); if(mout) mout.textContent=mo.output?'Açık':'Kapalı';
  const mmod=$('#moisture-mode'); if(mmod) mmod.textContent=mo.auto?'Otomatik':'Manuel';
  // Ayarlar sekmesi nem göstergeleri
  const smv=$('#settings-moisture-val'); if(smv) smv.textContent=(mo.percent||0).toFixed(1)+'%';
  const smo=$('#settings-moisture-out'); if(smo) smo.textContent=mo.output?'Açık':'Kapalı';
  const smm=$('#settings-moisture-mod'); if(smm) smm.textContent=mo.auto?'Otomatik':'Manuel';
  const sml=$('#moisture-settings-low'); if(sml&&!sml.matches(':focus')&&!yakinKorumali('moisture-settings-low')) sml.value=mo.low||0;
  const smh=$('#moisture-settings-high'); if(smh&&!smh.matches(':focus')&&!yakinKorumali('moisture-settings-high')) smh.value=mo.high||0;
  // Bilgiler sekmesi - sistem bilgileri
  const bi=$('#bilgi-sistem');
  if(bi){
    bi.innerHTML='<p>ESP32 IP: <b>'+(d.esp32_ip||'-')+'</b> ('+(d.esp32_mode||'-')+')</p><p>AP SSID: <b>'+(d.ap_ssid||'-')+'</b></p><p>mDNS: <b>'+(d.mdns||'-')+'</b></p><p>ESP8266: <b>'+(d.esp8266_online?'OK':'Offline')+'</b> ('+(d.esp8266_last_sec!=null?d.esp8266_last_sec+'sn önce':'-')+')</p><p>Nano: <b>'+(d.nano_online?'OK':'Offline')+'</b></p><p>Depo: <b>'+(d.level_percent||0).toFixed(1)+'%</b> ('+(d.level_liters||0).toFixed(0)+' L)</p><p>Nem: <b>'+(mo.percent||0).toFixed(1)+'%</b></p><p>Kalibrasyon: Bos '+(d.bosMesafe||0)+' cm / Dolu '+(d.doluMesafe||0)+' cm</p><p>Kapasite: <b>'+(d.kapasite||0)+' L</b> | Alarm: %'+(d.alarmYuzde||0)+'</p>';
  }
  const bfi=$('#build-info');
  if(bfi) bfi.textContent='Firmware derleme tarihi: '+(d.build_date||'-')+' | Çalışma süresi: '+fmtSure(d.uptime_sec||0);
  const gb=$('#guncelleme-bilgi');
  if(gb){
    gb.innerHTML='<p>RS485 poll (ESP32↔ESP8266): <b>'+(d.rs485_interval_ms||'-')+' ms</b></p><p>Son ESP8266 verisi: <b>'+(d.esp8266_last_sec!=null?d.esp8266_last_sec+' sn önce':'-')+'</b></p><p>Tarayıcı bağlantısı: <b>'+(_es?'SSE (anlık)':'Polling (5sn)')+'</b></p>';
  }
  const agap=$('#ag-ap-bilgi'); if(agap) agap.textContent=d.ap_ssid||'-';
}
function fmtSure(sn){
  sn=Math.floor(sn);
  const g=Math.floor(sn/86400), sa=Math.floor((sn%86400)/3600), dk=Math.floor((sn%3600)/60);
  if(g>0) return g+'g '+sa+'sa';
  if(sa>0) return sa+'sa '+dk+'dk';
  return dk+'dk';
}

function guncelle(){
  fetch('/api/status')
    .then(r=>r.json())
    .then(d=>renderUI(d))
    .catch(()=>{});
}

// SSE: RS485'ten yeni veri gelir gelmez (ya da en gec 1sn'de bir) sunucu
// push eder. setInterval polling'e kiyasla gecikmeyi buyuk olcude azaltir.
// Baglanti koparsa 3sn sonra otomatik yeniden dener; guncelle() 5sn'de bir
// yedek olarak calismaya devam eder (SSE tamamen kesilirse bile ekran donmaz).
let _es = null;
function connectSSE(){
  if(_es) return;
  _es = new EventSource('/events');
  _es.onmessage = function(e){
    try{ renderUI(JSON.parse(e.data)); }catch(err){}
  };
  _es.onerror = function(){
    if(_es){ _es.close(); _es=null; }
    setTimeout(connectSSE, 3000);
  };
}

function api(p){
  return fetch(p)
    .then(r=>r.ok?r.json():r.json().catch(()=>({basarili:false,mesaj:'HTTP '+r.status})))
    .catch(()=>({basarili:false,mesaj:'Bağlantı hatası'}));
}

// sendCommand: sadece KENDI butonunu kilitler (busySet), digerleri serbest
// kalir. RS485 komutunun kendisi (retry dahil) ESP32 tarafinda degismedi -
// guvenilirlik icin orada birakildi, burada sadece UI'nin gereksiz yere
// tum butonlari kilitlemesi kaldirildi.
function sendCommand(btnId, path, resultId, label){
  if(btnId && busySet.has(btnId)) return;
  if(btnId) busySet.add(btnId);
  const b = btnId ? $(btnId) : null;
  if(b){ b.textContent='...'; b.disabled=true; }
  api(path).then(d=>{
    if(resultId){ const el=$(resultId); if(el) el.textContent=d.mesaj||''; }
  }).finally(()=>{
    // Sunucudan taze veri çek — buton metnini renderUI yazar
    fetch('/api/status')
      .then(r=>r.json())
      .then(d=>{ renderUI(d); })
      .catch(()=>{})
      .finally(()=>{
        if(btnId){ busySet.delete(btnId); if(b) b.disabled=false; }
        // Sonucu birkaç saniye göster sonra temizle
        if(resultId) setTimeout(()=>{ const el=$(resultId); if(el) el.textContent=''; }, 4000);
      });
  });
}

function toggleLamba(){
  // Mevcut durumu buton metninden değil, sunucu state'inden bil
  // Butonda 'Kapat' yazıyorsa lamba açık demek → hedef=0
  const acik = $('#lamba-btn').textContent.trim() === 'Kapat';
  sendCommand('#lamba-btn', '/api/lamba?durum='+(acik?0:1), '#lamba-sonuc');
}
function toggleMoisture(){
  const acik = $('#moisture-settings-toggle-btn').textContent.trim() === 'Kapat';
  sendCommand('#moisture-settings-toggle-btn', '/api/moisture?durum='+(acik?0:1), '#moisture-settings-msg');
}
function toggleMoistureAuto(){
  const manuel = $('#moisture-settings-auto-btn').textContent.trim() === 'Manuel';
  sendCommand('#moisture-settings-auto-btn', '/api/moisture/auto?aktif='+(manuel?0:1), '#moisture-settings-msg');
}
function setMoistureThresholds(){
  yakinDuzenlendi('moisture-settings-low'); yakinDuzenlendi('moisture-settings-high');
  const low=parseInt($('#moisture-settings-low').value)||0;
  const high=parseInt($('#moisture-settings-high').value)||0;
  if(low<0||low>100||high<0||high>100||low>=high){
    $('#moisture-settings-msg').textContent='0-100 arası ve alt < üst olmalı'; return;
  }
  sendCommand(null,'/api/moisture/threshold?low='+low+'&high='+high,'#moisture-settings-msg');
}
function toggleAlarm(){
  const aktif = $('#alarm-btn').textContent.trim().endsWith('Alarmı Kapat');
  sendCommand('#alarm-btn', '/api/alarm?aktif='+(aktif?0:1), '#alarm-sonuc');
}
function toggleKonteynerAlarm(){
  const aktif = $('#konteyner-alarm-btn').textContent.trim().endsWith('Alarmı Kapat');
  sendCommand('#konteyner-alarm-btn', '/api/konteyner/alarm?aktif='+(aktif?0:1), '#konteyner-alarm-sonuc');
}
function kapiKontrol(v){
  sendCommand(null, '/api/kapi?durum='+v, '#lamba-sonuc');
}
function togglePanic(){
  sendCommand('#panic-btn', '/api/panic', '#panic-sonuc');
}
function setAlarmMod(){
  const m = $('#alarm-mod-sel').value;
  sendCommand(null, '/api/alarm/mod?mod='+m, '#alarm-sonuc');
}
function alarmMute(){
  sendCommand('#alarm-mute-btn', '/api/alarm/mute', '#alarm-sonuc');
}
// Banner butonlari icin - bkz bannerIslemSuruyor aciklamasi (guncelle icinde).
// Tiklanan elemani DOGRUDAN (this ile) alip aninda 'İşleniyor...' gosterir -
// diger sendCommand() kullanan butonlardan farkli olarak sabit bir #id'ye
// degil, o an DOM'da bulunan gercek butona yaziyor.
function bannerAksiyon(btn, path){
  if(bannerIslemSuruyor) return;
  bannerIslemSuruyor = true;
  if(btn){ btn.disabled = true; btn.textContent = 'İşleniyor...'; }
  api(path).finally(()=>{
    bannerIslemSuruyor = false;
    fetch('/api/status').then(r=>r.json()).then(renderUI).catch(()=>{});
  });
}
function otaGuncelle(){
  if(!confirm('GitHub\'daki en son firmware indirilip yazılacak, cihaz yeniden başlayacak. Emin misin?'))return;
  $('#ota-sonuc').textContent='Güncelleniyor...';
  api('/api/ota').then(d=>{$('#ota-sonuc').textContent=d.mesaj||'';});
}
function firmwareDurumYukle(){
  fetch('/api/firmware/durum').then(r=>r.json()).then(d=>{
    $('#fw-durum-kutu').innerHTML = d.varMi
      ? ('Yuklu: <b>'+(d.boyut/1024).toFixed(0)+' KB</b> (yukleme: '+d.yuklemeZamani+')<br>URL: <code>'+d.url+'</code>')
      : 'Henuz firmware yuklenmedi';
  }).catch(()=>{});
}
function firmwareYukle(){
  const f=$('#fwDosya').files[0];
  if(!f){$('#fw-sonuc').textContent='Dosya secin';return;}
  $('#fw-sonuc').textContent='Yukleniyor...';
  const fd=new FormData(); fd.append('firmware',f);
  fetch('/firmware/upload',{method:'POST',body:fd})
    .then(r=>r.json()).then(d=>{$('#fw-sonuc').textContent='Yuklendi'; firmwareDurumYukle();})
    .catch(()=>{$('#fw-sonuc').textContent='Hata!';});
}
function yedekDurumYukle(){
  fetch('/api/kayit/yedek_durum').then(r=>r.json()).then(d=>{
    $('#yedek-durum-kutu').textContent = d.varMi ? ('Yedek dosyasi: '+d.dosya+' - Son yedekleme: '+d.sonYedek) : 'Henuz yedek alinmadi';
  }).catch(()=>{});
}
function kayitYedekle(){
  $('#yedek-sonuc').textContent='Yedekleniyor...';
  api('/api/kayit/yedekle').then(d=>{$('#yedek-sonuc').textContent=d.mesaj||''; yedekDurumYukle();});
}
function kayitGeriYukle(){
  if(!confirm('ESP8266\'nin mevcut kayitlar.csv dosyasi, buradaki yedekle degistirilecek. Emin misin?'))return;
  $('#yedek-sonuc').textContent='Geri yukleniyor...';
  api('/api/kayit/geri_yukle').then(d=>{$('#yedek-sonuc').textContent=d.mesaj||'';});
}
function weatherYukleUI(){
  const kutu=$('#weather-haftalik'); if(!kutu) return;
  fetch('/api/weather').then(r=>r.json()).then(d=>{
    const wdk=$('#weather-durum-kutu');
    if(wdk){
      wdk.innerHTML = d.sayi>0
        ? ('Son çekim: <b>'+(d.tarih||'-')+'</b> ('+(d.guncel?'güncel':'ESKİ - dikkate alınmıyor')+')<br>Yarın yağmur: <b>'+(d.oneri?'Evet, sulama atlanacak':'Hayır')+'</b><br><span style="font-size:11px;color:var(--muted)">Durum: '+(d.durum||'-')+'</span>')
        : ('Henüz tahmin çekilmedi<br><span style="font-size:11px;color:var(--muted)">Durum: '+(d.durum||'-')+'</span>');
    }
    const gunler=['Paz','Pzt','Sal','Çar','Per','Cum','Cmt'];
    const liste=d.haftalik||[];
    if(liste.length===0){kutu.innerHTML='';return;}
    kutu.innerHTML=liste.map((g,i)=>{
      const tarih=new Date(g.tarih+'T12:00:00');
      const gunAdi=i===0?'Bugün':(i===1?'Yarın':gunler[tarih.getDay()]);
      const yagmurVar=g.mm>=1.0;
      const stil=yagmurVar?'background:rgba(37,99,235,.15);border-color:var(--primary)':'';
      return '<div style="padding:6px 10px;border:1px solid var(--border);border-radius:8px;font-size:12px;text-align:center;'+stil+'">'+gunAdi+'<br><b>'+g.mm.toFixed(1)+'mm</b>'+(yagmurVar?' 🌧':'')+'</div>';
    }).join('');
  }).catch(()=>{});
}
function weatherKontrolEt(){
  $('#weather-sonuc').textContent='Kontrol ediliyor...';
  api('/api/weather/check').then(d=>{$('#weather-sonuc').textContent=d.mesaj||'';weatherYukleUI();});
}
function telegramTest(){
  $('#telegram-sonuc').textContent='Gönderiliyor...';
  api('/api/telegram/test').then(d=>{$('#telegram-sonuc').textContent=d.mesaj||'';});
}
let telegramAktifBilinen=true;
function telegramAcKapa(){
  api('/api/telegram/ayar?aktif='+(telegramAktifBilinen?0:1)).then(d=>{
    telegramAktifBilinen=d.aktif;
    $('#telegram-ac-kapa-btn').textContent=telegramAktifBilinen?'🔔 Bildirimler Açık':'🔕 Bildirimler Kapalı';
  });
}

function konteynerPirKaydet(){
  yakinDuzenlendi('kz_pirTutma'); yakinDuzenlendi('kz_pirOnay');
  const tutma=$('#kz_pirTutma').value;
  const onay=$('#kz_pirOnay').value;
  api('/api/konteyner/pir_ayar?tutma='+encodeURIComponent(tutma)+'&onay='+encodeURIComponent(onay)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

// === SUDEPO ZONU (ESP8266+Nano) AYARLARI - Kalburum'dan koprulu yonetim ===
const szTetikleyiciler=[['kapi1','Sol Kapı'],['kapi2','Sağ Kapı'],['pir','PIR'],['seviye','Su Seviyesi'],['kacak','Kaçak'],['sensor','Sensör Hatası']];
function szGridHtml(prefix){
  return szTetikleyiciler.map(t=>'<label><input type="checkbox" id="'+prefix+'_'+t[0]+'" onchange="szKaydet()">'+t[1]+'</label>').join('');
}
function szOutputGridHtml(prefix){
  return '<label><input type="checkbox" id="'+prefix+'_siren" onchange="szKaydet()">Siren</label><label><input type="checkbox" id="'+prefix+'_lamba" onchange="szKaydet()">Lamba</label>';
}
function szCalcTrigger(prefix){
  let v=0;
  szTetikleyiciler.forEach((t,i)=>{ if($('#'+prefix+'_'+t[0]).checked) v|=(1<<i); });
  return v;
}
function szCalcOutput(prefix){
  let v=0;
  if($('#'+prefix+'_siren').checked) v|=1;
  if($('#'+prefix+'_lamba').checked) v|=2;
  return v;
}
function szUncheckAll(prefix, adlar){ adlar.forEach(a=>{ const el=$('#'+prefix+'_'+a); if(el) el.checked=false; }); }
function szSetTrigger(prefix, mask){
  szTetikleyiciler.forEach((t,i)=>{ const el=$('#'+prefix+'_'+t[0]); if(el) el.checked=((mask&(1<<i))!==0); });
}
function szSetOutput(prefix, mask){
  const s=$('#'+prefix+'_siren'), l=$('#'+prefix+'_lamba');
  if(s) s.checked=((mask&1)!==0);
  if(l) l.checked=((mask&2)!==0);
}
function szAyarlarYukle(){
  $('#sz-grid-gunduz').innerHTML=szGridHtml('sz_Gunduz');
  $('#sz-grid-gece').innerHTML=szGridHtml('sz_Gece');
  $('#sz-grid-sesli-girdi').innerHTML=szGridHtml('sz_Sesli');
  $('#sz-grid-sesli-cikis').innerHTML=szOutputGridHtml('sz_Sesli');
  $('#sz-grid-sessiz-girdi').innerHTML=szGridHtml('sz_Sessiz');
  $('#sz-grid-sessiz-cikis').innerHTML=szOutputGridHtml('sz_Sessiz');
  $('#sz-grid-onayli-girdi').innerHTML=szGridHtml('sz_Onayli');
  fetch('/api/sudepo_ayarlar').then(r=>r.json()).then(d=>{
    if(!d.basarili){ $('#sz-yukleniyor').textContent='ESP8266\'dan yanıt alınamadı - bağlı olduğundan emin olun.'; return; }
    $('#sz-yukleniyor').style.display='none';
    $('#sz-form').style.display='block';
    const set=(id,v)=>{ const el=$('#'+id); if(el) el.value=v; };
    set('sz_bosMesafe',d.bosMesafe); set('sz_doluMesafe',d.doluMesafe); set('sz_kapasite',d.kapasite); set('sz_depoYatay',d.depoYatay);
    set('sz_alarmYuzde',d.alarmYuzde); set('sz_geceBaslangic',d.geceBaslangic); set('sz_geceBitis',d.geceBitis);
    set('sz_pirPencereSaniye',d.pirPencereSaniye); set('sz_pirMinTetiklenme',d.pirMinTetiklenme);
    set('sz_minDolumLitre',d.minDolumLitre); set('sz_kacakEsikDakika',d.kacakEsikDakika);
    const modEl=$('#sz_mod'+Math.round(d.alarmMod)); if(modEl) modEl.checked=true;
    szSetTrigger('sz_Gunduz', d.triggerGunduz); szSetTrigger('sz_Gece', d.triggerGece);
    szSetTrigger('sz_Sesli', d.alarmMaskSesli); szSetOutput('sz_Sesli', d.alarmOutputSesli);
    szSetTrigger('sz_Sessiz', d.alarmMaskSessiz); szSetOutput('sz_Sessiz', d.alarmOutputSessiz);
    szSetTrigger('sz_Onayli', d.alarmMaskOnayli);
  }).catch(()=>{ $('#sz-yukleniyor').textContent='ESP8266\'dan yanıt alınamadı.'; });
}
function szKaydet(){
  $('#sz-sonuc').textContent='Kaydediliyor...';
  const modSecili=document.querySelector('input[name=sz_alarmMod]:checked');
  const q=new URLSearchParams({
    bosMesafe:$('#sz_bosMesafe').value, doluMesafe:$('#sz_doluMesafe').value, kapasite:$('#sz_kapasite').value, depoYatay:$('#sz_depoYatay').value,
    alarmYuzde:$('#sz_alarmYuzde').value, geceBaslangic:$('#sz_geceBaslangic').value, geceBitis:$('#sz_geceBitis').value,
    pirPencereSaniye:$('#sz_pirPencereSaniye').value, pirMinTetiklenme:$('#sz_pirMinTetiklenme').value,
    minDolumLitre:$('#sz_minDolumLitre').value, kacakEsikDakika:$('#sz_kacakEsikDakika').value,
    alarmMod: modSecili?modSecili.value:1,
    triggerGunduz:szCalcTrigger('sz_Gunduz'), triggerGece:szCalcTrigger('sz_Gece'),
    alarmMaskSesli:szCalcTrigger('sz_Sesli'), alarmOutputSesli:szCalcOutput('sz_Sesli'),
    alarmMaskSessiz:szCalcTrigger('sz_Sessiz'), alarmOutputSessiz:szCalcOutput('sz_Sessiz'),
    alarmMaskOnayli:szCalcTrigger('sz_Onayli')
  });
  fetch('/api/sudepo_ayarlar/kaydet?'+q.toString()).then(r=>r.json()).then(d=>{
    $('#sz-sonuc').textContent=d.basarili?'Kaydedildi ✓':(d.mesaj||'Hata');
  }).catch(()=>{ $('#sz-sonuc').textContent='Hata'; });
}
szAyarlarYukle();
function otaDosyaOnay(){
  const f=$('#otaDosya').files[0];
  if(!f){$('#ota-dosya-sonuc').textContent='Dosya secin';return false;}
  if(!confirm(f.name+' yuklenecek ve cihaz yeniden baslayacak. Emin misin?'))return false;
  $('#ota-dosya-sonuc').textContent='Yukleniyor... (bitince cihaz yeniden baslar)';
  return true;
}
function wifiKaydet(){
  const sel=$('#staSSIDSel').value;
  const s=$('#staSSID').value || sel;
  if(!s){$('#wifi-sonuc').textContent='SSID gerekli';return;}
  api('/api/wifi?ssid='+encodeURIComponent(s)+'&sifre='+encodeURIComponent($('#staPASS').value))
    .then(d=>{$('#wifi-sonuc').textContent=d.mesaj||''; setTimeout(guncelle,4000);});
}
function wifiKaldir(){
  if(!confirm('Kayıtlı ağ kaldırılsın mı?')) return;
  api('/api/wifi?ssid=').then(d=>{
    $('#wifi-sonuc').textContent=d.mesaj||'';
    $('#staSSID').value=''; $('#staPASS').value='';
    guncelle();
  });
}
function wifiScan(){
  $('#wifi-sonuc').textContent='Taranıyor...';
  api('/api/wifi/scan').then(list=>{
    const sel=$('#staSSIDSel');
    sel.innerHTML='<option value="">Ağ seçin...</option>';
    (Array.isArray(list)?list:[]).forEach(n=>{
      const o=document.createElement('option');
      o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+'dBm'+(n.secured?', kilitli':'')+')';
      sel.appendChild(o);
    });
    $('#wifi-sonuc').textContent=(Array.isArray(list)?list.length:0)+' ağ bulundu';
  });
}
function restartSistem(){
  if(confirm('Yeniden başlatılsın mı?'))
    api('/api/restart').then(()=>{$('#wifi-sonuc').textContent='Yeniden başlatılıyor...';}).catch(()=>{});
}
connectSSE();
setInterval(guncelle, 5000); guncelle();
yedekDurumYukle();
setInterval(weatherYukleUI, 5*60*1000); weatherYukleUI();

// === IR KUMANDA - OGRENME/ESLESTIRME ===
const irKomutAdlari={LAMBA_TOGGLE:'Lamba Aç/Kapat (tek tuş)',LAMBA_AC:'Lamba Aç',LAMBA_KAPAT:'Lamba Kapat',ALARM_TOGGLE:'Alarm Aç/Kapat (tek tuş)',ALARM_AC:'Alarm Aç',ALARM_KAPAT:'Alarm Kapat','ALARM_MOD=1':'Mod: Sesli','ALARM_MOD=2':'Mod: Sessiz','ALARM_MOD=3':'Mod: Onaylı',ALARM_SUSTUR:'Sustur',ALARM_ONAYLA:'Onayla',KAPI_TOGGLE:'Kapı Aç/Kapat (tek tuş)',KAPI_AC:'Kapı Aç',KAPI_KAPAT:'Kapı Kapat',PANIK:'Panik'};
// Kumanda Haritası'ndaki tuşlar icin onerilen (sadece on-dolgu, degistirilebilir) komut
const irOnerilenKomut={'POWER':'ALARM_TOGGLE','MUTE':'ALARM_SUSTUR','AV/TV':'PANIK','CH+':'LAMBA_AC','CH-':'LAMBA_KAPAT','VOL+':'KAPI_AC','VOL-':'KAPI_KAPAT','MENU':'KAPI_TOGGLE','1':'ALARM_MOD=1','2':'ALARM_MOD=2','3':'ALARM_MOD=3','0':'ALARM_ONAYLA','AUTO':'LAMBA_TOGGLE'};
let irOgrenPolling=null;
let irSeciliTusAdi=null; // Kumanda Haritasi'ndan secilen tus (haritasiz ogrenmede null kalir)
function irListesiYukle(){
  fetch('/api/ir/liste').then(r=>r.json()).then(list=>{
    const el=$('#ir-liste'); if(!el) return;
    if(!Array.isArray(list)||!list.length){ el.innerHTML='<p class="muted">Henüz tanımlı tuş yok.</p>'; }
    else {
      el.innerHTML=list.map(e=>
        '<div class="row" style="justify-content:space-between;align-items:center;padding:4px 0;border-bottom:1px solid var(--input-border)">'
        +'<span>'+(e.etiket||e.komut)+' <span class="muted" style="font-size:11px">(0x'+e.kod+')</span></span>'
        +'<button class="btn-sil" onclick="irSil(\''+e.kod+'\')">🗑</button></div>'
      ).join('');
    }
    // Kumanda Haritasi'nda atanmis tuslari isaretle - etiket "Komut — TUS" formatinda
    // kaydedildiginde (bkz irKodAtamaFormuGoster/irKaydet) son parca tus adiyla eslesir.
    document.querySelectorAll('.remote-key[data-key]').forEach(b=>{ b.classList.remove('assigned'); b.title=''; });
    (Array.isArray(list)?list:[]).forEach(e=>{
      const parts=(e.etiket||'').split(' — ');
      const tus = parts.length>1 ? parts[parts.length-1] : null;
      if(!tus) return;
      const btn=document.querySelector('.remote-key[data-key="'+tus+'"]');
      if(btn){ btn.classList.add('assigned'); btn.title=e.etiket; }
    });
  }).catch(()=>{});
}
function irTusaTikla(tus){
  document.querySelectorAll('.remote-key').forEach(b=>b.classList.remove('active-learning'));
  const btn=document.querySelector('.remote-key[data-key="'+tus+'"]');
  if(btn) btn.classList.add('active-learning');
  irOgrenBaslat(tus);
}
function irOgrenBaslat(tusAdi){
  irSeciliTusAdi = tusAdi || null;
  fetch('/api/ir/ogren_baslat').then(()=>{
    $('#ir-ogren-durum').innerHTML = irSeciliTusAdi
      ? ('Kumandanızda <b>'+irSeciliTusAdi+'</b> tuşuna basın... (20sn içinde)')
      : 'Kumandada bir tuşa basın... (20sn içinde)';
    if(irOgrenPolling) clearInterval(irOgrenPolling);
    irOgrenPolling=setInterval(irOgrenKontrolEt, 800);
  }).catch(()=>{});
}
function irOgrenKontrolEt(){
  fetch('/api/ir/ogren_durum').then(r=>r.json()).then(d=>{
    if(d.hazir){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      irKodAtamaFormuGoster(d.kod);
    } else if(d.zamanAsimi){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      $('#ir-ogren-durum').innerHTML='Zaman aşımı, tuş algılanamadı - tekrar deneyin.';
      document.querySelectorAll('.remote-key').forEach(b=>b.classList.remove('active-learning'));
    }
  }).catch(()=>{});
}
function irKodAtamaFormuGoster(kod){
  let secenekler='';
  for(const k in irKomutAdlari) secenekler+='<option value="'+k+'">'+irKomutAdlari[k]+'</option>';
  $('#ir-ogren-durum').innerHTML='Kod alındı: <b>0x'+kod+'</b><br>'
    +'<select id="ir-komut-sec" style="margin-top:6px">'+secenekler+'</select><br>'
    +'<input id="ir-not" class="input" placeholder="Not: örn. kumandanın kırmızı tuşu (opsiyonel)" style="margin-top:6px;width:100%;max-width:280px" value="'+(irSeciliTusAdi||'')+'">'
    +'<br><button class="btn btn-yesil" onclick="irKaydet(\''+kod+'\')" style="margin-top:6px">Kaydet</button>';
  const onerilen = irSeciliTusAdi && irOnerilenKomut[irSeciliTusAdi];
  if(onerilen){ const sel=$('#ir-komut-sec'); if(sel) sel.value=onerilen; }
}
function irKaydet(kod){
  const sel=$('#ir-komut-sec'); const komut=sel.value; const komutAdi=sel.options[sel.selectedIndex].text;
  const not_=($('#ir-not')?.value||'').trim();
  const etiket=not_?(komutAdi+' — '+not_):komutAdi;
  fetch('/api/ir/kaydet?kod='+kod+'&komut='+encodeURIComponent(komut)+'&etiket='+encodeURIComponent(etiket)).then(()=>{
    $('#ir-ogren-durum').innerHTML='Kaydedildi ✓';
    irSeciliTusAdi=null;
    document.querySelectorAll('.remote-key').forEach(b=>b.classList.remove('active-learning'));
    irListesiYukle();
  }).catch(()=>{});
}
function irSil(kod){
  if(!confirm('Bu tuş eşlemesi silinsin mi?')) return;
  fetch('/api/ir/sil?kod='+kod).then(()=>irListesiYukle()).catch(()=>{});
}
irListesiYukle();
</script>
</body>
</html>
  )html";
  server.send_P(200, "text/html", PAGE_HTML, sizeof(PAGE_HTML) - 1);
}

String durumJson() {
  DynamicJsonDocument doc(1024);

  doc["level_cm"] = sensorData.level_cm;
  doc["level_percent"] = sensorData.level_percent;
  doc["level_liters"] = sensorData.level_liters;
  doc["temperature"] = sensorData.temperature;
  doc["night_mode"] = sensorData.night_mode;
  doc["rtc_ok"] = sensorData.rtc_ok;
  doc["sensor_err"] = sensorData.sensor_err;

  doc["nano"]["door1"] = nanoStatus.door1_open;
  doc["nano"]["door2"] = nanoStatus.door2_open;
  doc["nano"]["relay"] = nanoStatus.relay_active;
  doc["nano"]["lamp"] = nanoStatus.lamp_on;
  
  doc["alarm"]["leak"] = alarmStatus.leak_alarm;
  doc["alarm"]["low_level"] = alarmStatus.low_level_alarm;
  doc["alarm"]["door"] = alarmStatus.door_alarm;
  doc["alarm"]["enabled"] = alarmStatus.enabled;
  doc["alarm"]["panic"] = alarmStatus.panic_mode;
  doc["alarm"]["mode"] = alarmStatus.mode;
  doc["alarm"]["muted"] = alarmStatus.muted;
  doc["alarm"]["pending"] = alarmStatus.pending;
  doc["alarm"]["trigger_mask"] = alarmStatus.trigger_mask;

  doc["konteyner"]["enabled"] = konteynerAlarmEtkin;
  doc["konteyner"]["kapi_acik"] = kapi2Acik;
  doc["konteyner"]["pir"] = pir2HareketVar;
  doc["konteyner"]["pir_alarm"] = konteynerPirEskalasyonOldu; // eskale olmus (GERCEK) alarm
  doc["konteyner"]["pir_tutma"] = konteynerPirTutmaSaniye;
  doc["konteyner"]["pir_onay"] = konteynerPirOnaySaniye;
  doc["konteyner"]["pending"] = konteynerOnayBekleniyor;
  doc["konteyner"]["siren"] = konteynerSirenAktif;
  doc["konteyner"]["lamba"] = konteynerLambaAktif;

  doc["telegram_aktif"] = telegramBildirimAktif;

  doc["moisture"]["raw"] = sensorData.moisture_raw;
  doc["moisture"]["percent"] = sensorData.moisture_percent;
  doc["moisture"]["output"] = sensorData.moisture_output;
  doc["moisture"]["auto"] = sensorData.moisture_auto;
  doc["moisture"]["low"] = sensorData.moisture_low;
  doc["moisture"]["high"] = sensorData.moisture_high;
  
  doc["bosMesafe"] = TANK_EMPTY_CM;
  doc["doluMesafe"] = TANK_FULL_CM;
  doc["kapasite"] = TANK_CAPACITY_LITERS;
  doc["alarmYuzde"] = ALARM_LEVEL_PERCENT;
  
  doc["esp8266_online"] = (millis() - sensorData.last_update_ms) < 10000;
  doc["nano_online"] = (millis() - nanoStatus.last_update_ms) < 10000;
  doc["esp8266_last_sec"] = (millis() - sensorData.last_update_ms) / 1000;
  doc["uptime_sec"] = millis() / 1000;
  doc["build_date"] = String(__DATE__) + " " + String(__TIME__);
  doc["rs485_interval_ms"] = RS485_UPDATE_INTERVAL;
  doc["esp32_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["esp32_mode"] = (WiFi.status() == WL_CONNECTED) ? "STA" : "AP";
  doc["ap_ssid"] = AP_SSID;
  doc["ap_ip"] = WiFi.softAPIP().toString();
  doc["wifi_baglibi"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_ssid"] = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : (savedSSID.length() > 0 ? savedSSID : "");
  doc["wifi_ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  doc["mdns"] = String(MDNS_NAME) + ".local";

  String jsonStr;
  serializeJson(doc, jsonStr);
  return jsonStr;
}

void handleAPI_Status() {
  server.send(200, "application/json", durumJson());
}

// ============ SSE PUSH (ESP8266'daki ile ayni desen) ============
WiFiClient sseClient;
bool sseAktif = false;

void ssePush() {
  if (!sseAktif) return;
  if (!sseClient.connected()) { sseAktif = false; return; }
  String data = "data: " + durumJson() + "\n\n";
  size_t yazildi = sseClient.print(data);
  // Yazma timeout'a takilip yarim kaldiysa (tikanik istemci) baglantiyi
  // kapat - aksi halde her sonraki push'ta ayni 200ms'lik gecikmeyi
  // tekrar tekrar yer, bosuna zaman kaybederiz.
  if (yazildi < data.length()) {
    sseClient.stop();
    sseAktif = false;
  }
}

void handleSSE() {
  if (sseAktif) { sseClient.stop(); sseAktif = false; }
  sseClient = server.client();
  sseClient.setNoDelay(true);
  // KRITIK: WiFiClient.print() varsayilan olarak yazma tikanirsa (istemci
  // yavas tuketiyorsa, sekme arka plandaysa vb.) 5 saniyeye kadar
  // BLOKLAYABILIR - ve bu tek dongulu Arduino kodunda TUM sistemi
  // (RS485 polling, web sunucu, MQTT) durdurur. Kisa timeout ile bu riski
  // ~200ms'e indiriyoruz: tikanik istemci en fazla 200ms gecikme yaratir,
  // sonraki dongude sseAktif=false olup connected() kontrolunde ayiklanir.
  sseClient.setTimeout(200);
  sseClient.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n"
  );
  sseAktif = true;
  ssePush();
}

void handleOTA() {
  // Sabit GitHub linkinden ("son surum") indirir - bkz config.h GITHUB_FIRMWARE_URL.
  // FIX: Eskiden yanit indirme/yazma BASLAMADAN ONCE "Guncelleniyor" diye
  // gonderiliyordu - web arayuzu bu tek (ve hep ayni) mesaji gosterip
  // bekliyordu, gercek sonuc (basarili/hatali, hata nedeni) hicbir zaman
  // kullaniciya ulasmiyordu; buton metni fiilen sonsuza kadar "Guncelleniyor"
  // yazili kaliyordu. Simdi yanit, indirme/yazma tamamlandiktan SONRA ve
  // gercek sonucu tasiyarak gonderiliyor - sadece basarili olursa restart.
  String url = GITHUB_FIRMWARE_URL;

  WiFiClientSecure client;
  client.setInsecure();  // raw.githubusercontent.com sertifika zincirini dogrulamadan kabul et
  HTTPClient http;
  http.setTimeout(15000); // FIX: sinirsiz beklemek yerine agda takilirsa 15sn'de vazgec
  http.begin(client, url);
  int httpCode = http.GET();

  String mesaj;
  bool basarili = false;

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (contentLength > 0) {
      if (Update.begin(contentLength)) {
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        if (written == (size_t)contentLength && Update.end()) {
          basarili = true;
          mesaj = "Basarili - yeniden baslatiliyor";
          DEBUG_PRINTLN("[OTA] Update OK. Rebooting...");
        } else {
          mesaj = "Yazma hatasi (" + String(written) + "/" + String(contentLength) + " bayt)";
          DEBUG_PRINTLN("[OTA] Write failed");
          Update.abort();
        }
      } else {
        mesaj = "Yetersiz flash alani";
        DEBUG_PRINTLN("[OTA] Not enough space");
      }
    } else {
      mesaj = "Icerik boyutu okunamadi (GitHub'daki dosya eksik/erisilemez olabilir)";
    }
  } else {
    mesaj = "HTTP hata: " + String(httpCode) + " (" + http.errorToString(httpCode) + ")";
    DEBUG_PRINTLN("[OTA] HTTP failed: " + String(httpCode) + " " + http.errorToString(httpCode));
  }
  http.end();

  server.send(200, "application/json", "{\"basarili\":" + String(basarili ? "true" : "false") + ",\"mesaj\":\"" + mesaj + "\"}");
  if (basarili) {
    delay(200);
    ESP.restart();
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
    DEBUG_PRINTLN(String("[OTA-FILE] Basliyor: ") + upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      DEBUG_PRINTLN(String("[OTA-FILE] Basarili: ") + String(upload.totalSize) + " byte");
    } else {
      Update.printError(Serial);
    }
  }
}

// ============ ESP8266 FIRMWARE DEPOSU ============
// AMAC: Bahcede sabit internet olmadigindan, esp8266_slave'in "URL'den OTA"
// ozelligi GitHub'a degil buraya (ESP32'nin kendi SPIFFS'i) isaret edebilsin.
// Ikisi ayni WiFi agina (orn. telefon hotspot'u) bagliyken calisir.
#define ESP8266_FIRMWARE_DOSYASI "/esp8266_firmware.bin"
String esp8266FirmwareYuklemeZamani = "-";

void handleFirmwareUpload() {
  server.send(200, "application/json", "{\"basarili\":true}");
}

File esp8266FirmwareYazFile;
void handleFirmwareUploadProgress() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    DEBUG_PRINTLN(String("[FW-DEPO] Basliyor: ") + upload.filename);
    esp8266FirmwareYazFile = SPIFFS.open(ESP8266_FIRMWARE_DOSYASI, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (esp8266FirmwareYazFile) esp8266FirmwareYazFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (esp8266FirmwareYazFile) {
      esp8266FirmwareYazFile.close();
      esp8266FirmwareYuklemeZamani = String(millis() / 1000) + "sn (uptime)";
      DEBUG_PRINTLN(String("[FW-DEPO] Kaydedildi: ") + String(upload.totalSize) + " byte");
    }
  }
}

void handleFirmwareServe() {
  File f = SPIFFS.open(ESP8266_FIRMWARE_DOSYASI, "r");
  if (!f) { server.send(404, "text/plain", "Henuz firmware yuklenmedi"); return; }
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

void handleFirmwareDurum() {
  bool varMi = SPIFFS.exists(ESP8266_FIRMWARE_DOSYASI);
  size_t boyut = 0;
  if (varMi) { File f = SPIFFS.open(ESP8266_FIRMWARE_DOSYASI, "r"); boyut = f.size(); f.close(); }
  server.send(200, "application/json", "{\"varMi\":" + String(varMi ? "true" : "false") + ",\"boyut\":" + String(boyut) +
              ",\"yuklemeZamani\":\"" + esp8266FirmwareYuklemeZamani + "\",\"url\":\"http://" + String(MDNS_NAME) + ".local/firmware/esp8266.bin\"}");
}

// ============ HAVA DURUMU / YAGMUR TAHMINI API'LERI ============
// ============ KAYIT YEDEKLEME (ESP8266'nin kayitlar.csv'si buraya yedeklenir) ============
// AMAC: ESP8266'da "pio run -t uploadfs" tum LittleFS'i sifirlar, kayitlar.csv da
// dahil olmak uzere calisma-zamani dosyalarini siler. Yedek burada (ESP32 SPIFFS)
// tutulur ve istendiginde ESP8266'ya geri gonderilir.
#define KAYIT_BACKUP_DOSYASI "/kayitlar_backup.csv"
String sonYedekZamanStr = "-";

bool esp8266KayitYedekle() {
  String reply;
  bool ok = rs485_send_wait_ack("MASTER:GET_KAYITLAR\n", reply, 1000, 3);
  if (!ok) return false;
  int eq = reply.indexOf("GET_KAYITLAR=");
  if (eq < 0) return false;
  String joined = reply.substring(eq + 13);
  File f = SPIFFS.open(KAYIT_BACKUP_DOSYASI, "w");
  if (!f) return false;
  int start = 0;
  while (start <= (int)joined.length()) {
    int tilde = joined.indexOf('~', start);
    String satir = (tilde >= 0) ? joined.substring(start, tilde) : joined.substring(start);
    if (satir.length() > 0) f.println(satir);
    if (tilde < 0) break;
    start = tilde + 1;
  }
  f.close();
  // Gercek tarih/saat icin ESP8266'nin RTC'sini sor (ESP32'de RTC yok).
  String zamanReply;
  if (rs485_send_wait_ack("MASTER:GET_ZAMAN\n", zamanReply, 1000, 3)) {
    int eqZ = zamanReply.indexOf("GET_ZAMAN=");
    if (eqZ >= 0) sonYedekZamanStr = zamanReply.substring(eqZ + 10);
    else sonYedekZamanStr = String(millis() / 1000) + "sn (uptime)";
  } else {
    sonYedekZamanStr = String(millis() / 1000) + "sn (uptime)";
  }
  return true;
}

// ESP8266'nin RS485 tarafi kucuk SoftwareSerial arabellegi kullaniyor - tum
// yedegi TEK uzun satirda geri gondermek tasma riski tasir. Bunun yerine
// satir satir, her birini ACK ile onaylatarak gonderiyoruz.
String kayitGeriYuklemeHata = "";

bool esp8266KayitGeriYukle() {
  kayitGeriYuklemeHata = "";
  File f = SPIFFS.open(KAYIT_BACKUP_DOSYASI, "r");
  if (!f) { kayitGeriYuklemeHata = "Yedek dosyasi yok"; return false; }
  String reply;
  // FIX: 1000ms/3 deneme ara sira yetersiz kaliyordu (ESP8266 kendi nanoPoll
  // dongusuyle mesgulken RESTORE_SATIR kacabiliyordu) - "yedeklendi" basarili
  // oluyordu (tek istek) ama "geri yukle" (10 ardisik istek) sik basarisiz
  // oluyordu. Sure/deneme arttirildi, satirlar arasina kucuk bir bosluk
  // eklendi ki ESP8266 arada nefes alsin.
  if (!rs485_send_wait_ack("MASTER:RESTORE_BASLA\n", reply, 1500, 5)) {
    f.close(); kayitGeriYuklemeHata = "RESTORE_BASLA yanit vermedi"; return false;
  }
  bool hepsiOk = true;
  int satirNo = 0;
  while (f.available()) {
    String satir = f.readStringUntil('\n'); satir.trim();
    if (satir.length() == 0) continue;
    satirNo++;
    String cmd = "MASTER:RESTORE_SATIR=" + satir + "\n";
    if (!rs485_send_wait_ack(cmd.c_str(), reply, 1500, 5)) {
      hepsiOk = false;
      kayitGeriYuklemeHata = String(satirNo) + ". satirda yanit alinamadi";
      break;
    }
    delay(30);
  }
  f.close();
  if (!rs485_send_wait_ack("MASTER:RESTORE_BITIR\n", reply, 1500, 5)) {
    hepsiOk = false;
    if (kayitGeriYuklemeHata.length() == 0) kayitGeriYuklemeHata = "RESTORE_BITIR yanit vermedi";
  }
  return hepsiOk;
}

void handleAPI_KayitYedekle() {
  rs485_api_busy = true;
  bool ok = esp8266KayitYedekle();
  rs485_api_busy = false;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" +
              String(ok ? "Yedeklendi" : "Yedekleme basarisiz") + "\"}");
}

void handleAPI_KayitGeriYukle() {
  rs485_api_busy = true;
  bool ok = esp8266KayitGeriYukle();
  rs485_api_busy = false;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" +
              String(ok ? "Geri yuklendi" : ("Geri yukleme basarisiz: " + kayitGeriYuklemeHata)) + "\"}");
}

void handleAPI_KayitYedekDurum() {
  bool varMi = SPIFFS.exists(KAYIT_BACKUP_DOSYASI);
  server.send(200, "application/json", "{\"varMi\":" + String(varMi ? "true" : "false") + ",\"dosya\":\"" + String(KAYIT_BACKUP_DOSYASI) + "\",\"sonYedek\":\"" + sonYedekZamanStr + "\"}");
}

// ============ HAVA DURUMU API'LERI ============
void handleAPI_WeatherGet() {
  String j = "{";
  j += "\"tarih\":\"" + weatherFetchTarihStr + "\",";
  j += "\"sayi\":" + String(weatherForecastCount) + ",";
  j += "\"guncel\":" + String(weatherGuncelMi() ? "true" : "false") + ",";
  j += "\"oneri\":" + String(weatherSkipOneri ? "true" : "false") + ",";
  j += "\"durum\":\"" + weatherDurum + "\",";
  j += "\"haftalik\":[";
  for (int i = 0; i < weatherForecastCount; i++) {
    if (i > 0) j += ",";
    j += "{\"tarih\":\"" + weatherForecastDates[i] + "\",\"mm\":" + String(weatherForecastMm[i], 1) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleAPI_WeatherCheck() {
  bool ok = weatherTahminCek();
  bool guncel = weatherGuncelMi();
  weatherSkipOneri = guncel && weatherForecastCount >= 2 && (weatherForecastMm[1] >= WEATHER_RAIN_THRESHOLD_MM);
  String reply;
  rs485_send_wait_ack(weatherSkipOneri ? "MASTER:SET_RAIN_SKIP=1\n" : "MASTER:SET_RAIN_SKIP=0\n", reply, 1000, 3);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + weatherDurum + "\"}");
}

void handleAPI_TelegramTest() {
  bool ok = telegramMesajGonder("🌱 SuDepo: Test mesaji - bildirimler calisiyor.");
  String mesaj;
  if (ok) mesaj = "Gonderildi - Telegram'i kontrol edin";
  else if (WiFi.status() != WL_CONNECTED) mesaj = "WiFi bagli degil";
  else if (String(TELEGRAM_BOT_TOKEN).length() == 0) mesaj = "TELEGRAM_BOT_TOKEN bos (secrets.h)";
  else mesaj = "Gonderilemedi - token/chat ID'yi kontrol edin";
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + mesaj + "\"}");
}

void handleAPI_TelegramAyar() {
  if (server.hasArg("aktif")) {
    telegramAyarKaydet(server.arg("aktif").toInt() != 0);
  }
  server.send(200, "application/json", "{\"basarili\":true,\"aktif\":" + String(telegramBildirimAktif ? "true" : "false") + "}");
}

void handleAPI_KonteynerPirAyar() {
  uint16_t tutma = konteynerPirTutmaSaniye;
  uint16_t onay = konteynerPirOnaySaniye;
  if (server.hasArg("tutma")) {
    tutma = (uint16_t)server.arg("tutma").toInt();
    if (tutma < 1) tutma = 1;
  }
  if (server.hasArg("onay")) {
    onay = (uint16_t)server.arg("onay").toInt();
    if (onay < 1) onay = 1;
  }
  konteynerPirAyarKaydet(tutma, onay);
  server.send(200, "application/json", "{\"basarili\":true,\"tutma\":" + String(konteynerPirTutmaSaniye) + ",\"onay\":" + String(konteynerPirOnaySaniye) + "}");
}

// Konteyner Zonu'nun KENDI alarm ac/kapa anahtari - Sudepo'nun /api/alarm'inin
// aksine RS485 YOK, tamamen yerel/anlik NVS kaydi (bkz konteynerAlarmEtkin).
void handleAPI_KonteynerAlarm() {
  if (!server.hasArg("aktif")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"aktif eksik\"}");
    return;
  }
  bool aktif = server.arg("aktif").toInt() != 0;
  konteynerAlarmAyarKaydet(aktif);
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(aktif ? "Konteyner Alarmi Aktif" : "Konteyner Alarmi Pasif") + "\"}");
}

// ============ RS485 KOMUT API'LERI ============
// FIX: Nano'nun anladığı komut formatına uygun:
//   LAMBA_ON / LAMBA_OFF / RELAY_ON / RELAY_OFF / GET_STATUS
//
// Asagidaki *Ayarla/*Tetikle fonksiyonlari HTTP (handleAPI_*) ve BLE
// (bleKomutIsle, asagida) tarafindan ortak kullanilir - ikisi de ayni RS485
// komutunu gonderip ayni global durumu guncellemeli.
bool lambaAyarla(bool acik, String& reply) {
  rs485_api_busy = true; // poll'u durdur
  bool ok = rs485_send_wait_ack(acik ? "MASTER:SET_LAMBA=1\n" : "MASTER:SET_LAMBA=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) {
    nanoStatus.lamp_on = acik;
    last_rs485_update_ms = millis(); // poll timer'ı sıfırla - hemen tekrar GET_STATUS göndermesin
  }
  return ok;
}

void handleAPI_Lamba() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  bool ok = lambaAyarla(d == 1, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Lamba Acik" : "Lamba Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

bool alarmAyarla(bool aktif, String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(aktif ? "MASTER:SET_ALARM=1\n" : "MASTER:SET_ALARM=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.enabled = aktif; last_rs485_update_ms = millis(); }
  return ok;
}

void handleAPI_Alarm() {
  if (!server.hasArg("aktif")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"aktif eksik\"}");
    return;
  }
  int d = server.arg("aktif").toInt();
  String reply;
  bool ok = alarmAyarla(d == 1, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Alarm Aktif" : "Alarm Pasif") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

bool alarmModAyarla(uint8_t mod, String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack((String("MASTER:SET_ALARM_MOD=") + mod + "\n").c_str(), reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.mode = mod; alarmStatus.muted = false; alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  return ok;
}

void handleAPI_AlarmMod() {
  if (!server.hasArg("mod")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"mod eksik\"}");
    return;
  }
  int m = server.arg("mod").toInt();
  if (m < 1 || m > 3) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"mod 1-3 olmali\"}");
    return;
  }
  String reply;
  bool ok = alarmModAyarla((uint8_t)m, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? ("Mod " + String(m)) : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

bool alarmSustur(String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_MUTE\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.muted = !alarmStatus.muted; last_rs485_update_ms = millis(); }
  return ok;
}

void handleAPI_AlarmMute() {
  String reply;
  bool ok = alarmSustur(reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"muted\":" + String(alarmStatus.muted ? "true" : "false") + ",\"reply\":\"" + reply + "\"}");
}

bool alarmOnayla(String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_ONAYLA\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  // Konteyner'in kendi onayi ESP8266/RS485'ten BAGIMSIZ (yerel bayrak) -
  // ESP8266 cevrimdisi olsa bile Kalburum'un onayi calissin.
  konteynerOnayBekleniyor = false;
  konteynerOnayVerildi = true; // "Sesli" onay - buzzer devreye girer
  return ok;
}

void handleAPI_AlarmOnayla() {
  String reply;
  bool ok = alarmOnayla(reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Onaylandi" : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_AlarmOnaylaLamba() {
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_ONAYLA_LAMBA\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  // Konteyner'in KONTEYNER_LAMBA_PIN uzerinden gercek bir lamba ciktisi var -
  // "Sessiz" secildigi icin buzzer/siren ATILMAZ (konteynerOnayVerildi false
  // kalir), ama lamba tek basina aktif olur.
  konteynerOnayBekleniyor = false;
  konteynerLambaOnayVerildi = true;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Sadece lamba flasoru aktif" : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_MoistureToggle() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_MOISTURE=1\n" : "MASTER:SET_MOISTURE=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { sensorData.moisture_output = (d == 1); last_rs485_update_ms = millis(); }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Nem cikisi Acik" : "Nem cikisi Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_MoistureAuto() {
  if (!server.hasArg("aktif")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"aktif eksik\"}");
    return;
  }
  int d = server.arg("aktif").toInt();
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_MOISTURE_AUTO=1\n" : "MASTER:SET_MOISTURE_AUTO=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { sensorData.moisture_auto = (d == 1); last_rs485_update_ms = millis(); }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Nem otomatik" : "Nem manuel") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_MoistureThreshold() {
  if (!server.hasArg("low") || !server.hasArg("high")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"low/high eksik\"}");
    return;
  }
  int low = server.arg("low").toInt();
  int high = server.arg("high").toInt();
  if (low < 0) low = 0;
  if (high < 0) high = 0;
  if (low > 100) low = 100;
  if (high > 100) high = 100;
  String replyLow;
  String replyHigh;
  String cmdLow = String("MASTER:SET_MOISTURE_LOW=") + low + "\n";
  String cmdHigh = String("MASTER:SET_MOISTURE_HIGH=") + high + "\n";
  rs485_api_busy = true;
  bool okLow = rs485_send_wait_ack(cmdLow.c_str(), replyLow, 1000, 3);
  bool okHigh = rs485_send_wait_ack(cmdHigh.c_str(), replyHigh, 1000, 3);
  rs485_api_busy = false;
  if (okLow && okHigh) {
    sensorData.moisture_low = low;
    sensorData.moisture_high = high;
    last_rs485_update_ms = millis();
  }
  bool ok = okLow && okHigh;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Esikler ayarlandi" : "Komut hatasi") + "\",\"replyLow\":\"" + replyLow + "\",\"replyHigh\":\"" + replyHigh + "\"}");
}

bool kapiAyarla(bool acik, String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(acik ? "MASTER:SET_KAPI=1\n" : "MASTER:SET_KAPI=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) last_rs485_update_ms = millis();
  return ok;
}

void handleAPI_Kapi() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  bool ok = kapiAyarla(d == 1, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Kapi Acik" : "Kapi Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

// ============================================================
// SUDEPO ZONU AYARLARI (ESP8266+Nano) - Kalburum'dan da yonetim
// ============================================================
// Kullanici talebiyle: karar/yurutme hala ESP8266'da (RS485 gecikmesi
// olmadan tepki versin, ayrica bu tarafi "tam test edilmeden" ESP32'ye
// tasimak riskliydi - bkz kullanici onayi). Burada SADECE ekran/kontrol
// Kalburum'a tasiniyor - ESP8266'nin GET_AYARLAR/SET_AYARLAR RS485
// komutlarina koprulenir, ESP8266 hala tek dogru kaynak.
bool sudepoAyarlarGetir(String& veri) {
  rs485_api_busy = true;
  String reply;
  bool ok = rs485_send_wait_ack("MASTER:GET_AYARLAR\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) {
    int idx = reply.indexOf("AYARLAR=");
    veri = (idx >= 0) ? reply.substring(idx + 8) : "";
  }
  return ok;
}

bool sudepoAyarlarKaydet(const String& veri, String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(("MASTER:SET_AYARLAR=" + veri + "\n").c_str(), reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) last_rs485_update_ms = millis();
  return ok;
}

void handleAPI_SudepoAyarlarGetir() {
  String veri;
  bool ok = sudepoAyarlarGetir(veri);
  if (!ok) {
    server.send(200, "application/json", "{\"basarili\":false,\"mesaj\":\"ESP8266'dan yanit yok\"}");
    return;
  }
  // "k=v,k=v,..." -> JSON. Deger tipini tahmin etmeye calismadan hepsini
  // sayi olarak yaziyoruz (JS tarafinda Number()/parseInt() zaten kullanilacak).
  DynamicJsonDocument doc(1024);
  int pos = 0;
  while (pos < (int)veri.length()) {
    int eq = veri.indexOf('=', pos);
    if (eq < 0) break;
    int comma = veri.indexOf(',', eq);
    if (comma < 0) comma = veri.length();
    String key = veri.substring(pos, eq);
    String val = veri.substring(eq + 1, comma);
    doc[key] = val.toFloat();
    pos = comma + 1;
  }
  doc["basarili"] = true;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleAPI_SudepoAyarlarKaydet() {
  // Web formundan gelen ayni parametre adlarini "k=v,k=v" RS485 formatina
  // cevirir - ESP8266'nin kendi /ayarlar/kaydet endpoint'iyle ayni alan adlari.
  const char* alanlar[] = {
    "bosMesafe", "doluMesafe", "kapasite", "alarmYuzde", "geceBaslangic", "geceBitis",
    "minDolumLitre", "kacakEsikDakika", "depoYatay", "moistureAutomatic",
    "moistureThresholdLow", "moistureThresholdHigh", "triggerGunduz", "triggerGece",
    "alarmMod", "alarmMaskSesli", "alarmMaskSessiz", "alarmMaskOnayli",
    "alarmOutputSesli", "alarmOutputSessiz", "pirPencereSaniye", "pirMinTetiklenme"
  };
  String veri;
  for (const char* alan : alanlar) {
    if (server.hasArg(alan)) {
      if (veri.length() > 0) veri += ",";
      veri += String(alan) + "=" + server.arg(alan);
    }
  }
  if (veri.length() == 0) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"ayar yok\"}");
    return;
  }
  String reply;
  bool ok = sudepoAyarlarKaydet(veri, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Kaydedildi" : "Komut hatasi") + "\"}");
}

// ============ PANIK BUTONU (ESP8266 ile senkronize) ============
// ESP8266'daki /role/panic ile aynı toggle davranışı.
// RS485 üzerinden MASTER:PANIC gönderir, ESP8266 toggle yapar ve
// ACK:PANIC=1 veya ACK:PANIC=0 ile yeni durumu döndürür.
bool panikTetikle(bool& panicActive, String& reply) {
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:PANIC\n", reply, 1000, 3);
  rs485_api_busy = false;

  // ACK yanıtından panik durumunu çöz: "ACK:PANIC=1" veya "ACK:PANIC=0"
  panicActive = false;
  if (ok) {
    int eqIdx = reply.indexOf("PANIC=");
    if (eqIdx >= 0) {
      panicActive = (reply.substring(eqIdx + 6).startsWith("1"));
    }
    alarmStatus.panic_mode = panicActive;
    last_rs485_update_ms = millis();
  }
  return ok;
}

void handleAPI_Panic() {
  String reply;
  bool panicActive = false;
  bool ok = panikTetikle(panicActive, reply);

  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") +
    ",\"mesaj\":\"" + String(ok ? (panicActive ? "Panik Acik" : "Panik Kapali") : "Komut hatasi") +
    "\",\"panic\":" + String(panicActive ? "true" : "false") +
    ",\"reply\":\"" + reply + "\"}");
}

// ============================================================
// ORTAK KOMUT SOZLUGU (Kontrol sekmesi/BLE/IR kumanda - hepsi ayni davranir)
// ============================================================
// Yukaridaki *Ayarla/*Tetikle fonksiyonlarini cagirip ayni "LAMBA_AC" tarzi
// komut kelime dagarcigini isler. BLE (bkz asagida) ve IR kumanda (bkz
// konteynerDonanimi bolumu) bu TEK fonksiyonu kullanir - ikisinin birbirinden
// farkli davranma riskini ortadan kaldirir. ENABLE_BLE'dan bagimsiz (IR,
// BLE kapaliyken de calismali).
bool komutCalistir(const String& komut, String& mesaj) {
  String reply;
  bool ok = false;

  if (komut == "LAMBA_AC" || komut == "LAMBA_KAPAT") {
    ok = lambaAyarla(komut == "LAMBA_AC", reply);
    mesaj = ok ? (komut == "LAMBA_AC" ? "LAMBA=1" : "LAMBA=0") : "LAMBA";
  } else if (komut == "LAMBA_TOGGLE") {
    // IR kumandada tek tusla ac/kapa istegi - mevcut duruma bakip tersini yapar.
    bool yeniDurum = !nanoStatus.lamp_on;
    ok = lambaAyarla(yeniDurum, reply);
    mesaj = ok ? ("LAMBA=" + String(yeniDurum ? "1" : "0")) : "LAMBA";
  } else if (komut == "ALARM_AC" || komut == "ALARM_KAPAT") {
    ok = alarmAyarla(komut == "ALARM_AC", reply);
    mesaj = ok ? (komut == "ALARM_AC" ? "ALARM=1" : "ALARM=0") : "ALARM";
  } else if (komut == "ALARM_TOGGLE") {
    bool yeniDurum = !alarmStatus.enabled;
    ok = alarmAyarla(yeniDurum, reply);
    mesaj = ok ? ("ALARM=" + String(yeniDurum ? "1" : "0")) : "ALARM";
  } else if (komut.startsWith("ALARM_MOD=")) {
    int mod = komut.substring(10).toInt();
    if (mod >= 1 && mod <= 3) {
      ok = alarmModAyarla((uint8_t)mod, reply);
      mesaj = ok ? ("MOD=" + String(mod)) : "ALARM_MOD";
    } else {
      mesaj = "ALARM_MOD_GECERSIZ";
    }
  } else if (komut == "ALARM_SUSTUR") {
    ok = alarmSustur(reply);
    mesaj = ok ? ("MUTE=" + String(alarmStatus.muted ? "1" : "0")) : "ALARM_SUSTUR";
  } else if (komut == "ALARM_ONAYLA") {
    ok = alarmOnayla(reply);
    mesaj = ok ? "ONAYLANDI" : "ALARM_ONAYLA";
  } else if (komut == "KAPI_AC" || komut == "KAPI_KAPAT") {
    ok = kapiAyarla(komut == "KAPI_AC", reply);
    mesaj = ok ? (komut == "KAPI_AC" ? "KAPI=1" : "KAPI=0") : "KAPI";
  } else if (komut == "KAPI_TOGGLE") {
    bool yeniDurum = !nanoStatus.relay_active;
    ok = kapiAyarla(yeniDurum, reply);
    mesaj = ok ? ("KAPI=" + String(yeniDurum ? "1" : "0")) : "KAPI";
  } else if (komut == "PANIK") {
    bool panicActive = false;
    ok = panikTetikle(panicActive, reply);
    mesaj = ok ? ("PANIC=" + String(panicActive ? "1" : "0")) : "PANIK";
  } else {
    mesaj = "BILINMEYEN_KOMUT";
  }
  return ok;
}

// ============================================================
// IR KUMANDA ESLESTIRME (kod -> komut, SPIFFS'te kalici, web'den yonetilir)
// ============================================================
// irKumandaIsle() (konteynerDonanimi bolumunde, sadece ham kod yakalar) ile
// buradaki dispatch ayrildi - cunku komutCalistir() ve *Ayarla fonksiyonlari
// dosyada daha asagida tanimli, oysa irKumandaIsle() cok daha erken. loop()
// her ikisini de sirayla cagirir (bkz asagida).
#define IR_MAX_ESLESME 20
#define IR_OGRENME_TIMEOUT_MS 20000UL
struct IrEslesme {
  uint32_t kod = 0;
  String komut = "";
  String etiket = "";
};
IrEslesme irEslesmeler[IR_MAX_ESLESME];
uint8_t irEslesmeSayisi = 0;
const char* IR_MAP_DOSYA = "/ir_map.json";

bool irOgrenmeModu = false;
unsigned long irOgrenmeBaslangicMs = 0;
uint32_t irOgrenmeYakalananKod = 0;
bool irOgrenmeKodHazir = false;

void irEslesmeYukle() {
  File f = SPIFFS.open(IR_MAP_DOSYA, "r");
  if (!f) return;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    irEslesmeSayisi = 0;
    for (JsonObject o : doc.as<JsonArray>()) {
      if (irEslesmeSayisi >= IR_MAX_ESLESME) break;
      irEslesmeler[irEslesmeSayisi].kod = o["kod"] | 0UL;
      irEslesmeler[irEslesmeSayisi].komut = String((const char*)(o["komut"] | ""));
      irEslesmeler[irEslesmeSayisi].etiket = String((const char*)(o["etiket"] | ""));
      irEslesmeSayisi++;
    }
  }
  f.close();
}

void irEslesmeKaydet() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < irEslesmeSayisi; i++) {
    JsonObject o = arr.createNestedObject();
    o["kod"] = irEslesmeler[i].kod;
    o["komut"] = irEslesmeler[i].komut;
    o["etiket"] = irEslesmeler[i].etiket;
  }
  File f = SPIFFS.open(IR_MAP_DOSYA, "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

// Ham IR kodunu (irKumandaIsle'nin yakaladigi) ogrenme modunda kaydeder ya
// da eslesme tablosunda arayip bulursa ayni komutCalistir() ile calistirir -
// web/BLE ile birebir ayni davranir.
void irKomutIsleVeCalistir() {
  if (irOgrenmeModu && millis() - irOgrenmeBaslangicMs > IR_OGRENME_TIMEOUT_MS) {
    irOgrenmeModu = false; // zaman asimi
  }
  if (!irYeniKodVar) return;
  irYeniKodVar = false;
  uint32_t kod = irSonKod;

  if (irOgrenmeModu) {
    irOgrenmeYakalananKod = kod;
    irOgrenmeKodHazir = true;
    irOgrenmeModu = false;
    DEBUG_PRINT("[IR] Ogrenme modunda kod yakalandi: 0x");
    DEBUG_PRINTLN(String(kod, HEX));
    return;
  }

  for (uint8_t i = 0; i < irEslesmeSayisi; i++) {
    if (irEslesmeler[i].kod == kod) {
      String mesaj;
      komutCalistir(irEslesmeler[i].komut, mesaj);
      DEBUG_PRINT("[IR] Komut calistirildi: ");
      DEBUG_PRINTLN(irEslesmeler[i].komut);
      return;
    }
  }
}

void handleAPI_IrListe() {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < irEslesmeSayisi; i++) {
    JsonObject o = arr.createNestedObject();
    o["kod"] = String(irEslesmeler[i].kod, HEX);
    o["komut"] = irEslesmeler[i].komut;
    o["etiket"] = irEslesmeler[i].etiket;
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleAPI_IrOgrenBaslat() {
  irOgrenmeModu = true;
  irOgrenmeKodHazir = false;
  irOgrenmeYakalananKod = 0;
  irOgrenmeBaslangicMs = millis();
  server.send(200, "application/json", "{\"basarili\":true}");
}

void handleAPI_IrOgrenDurum() {
  bool zamanAsimi = (!irOgrenmeModu && !irOgrenmeKodHazir && irOgrenmeBaslangicMs > 0 &&
                      millis() - irOgrenmeBaslangicMs > IR_OGRENME_TIMEOUT_MS);
  String json = "{\"hazir\":" + String(irOgrenmeKodHazir ? "true" : "false") +
    ",\"kod\":\"" + String(irOgrenmeYakalananKod, HEX) + "\"" +
    ",\"zamanAsimi\":" + String(zamanAsimi ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleAPI_IrKaydet() {
  if (!server.hasArg("kod") || !server.hasArg("komut")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"eksik parametre\"}");
    return;
  }
  uint32_t kod = strtoul(server.arg("kod").c_str(), nullptr, 16);
  String komut = server.arg("komut");
  String etiket = server.hasArg("etiket") ? server.arg("etiket") : komut;

  int idx = -1;
  for (uint8_t i = 0; i < irEslesmeSayisi; i++) {
    if (irEslesmeler[i].kod == kod) { idx = i; break; }
  }
  if (idx < 0) {
    if (irEslesmeSayisi >= IR_MAX_ESLESME) {
      server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"Maksimum tus sayisina ulasildi\"}");
      return;
    }
    idx = irEslesmeSayisi++;
  }
  irEslesmeler[idx].kod = kod;
  irEslesmeler[idx].komut = komut;
  irEslesmeler[idx].etiket = etiket;
  irEslesmeKaydet();
  irOgrenmeKodHazir = false;
  server.send(200, "application/json", "{\"basarili\":true}");
}

void handleAPI_IrSil() {
  if (!server.hasArg("kod")) {
    server.send(400, "application/json", "{\"basarili\":false}");
    return;
  }
  uint32_t kod = strtoul(server.arg("kod").c_str(), nullptr, 16);
  int idx = -1;
  for (uint8_t i = 0; i < irEslesmeSayisi; i++) {
    if (irEslesmeler[i].kod == kod) { idx = i; break; }
  }
  if (idx >= 0) {
    for (uint8_t i = (uint8_t)idx; i < irEslesmeSayisi - 1; i++) irEslesmeler[i] = irEslesmeler[i + 1];
    irEslesmeSayisi--;
    irEslesmeKaydet();
  }
  server.send(200, "application/json", "{\"basarili\":true}");
}

// ============================================================
// BLE - TELEFON UYGULAMASI (Kontrol sekmesiyle ayni komutlar)
// ============================================================
// WiFi agina hic girmeden, dogrudan telefonla eslesip Kontrol
// sekmesindeki ayni aksiyonlari (lamba/alarm/kapi/panik) calistirabilsin
// diye eklendi. Ayni RS485 komutlarini gonderen yukaridaki *Ayarla/*Tetikle
// fonksiyonlarini kullanir, boylece web ve BLE her zaman ayni davranir.
#if ENABLE_BLE

// NOT: Onceki BLEDevice.h (Bluedroid) birkac baglan/kopar dongusunden sonra
// yeni baglantilari sessizce reddediyordu (telefonda "status=4 connection
// timeout" - bilinen Bluedroid zayifligi, tekrar tekrar gozlemlendi). NimBLE
// bu senaryoda cok daha kararli oldugu icin gecildi (bkz platformio.ini).
//
// GERCEK KOK NEDEN (asil bulunmasi gereken buydu): NimBLE'ye gecince de
// "koptu diyor ama zorla baglanmaya calisiyor, telefonda BT+APK'yi zorla
// kapatip actiktan sonra duzeliyor" sikayeti surdu. Sebep kutuphane degil -
// bu cihaz WiFi'yi AP+STA modunda surekli acik tutuyor, ustune MQTT, SSE,
// OTA dinleme, ve (en agirlari) hava durumu/telegram icin TLS (HTTPS)
// istekleri hep ayni tek 2.4GHz radyoyu BLE ile paylasiyor. TLS handshake
// birkac saniye surebilen yogun bir radyo/CPU patlamasi - o pencerede BLE
// baglanti olaylari art arda kacirilirsa telefonun supervision timeout'u
// dolup baglanti "temiz kapanmadan" (sessizce) kopuyor. Android boyle
// "kirli" kopmalardan sonra GATT'i bazen dogru temizlemiyor - uygulama
// "bagli"/"tekrar baglaniyor" sanip kilitleniyor, tek cikis BT'yi (ve
// genelde uygulamayi da) tamamen kapatip acmak oluyor. Bu, cihazin kendi
// hatasi degil, telefon tarafinin bilinen bir zayifligi.
//
// DENENDI VE GERI ALINDI: onConnect'te slave latency+supervision timeout
// yukselten bir updateConnParams cagrisi eklenmisti (Espressif'in WiFi+BLE
// coexistence onerisi). Kopma sikligini degistirmedi, ustune LAMBA_AC gibi
// komutlarin yanit suresini gozle gorulur yavaslatti (slave latency ESP32'nin
// gelen yaziyi ne zaman isleyecegini geciktiriyor) - net kazanc negatifti,
// kaldirildi (bkz BleSunucuCallback::onConnect). Su an elde kalan tek somut
// onlem: agir TLS isteklerini heap acisindan guvenli hale getirmek
// (weatherKontrolEt/telegramMesajGonder heap guard'i, asagida) ve boot/heap
// loglama - kopma tekrarlarsa Serial Monitor'den gercek kanit (crash mi, RF
// timeout mu) toplanip ona gore hedefli bir duzeltme yapilmali; radyo
// parametreleriyle korme denemeye devam etmek (yukaridaki gibi) yan etkisiz
// degil.
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleCharacteristic = nullptr;
bool bleDeviceConnected = false;
unsigned long last_ble_notify_ms = 0;

// Telefondan gelen metin komutunu isler, sonucu ayni karakteristik
// uzerinden "ACK:..."/"ERR:..." olarak geri bildirir (RS485 ACK deseniyle
// tutarli). Desteklenen komutlar Kontrol sekmesindeki butonlarla birebir
// eslesir: LAMBA_AC/LAMBA_KAPAT, ALARM_AC/ALARM_KAPAT, ALARM_MOD=1|2|3,
// ALARM_SUSTUR, ALARM_ONAYLA, KAPI_AC/KAPI_KAPAT, PANIK.
void bleKomutIsle(NimBLECharacteristic* pChar, const String& komut) {
  DEBUG_PRINT("[BLE] Komut alindi: ");
  DEBUG_PRINTLN(komut);

  String mesaj;
  bool ok = komutCalistir(komut, mesaj);

  String durum = (ok ? "ACK:" : "ERR:") + mesaj;
  pChar->setValue(durum.c_str());
  if (bleDeviceConnected) pChar->notify();
}

class BleKomutCallback: public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    String komut = String(pChar->getValue().c_str());
    komut.trim();
    if (komut.length() == 0) return;
    bleKomutIsle(pChar, komut);
  }
};

class BleSunucuCallback: public NimBLEServerCallbacks {
  // NOT: Burada daha once slave latency=4 + uzun supervision timeout isteyen
  // bir updateConnParams cagrisi vardi ("coexistence toleransi" denemesi).
  // Gercekte kopma sikligini degistirmedi ama LAMBA_AC gibi komutlarin
  // yanit suresini gozle gorulur sekilde yavaslatti (slave latency, ESP32'nin
  // gelen yazmayi/geri bildirimi ne zaman isleyecegini geciktiriyor) - net
  // sonuc kazançsiz bir yavaslamaydi, geri alindi. Baglanti parametreleri
  // artik telefonun (Android) varsayilanina birakiliyor - degisiklik oncesi
  // davranisin ayni si.
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    bleDeviceConnected = true;
    DEBUG_PRINT("[BLE] Telefon baglandi, heap=");
    DEBUG_PRINTLN(ESP.getFreeHeap());
  }
  void onDisconnect(NimBLEServer* pServer) override {
    bleDeviceConnected = false;
    DEBUG_PRINT("[BLE] Baglanti koptu, heap=");
    DEBUG_PRINTLN(ESP.getFreeHeap());
    // NOT: Yeniden yayina baslama artik burada elle yapilmiyor - NimBLEServer
    // her disconnect'te bunu zaten otomatik yapiyor (m_advertiseOnDisconnect,
    // varsayilan acik). Elle ikinci bir startAdvertising() cagrisi sadece
    // "already active" uyarisi birakan gereksiz bir tekrardi.
  }
};

void ble_init() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setMTU(247); // telefon MTU istegi yaparsa tam durum satiri tek pakette gitsin

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new BleSunucuCallback());

  NimBLEService* bleService = bleServer->createService(BLE_SERVICE_UUID);
  bleCharacteristic = bleService->createCharacteristic(
    BLE_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  bleCharacteristic->setCallbacks(new BleKomutCallback());
  bleService->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  NimBLEDevice::startAdvertising();

  DEBUG_PRINTLN("[BLE] Aktif, telefon baglantisi bekleniyor...");
}

// Bagliyken BLE_NOTIFY_INTERVAL_MS'de bir gercek anlik veriyi (RS485'ten
// gelen ayni sensorData/nanoStatus/alarmStatus) kompakt bir satir olarak
// pushlar - ornek koddaki rastgele T:/H: simulasyonunun yerini alir.
void bleDurumBildir() {
  if (!bleDeviceConnected) return;
  unsigned long simdi = millis();
  if (simdi - last_ble_notify_ms < BLE_NOTIFY_INTERVAL_MS) return;
  last_ble_notify_ms = simdi;

  char buf[160];
  snprintf(buf, sizeof(buf),
    "LEVEL=%.1f,PCT=%.1f,TEMP=%.1f,LAMP=%d,D1=%d,D2=%d,ALARM=%d,PANIC=%d,MOD=%d,MUTE=%d,PEND=%d,TRIG=%d",
    sensorData.level_cm, sensorData.level_percent, sensorData.temperature,
    nanoStatus.lamp_on ? 1 : 0, nanoStatus.door1_open ? 1 : 0, nanoStatus.door2_open ? 1 : 0,
    alarmStatus.enabled ? 1 : 0, alarmStatus.panic_mode ? 1 : 0, alarmStatus.mode,
    alarmStatus.muted ? 1 : 0, alarmStatus.pending ? 1 : 0, alarmStatus.trigger_mask);

  bleCharacteristic->setValue((uint8_t*)buf, strlen(buf));
  bleCharacteristic->notify();
}

#endif // ENABLE_BLE

void handleAPI_Wifi() {
  if (!server.hasArg("ssid")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"SSID eksik\"}");
    return;
  }
  String ssid = server.arg("ssid");
  String sifre = server.hasArg("sifre") ? server.arg("sifre") : "";

  // Bos SSID -> kayitli agi kaldir, varsayilana don
  if (ssid.length() == 0) {
    wifiCredKaydet("", "");
    WiFi.disconnect();
    server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Kaldirildi\"}");
    return;
  }

  wifiCredKaydet(ssid, sifre);
  DEBUG_PRINT("[WiFi] Kaydedilen SSID: "); DEBUG_PRINTLN(ssid);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), sifre.c_str());
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Kaydedildi, baglaniliyor\"}");
}

void handleAPI_WifiScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    String ss = WiFi.SSID(i);
    ss.replace("\\", "\\\\");
    ss.replace("\"", "\\\"");
    j += "{\"ssid\":\"" + ss + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secured\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

void handleAPI_Restart() {
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Yeniden baslatiliyor\"}");
  delay(500);
  ESP.restart();
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/status", handleAPI_Status);
  server.on("/events", handleSSE);
  server.on("/api/ota", handleOTA);
  server.on("/update", HTTP_POST, handleFileUploadUpdate, handleFileUploadProgress);
  server.on("/api/kayit/yedekle", handleAPI_KayitYedekle);
  server.on("/api/kayit/geri_yukle", handleAPI_KayitGeriYukle);
  server.on("/api/kayit/yedek_durum", handleAPI_KayitYedekDurum);
  server.on("/api/weather", handleAPI_WeatherGet);
  server.on("/api/weather/check", handleAPI_WeatherCheck);
  server.on("/api/telegram/test", handleAPI_TelegramTest);
  server.on("/api/telegram/ayar", handleAPI_TelegramAyar);
  server.on("/api/konteyner/pir_ayar", handleAPI_KonteynerPirAyar);
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUpload, handleFirmwareUploadProgress);
  server.on("/firmware/esp8266.bin", HTTP_GET, handleFirmwareServe);
  server.on("/api/firmware/durum", handleFirmwareDurum);
  server.on("/api/lamba", handleAPI_Lamba);
  server.on("/api/moisture", handleAPI_MoistureToggle);
  server.on("/api/moisture/auto", handleAPI_MoistureAuto);
  server.on("/api/moisture/threshold", handleAPI_MoistureThreshold);
  server.on("/api/alarm", handleAPI_Alarm);
  server.on("/api/konteyner/alarm", handleAPI_KonteynerAlarm);
  server.on("/api/alarm/mod", handleAPI_AlarmMod);
  server.on("/api/alarm/mute", handleAPI_AlarmMute);
  server.on("/api/alarm/onayla", handleAPI_AlarmOnayla);
  server.on("/api/alarm/onayla_lamba", handleAPI_AlarmOnaylaLamba);
  server.on("/api/kapi", handleAPI_Kapi);
  server.on("/api/panic", handleAPI_Panic);
  server.on("/api/wifi", handleAPI_Wifi);
  server.on("/api/wifi/scan", handleAPI_WifiScan);
  server.on("/api/restart", handleAPI_Restart);
  server.on("/api/sudepo_ayarlar", handleAPI_SudepoAyarlarGetir);
  server.on("/api/sudepo_ayarlar/kaydet", handleAPI_SudepoAyarlarKaydet);
  server.on("/api/ir/liste", handleAPI_IrListe);
  server.on("/api/ir/ogren_baslat", handleAPI_IrOgrenBaslat);
  server.on("/api/ir/ogren_durum", handleAPI_IrOgrenDurum);
  server.on("/api/ir/kaydet", handleAPI_IrKaydet);
  server.on("/api/ir/sil", handleAPI_IrSil);

  server.begin();
  DEBUG_PRINTLN("[WEB] Server started on port 80");
}

void setup_ota() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("[OTA] Finished");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINTLN("[OTA] Error");
  });
  ArduinoOTA.begin();
  DEBUG_PRINTLN("[OTA] Ready");
}

// ============================================================
// WiFi & MQTT KURULUM
// ============================================================

void wifi_connect() {
  // ESP8266'daki gibi: AP her zaman acik (STA basarisiz olsa da paneline
  // erisim kaybolmasin), STA kayitli ag varsa ona baglanir.
  wifiCredYukle();
  bool ozelAg = savedSSID.length() > 0;
  String ssid = ozelAg ? savedSSID : String(WIFI_SSID);
  String pass = ozelAg ? savedPass : String(WIFI_PASSWORD);

  DEBUG_PRINT("[WiFi] Connecting to ");
  DEBUG_PRINTLN(ssid);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);
  WiFi.softAPConfig(
    IPAddress(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4),
    IPAddress(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4),
    IPAddress(255, 255, 255, 0)
  );

  // Statik IP sadece varsayilan (config.h'daki) ag icin gecerli - farkli bir
  // ag kaydedilirse o agin kendi DHCP'si kullanilir (sabit IP baglanti
  // hatasina yol acabilirdi).
  if (!ozelAg) {
    WiFi.config(
      IPAddress(WIFI_STATIC_IP_OCTET_1, WIFI_STATIC_IP_OCTET_2, WIFI_STATIC_IP_OCTET_3, WIFI_STATIC_IP_OCTET_4),
      IPAddress(WIFI_GATEWAY_OCTET_1, WIFI_GATEWAY_OCTET_2, WIFI_GATEWAY_OCTET_3, WIFI_GATEWAY_OCTET_4),
      IPAddress(WIFI_SUBNET_OCTET_1, WIFI_SUBNET_OCTET_2, WIFI_SUBNET_OCTET_3, WIFI_SUBNET_OCTET_4),
      IPAddress(WIFI_DNS1_OCTET_1, WIFI_DNS1_OCTET_2, WIFI_DNS1_OCTET_3, WIFI_DNS1_OCTET_4),
      IPAddress(WIFI_DNS2_OCTET_1, WIFI_DNS2_OCTET_2, WIFI_DNS2_OCTET_3, WIFI_DNS2_OCTET_4)
    );
  }

  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());

    int attempts = 0;
    unsigned long start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      if (millis() - start_ms > 100) {  // 100ms per attempt (no blocking)
        DEBUG_PRINT(".");
        attempts++;
        start_ms = millis();
        yield();  // ESP32 diğer görevleri yapsın
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN();
    DEBUG_PRINT("[WiFi] Connected! IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
  } else {
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("[WiFi] STA baglanamadi, sadece AP modunda calisiyor");
  }
  DEBUG_PRINT("[WiFi] AP IP: ");
  DEBUG_PRINTLN(WiFi.softAPIP());
}

// Beklenmedik reset (crash/brownout/watchdog) BLE baglantisini telefona hic
// "disconnect" bildirmeden koparir - "koptu ama zorla baglanmaya calisiyor"
// sikayetinin bir kaynagi da bu olabilir. Her boot'ta sebep loglanir; eger
// tekrar ederse (PANIC/WDT/BROWNOUT) asil sorun BLE degil, cihazin
// coktugu/resetlendigi olur.
void resetSebebiYazdir() {
  esp_reset_reason_t sebep = esp_reset_reason();
  const char* metin;
  switch (sebep) {
    case ESP_RST_POWERON:   metin = "Power-on"; break;
    case ESP_RST_EXT:       metin = "Harici reset"; break;
    case ESP_RST_SW:        metin = "Yazilim (ESP.restart)"; break;
    case ESP_RST_PANIC:     metin = "PANIC/Crash"; break;
    case ESP_RST_INT_WDT:   metin = "Interrupt Watchdog"; break;
    case ESP_RST_TASK_WDT:  metin = "Task Watchdog"; break;
    case ESP_RST_WDT:       metin = "Diger Watchdog"; break;
    case ESP_RST_DEEPSLEEP: metin = "Deep sleep uyanma"; break;
    case ESP_RST_BROWNOUT:  metin = "Brownout (guc dususu)"; break;
    case ESP_RST_SDIO:      metin = "SDIO"; break;
    default:                metin = "Bilinmeyen"; break;
  }
  DEBUG_PRINT("[BOOT] Reset sebebi: ");
  DEBUG_PRINTLN(metin);
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(9600);
  delay(1000);
  resetSebebiYazdir();

  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("🟢 SuDepoSis v2 - ESP32-S3 Master");
  DEBUG_PRINTLN("========================================");
  
  // RS485 Initialize
  rs485_init();

  // SPIFFS - kayit yedekleme icin (bkz esp8266KayitYedekle/GeriYukle)
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("[SPIFFS] Baslatilamadi");
  }
  weatherYukle();
  irEslesmeYukle();
  telegramAyarYukle();
  konteynerPirAyarYukle();
  konteynerAlarmAyarYukle();

  // WiFi Connect
  wifi_connect();
  setup_ota();

  // MQTT Setup
  if (ENABLE_MQTT) {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    DEBUG_PRINT("[MQTT] Broker: ");
    DEBUG_PRINTLN(MQTT_BROKER);
  }
  
  // Web Server
  setupWebServer();

  // BLE - telefon uygulamasi (WiFi'siz de calisir)
  //
  // TANI SONUCU (onemli): BLE baglantisi hicbir zaman kurulamiyordu
  // (status=4). WiFi'yi (AP dahil) tamamen kapatarak ve hatta izole,
  // sifirdan minimal bir test firmware'iyle (WiFi/RS485/MQTT hicbiri yok)
  // test edildi - AYNI hata devam etti. nRF Connect (kanitlanmis, profesyonel
  // BLE araci) ise ayni ESP32'ye sorunsuz baglandi. Bu, sorunun ESP32
  // tarafinda OLMADIGINI, Android uygulamasindaki connectGatt() cagrisinda
  // oldugunu kesin olarak kanitladi (bkz BLEDProject BleRepository.kt -
  // TRANSPORT_LE eksikligi asil sebepti). ESP32 tarafinda bu yuzden baska
  // bir degisiklige gerek yoktu.
#if ENABLE_BLE
  ble_init();
#endif

  // Konteyner donanimi (IR/LED/PIR2/Reed) - RS485/BLE/WiFi'den bagimsiz
  konteynerDonanimiInit();

  // ===== BILGILER =====
  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("✅ SISTEM BILGILERI");
  DEBUG_PRINTLN("========================================");
  DEBUG_PRINT("📡 AP SSID: "); DEBUG_PRINTLN(AP_SSID);
  DEBUG_PRINT("📡 AP IP: "); DEBUG_PRINTLN(WiFi.softAPIP());
  DEBUG_PRINT("🌐 mDNS: "); DEBUG_PRINT(MDNS_NAME); DEBUG_PRINTLN(".local");
  DEBUG_PRINT("🔧 OTA Host: "); DEBUG_PRINTLN(OTA_HOSTNAME);
  DEBUG_PRINT("🔧 OTA Port: 3232\n");
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINT("📶 WiFi: "); DEBUG_PRINT(WIFI_SSID); DEBUG_PRINT(" IP: "); DEBUG_PRINTLN(WiFi.localIP());
  } else {
    DEBUG_PRINTLN("📶 WiFi: AP modunda (STA bagli degil)");
  }
  DEBUG_PRINT("🌍 Web: http://"); DEBUG_PRINT(MDNS_NAME); DEBUG_PRINTLN(".local");
  DEBUG_PRINT("🌍 Web AP: http://"); DEBUG_PRINTLN(WiFi.softAPIP());
  DEBUG_PRINTLN("========================================\n");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  // Duzenli heap izleme - dusuk/dususte heap, BLE'nin "kirli" kopmasina
  // (crash/coexistence) yol acan asil sebebi teshis etmek icin (bkz BLE
  // bolumundeki not). Sorun tekrarlarsa Serial Monitor'de bu satirlar
  // dususu/cokmeyi gosterir.
  static unsigned long sonHeapLogMs = 0;
  if (millis() - sonHeapLogMs >= 30000UL) {
    sonHeapLogMs = millis();
    DEBUG_PRINT("[HEAP] free=");
    DEBUG_PRINTLN(ESP.getFreeHeap());
  }

  // Web Server handle
  server.handleClient();
  ArduinoOTA.handle();

  // RS485 Polling
  rs485_poll();

  // Konteyner donanimi - sadece okuma/yerel LED, alarm mantigina yazmiyor
  konteynerSensorleriOku();
  alarmLedGuncelle();
  irKumandaIsle();
  irKomutIsleVeCalistir();

  // Hava durumu / yagmur tahmini - WiFi baglandiginda veya periyodik
  weatherKontrolEt();

  // Alarm baslarsa Telegram'a bildirim gonder
  telegramAlarmKontrolEt();

  // BLE - bagli telefona periyodik anlik veri
#if ENABLE_BLE
  bleDurumBildir();
#endif

  // MQTT
  mqtt_connect();
  mqtt_publish();
  
  // Alarm Kontrol
  if (sensorData.level_percent < ALARM_LEVEL_PERCENT) {
    alarmStatus.low_level_alarm = true;
  } else {
    alarmStatus.low_level_alarm = false;
  }
  
  // MQTT Callback process
  if (ENABLE_MQTT) {
    mqttClient.loop();
  }

  // SSE periyodik durum guncelleme (1000ms) - RS485'ten yeni veri gelmese de
  // baglı istemciler duzenli tazelenir (fallback, asil push veri geldiginde olur)
  static unsigned long sonSseMs = 0;
  unsigned long simdiMs = millis();
  if (simdiMs - sonSseMs >= 1000UL) {
    sonSseMs = simdiMs;
    ssePush();
  }
}