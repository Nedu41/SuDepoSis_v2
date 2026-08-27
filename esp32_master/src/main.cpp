// ============================================================
// SuDepoSis v2 - ESP32-S3 Master
// RS485 Hub + Web Dashboard + MQTT Publisher
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
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
#include <Wire.h>

#include "../include/config.h"

void irAliciBaslat(); // asagida "IR KUMANDA - HAM KENAR YAKALAMA" bolumunde tanimli

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

// Alarm logu icin "zaman" onbellegi - GET_ZAMAN'i HER alarm basladiginda
// (hot path) sormak yerine, dusuk oncelikli olarak arka planda ~60sn'de bir
// tek denemeyle (kisa timeout, retry YOK) yeniler. Boylece bir alarm tam da
// tetiklendigi anda RS485 hattini bloke edip siren/lamba tepkisini
// geciktirme riski YOK - bkz RS485 gecikme dersi (proje hafizasi).
String zamanCacheStr = "-";
unsigned long zamanCacheSonGuncellemeMs = 0;
#define ZAMAN_CACHE_YENILEME_MS (60UL * 1000UL)

void zamanCacheGuncelle() {
  if (zamanCacheSonGuncellemeMs != 0 && millis() - zamanCacheSonGuncellemeMs < ZAMAN_CACHE_YENILEME_MS) return;
  zamanCacheSonGuncellemeMs = millis();
  String zamanReply;
  if (rs485_send_wait_ack("MASTER:GET_ZAMAN\n", zamanReply, 500, 1)) {
    int eq = zamanReply.indexOf("GET_ZAMAN=");
    if (eq >= 0) {
      zamanCacheStr = zamanReply.substring(eq + 10);
      zamanCacheStr.trim();
    }
  }
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
  // FIX: timeout verilmezse HTTPClient varsayilani (~5sn, ag sikintisinda
  // daha da uzayabilir) kullanir - bu sure boyunca tek dongulu loop() (ve
  // onun icindeki tum web/RS485/buton islemesi) tamamen bloke olur. Bu
  // fonksiyon sadece WEATHER_CHECK_INTERVAL_MS'de bir calistigi icin nadir
  // ama gercek bir "butonlar bir anda donuyor" hissi yaratabilir.
  http.setTimeout(5000);
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

  // Boot sonrasi ilk WEATHER_BOOT_GRACE_MS icinde HENUZ fetch atma - web
  // sunucusuna (server.handleClient()) nefes alma payi ver, bkz config.h
  // WEATHER_BOOT_GRACE_MS yorumu. lastWeatherCheckMs=0 KALIR ki grace suresi
  // bitince zamanGeldi normal sekilde tetiklensin (fetch atlanmis olmaz,
  // sadece ertelenir).
  if (millis() < WEATHER_BOOT_GRACE_MS) return;

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

// MPPT sarj kontrolcusunden (ikinci/ayri RS232+PI30 hatti) okunan
// elektriksel degerler - bkz config.h "MPPT (RS232/PI30) Ayarlari". Ana
// sayfada SADECE birkac alan (battery_voltage/load_current/load_power/
// pv_voltage/pv_power/kalan_saat) gosterilir - geri kalani "Invertor"
// sekmesinde (bkz web arayuzu) detay olarak sunulur.
struct MpptData {
  // Ana sayfada da gosterilen ozet alanlar
  float battery_voltage = 0.0;
  float pv_voltage = 0.0;
  float pv_current = 0.0;
  float pv_power = 0.0;      // W - QPIGS "PV sarj gucu" alani
  float load_voltage = 0.0;
  float load_current = 0.0; // dogrudan alan yok, load_power/load_voltage'dan hesaplanir
  float load_power = 0.0;    // W
  int battery_soc = -1;      // % - MPPT'nin kendi tahmini, -1 = henuz okunmadi
  float kalan_saat = -1.0;   // Mevcut net tuketimle tahmini kalan sure, -1 = N/A (sarj oluyor/veri yok)
  bool read_ok = false;
  unsigned long last_update_ms = 0;

  // Sadece "Invertor" sekmesinde gosterilen QPIGS detay alanlari
  float grid_voltage = 0.0;
  float grid_freq = 0.0;
  float ac_out_freq = 0.0;
  float apparent_power = 0.0; // VA
  int load_percent = 0;       // %
  float bus_voltage = 0.0;
  float temperature_c = 0.0;  // invertor isi kanat sicakligi
  float battery_charge_current = 0.0;    // aku sarj akimi (AC/SCC toplam)
  float battery_discharge_current = 0.0; // aku desarj akimi
  String status_bits = "";    // b7..b0 ham string (QPIGS field 16)
  bool load_on = false;
  bool charging_on = false;
  bool scc_charging_on = false;
  bool ac_charging_on = false;

  // QMOD (calisma modu)
  char mode_code = '?';
  String mode_text = "Bilinmiyor";
  bool mode_read_ok = false;

  // QPIWS (36 bit uyari/ariza) - ham string + aktif olanlarin listesi
  String warn_raw = "";
  String warn_list = ""; // virgulle ayrilmis aktif uyari adlari, bos = uyari yok
  bool warn_read_ok = false;

  // QPIRI (anma degerleri/ayarlar) - yavas periyotta okunur, degisim az
  float qpiri_batt_voltage = 0.0;
  float qpiri_batt_recharge_v = 0.0;
  float qpiri_batt_under_v = 0.0;
  float qpiri_batt_bulk_v = 0.0;
  float qpiri_batt_float_v = 0.0;
  int qpiri_batt_type = -1;          // 0:AGM 1:Sulu 2:Kullanici
  int qpiri_max_ac_charge_a = -1;
  int qpiri_max_charge_a = -1;
  int qpiri_out_source_priority = -1; // 0:Sebeke 1:Solar 2:SBU
  int qpiri_charger_source_priority = -1; // 0:Sebeke 1:Solar 2:Solar+Sebeke 3:Sadece solar
  bool qpiri_read_ok = false;
  unsigned long qpiri_last_update_ms = 0;
};

// Global Veri
SensorData sensorData;
NanoIOStatus nanoStatus;
AlarmStatus alarmStatus;
MpptData mpptData;
SemaphoreHandle_t mpptDataMutex = NULL; // mpptData: ayri task (yazar) ile loop() (okur) arasi guvenli erisim
struct MpptKilit {
  MpptKilit() { xSemaphoreTake(mpptDataMutex, portMAX_DELAY); }
  ~MpptKilit() { xSemaphoreGive(mpptDataMutex); }
};
bool esp8266BatteryLowAck = false; // ESP8266'nin SET_BATTERY_LOW komutuna gore uyguladigi durum
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
// Swan Quad PET PIR (ticari alarm dedektoru, NC/COM role kontagi) - HC-SR505
// (pir2HareketVar) gibi kendi donanimsal debounce/pet-immunity'si var, bu
// yuzden VARSAYILAN olarak hala kapi gibi ANINDA eskale eder (bkz
// konteynerSwanTutmaSaniye/OnaySaniye varsayilan 0). 2026-08-27 kullanici
// talebiyle PIR2'deki gibi ISTEGE BAGLI Tutma/Onay suresi eklendi - sifir
// birakilirsa davranis DEGISMEZ, kullanici sahada yanlis tetiklenme
// yasarsa buradan bir gecikme ekleyebilir.
bool swanPirVar = false;
bool konteynerSwanAlarmVar = false;        // Hareket VAR veya tutma suresi icinde (PIR2'deki konteynerPirAlarmVar esdegeri)
bool konteynerSwanEskalasyonOldu = false;  // bu "bolum"de Onay Suresi asildi mi
unsigned long konteynerSwanBolumBaslangicMs = 0; // 0 = aktif bolum yok
unsigned long konteynerSwanSonHareketMs = 0;     // 0 = HENUZ HIC true olmadi (sentinel)
bool konteynerDumanVar = false; // GP2Y10 (optik toz/duman sensoru) esik asildi mi - kapi/Swan gibi ANINDA eskale eder, tutma/onay suresi yok (bkz gp2y10Poll)
bool konteynerGazVar = false; // MQ6 esik asildi mi - panik gibi ANINDA eskale eder (bkz alarmLedGuncelle, mq6Poll)
bool pir2HareketVar = false;  // Konteyner PIR - true = hareket var (ham)
bool konteynerPirAlarmVar = false; // Hareket VAR veya tutma suresi icinde (ham "bolum" durumu)
unsigned long konteynerPirSonHareketMs = 0; // pir2HareketVar'in en son true goruldugu an - 0 = HENUZ HIC true olmadi (sentinel, gercek bir zaman degil)

// Boot sonrasi HC-SR505'in isinma suresi boyunca cikisi kararsiz/rastgele
// HIGH verebilir - bu sure icinde eskalasyon (siren/lamba/Telegram)
// TETIKLENMEZ. Ham "Hareket" okuma alani bu sureden bagimsiz, her zaman
// anlik pin durumunu gosterir.
#define KONTEYNER_PIR_BOOT_GRACE_MS 90000UL

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
// alarmStatus.muted KULLANILAMAZ: o alan her ESP8266 RS485 durum mesajinda
// (~600ms) ESP8266'nin kendi mute alaniyla eziliyor. Konteyner yerel
// calistigindan (bkz PIR2 aciklamasi) kendi ayri, RS485'ten bagimsiz bayragi.
bool konteynerSusturuldu = false;
#define KONTEYNER_SIREN_MAX_MS (2UL * 60UL * 1000UL) // bu kadar kesintisiz calarsa (sensor arizasi ihtimaline karsi) otomatik susturulur
unsigned long konteynerLambaMinSureBaslangicMs = 0; // 0 = MIN sure penceresi kapali
#define KONTEYNER_LAMBA_MIN_SURE_MS (60UL * 1000UL) // alarm erken temizlense bile lamba en az bu kadar yanik kalir
unsigned long konteynerSirenEpisodeMs = 0; // 0 = siren cikisi su an secili degil (bkz asagidaki kademeli zamanlama deseni) - HEM state machine referans ani HEM oto-sustur olcumu icin kullanilir
unsigned long konteynerLambaSurekliBaslangicMs = 0; // 0 = alarm-tetikli lamba su an surekli yanmiyor
#define KONTEYNER_LAMBA_MAX_SURE_MS (10UL * 60UL * 1000UL) // alarm-tetikli lamba (manuel haric) bu kadar kesintisiz yanarsa zorla soner - enerji butcesi icin ust sinir
bool konteynerOtoSusturBildirimBekliyor = false; // yukaridaki oto-sustur tetiklendiginde Telegram fonksiyonuna sinyal
bool konteynerLambaManuel = false;         // Kontrol sekmesinden elle acilan/kapanan lamba - alarm/siren'den BAGIMSIZ, otomatik davranisla OR'lanir (bkz alarmLedGuncelle)
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

// Swan Quad PET PIR icin de PIR2 ile AYNI Tutma/Onay Suresi mantigi (2026-08-27
// kullanici talebi) - ONCEDEN Swan'in kendi donanimsal debounce'u oldugu icin
// yazilim gecikmesine gerek olmadigi dusunulmustu (bkz asagidaki swanPirVar
// yorumu), ama kullanici sahada zamanlama ayari istedi. Varsayilan 0/0 =
// ESKI DAVRANISLA AYNI (aninda eskale) - boylece bu ozellik mevcut sahadaki
// cihazlarda davranisi SESSIZCE degistirmez, kullanici acikca bir sure girene
// kadar Swan hala aninda tetikler.
uint16_t konteynerSwanTutmaSaniye = 0;
uint16_t konteynerSwanOnaySaniye = 0;
void konteynerSwanAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerSwanTutmaSaniye = ayarPrefs.getUShort("k_swan_tut", 0);
  konteynerSwanOnaySaniye = ayarPrefs.getUShort("k_swan_onay", 0);
  ayarPrefs.end();
}
void konteynerSwanAyarKaydet(uint16_t tutmaSaniye, uint16_t onaySaniye) {
  konteynerSwanTutmaSaniye = tutmaSaniye;
  konteynerSwanOnaySaniye = onaySaniye;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putUShort("k_swan_tut", tutmaSaniye);
  ayarPrefs.putUShort("k_swan_onay", onaySaniye);
  ayarPrefs.end();
}

// Her Konteyner sensoru (PIR2, kapi reed, Swan PIR) ayri ayri aktif/pasif
// yapilabilir - konteynerAlarmEtkin (zon geneli) bunun ustunde, o kapaliysa
// hepsi zaten devre disi. Kullanici talebi: Swan PIR henuz donanima
// baglanmadigi icin (fail-safe "acik devre" okumasi surekli true) devre disi
// birakilabilsin, aksi halde bu ham true degeri konteynerOnayBekleniyor/
// konteynerSusturuldu gibi bayraklarin hic sifirlanmamasina yol aciyordu.
bool konteynerPirEtkin = true;
bool konteynerKapiEtkin = true;
bool konteynerSwanEtkin = true;
bool konteynerDumanEtkin = true;
bool konteynerGazEtkin = true;
void konteynerSensorAktifYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerPirEtkin = ayarPrefs.getBool("k_pir_en", true);
  konteynerKapiEtkin = ayarPrefs.getBool("k_kapi_en", true);
  konteynerSwanEtkin = ayarPrefs.getBool("k_swan_en", true);
  konteynerDumanEtkin = ayarPrefs.getBool("k_duman_en", true);
  konteynerGazEtkin = ayarPrefs.getBool("k_gaz_en", true);
  ayarPrefs.end();
}
void konteynerSensorAktifKaydet(bool pirEtkin, bool kapiEtkin, bool swanEtkin, bool dumanEtkin, bool gazEtkin) {
  konteynerPirEtkin = pirEtkin;
  konteynerKapiEtkin = kapiEtkin;
  konteynerSwanEtkin = swanEtkin;
  konteynerDumanEtkin = dumanEtkin;
  konteynerGazEtkin = gazEtkin;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putBool("k_pir_en", pirEtkin);
  ayarPrefs.putBool("k_kapi_en", kapiEtkin);
  ayarPrefs.putBool("k_swan_en", swanEtkin);
  ayarPrefs.putBool("k_duman_en", dumanEtkin);
  ayarPrefs.putBool("k_gaz_en", gazEtkin);
  ayarPrefs.end();
}

// MQ6 gaz esigi (Volt) - NVS'de kalici, web'den ayarlanabilir. Varsayilan
// KONSERVATIF/tahmini bir deger - MQ6'un kalibrasyon potu olmadigindan
// (bu projede sadece ham ADC/volt okunuyor) sahada gercek gaz kaynagiyla
// (cakmak gazi vb.) test edilip DOGRULANMALI, tetiklenmiyorsa dusurulmeli/
// yanlis tetikliyorsa yukseltilmeli.
float konteynerGazEsikVolt = 2.0f;
void konteynerGazAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerGazEsikVolt = ayarPrefs.getFloat("k_gaz_esik", 2.0f);
  ayarPrefs.end();
}
void konteynerGazAyarKaydet(float esikVolt) {
  konteynerGazEsikVolt = esikVolt;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putFloat("k_gaz_esik", esikVolt);
  ayarPrefs.end();
}

// GP2Y10 duman/toz esigi (Volt) - NVS'de kalici, web'den ayarlanabilir.
// MQ6 esigiyle AYNI desen: kalibrasyon potu yok, ham Vo okunuyor, sahada
// gercek dumanla (guvenli mesafeden, orn. mum/tutusturucu) test edilip
// DOGRULANMALI. Sensorun kendi sifir-nokta gerilimi tipik ~0.5-0.9V civari
// (partikul yokken), duman/toz artinca yukselir.
float konteynerDumanEsikVolt = 1.5f;
void konteynerDumanAyarYukle() {
  ayarPrefs.begin("ayarlar", true);
  konteynerDumanEsikVolt = ayarPrefs.getFloat("k_duman_esik", 1.5f);
  ayarPrefs.end();
}
void konteynerDumanAyarKaydet(float esikVolt) {
  konteynerDumanEsikVolt = esikVolt;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putFloat("k_duman_esik", esikVolt);
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
  // Alarm kapatilirken bekleyen/verilmis onay bayraklari da temizlenir -
  // aksi halde PIR hala aktif oldugu surece (bkz konteynerSensorleriOku
  // sifirlama kosulu) konteynerLambaOnayVerildi eskiden true kalmissa
  // lamba, alarm devre disi birakilsa bile yanik kaliyordu.
  if (!etkin) {
    konteynerOnayBekleniyor = false;
    konteynerOnayVerildi = false;
    konteynerLambaOnayVerildi = false;
  }
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putBool("k_alarm_en", etkin);
  ayarPrefs.end();
}

// ===== Batarya (MPPT) Koruma Ayarlari =====
// korumaAktif VARSAYILAN KAPALI: MPPT register/olcek sahada (bahcede)
// dogrulanmadan otomatik kesme calismasin diye bilincli tercih - kullanici
// Serial Monitor'daki [MPPT] voltajini multimetreyle dogruladiktan SONRA
// web arayuzunden acik hale getirir (bkz /api/batarya/ayar).
// Histerezis (kesme != geri yukleme esigi): tam esik sinirinda role
// cirpinmasini onlemek icin - konteynerPirTutmaSaniye deseniyle ayni NVS
// (ayarPrefs, "ayarlar" namespace) uzerinden kalici saklanir.
bool bateryaKorumaAktif = false;
float bateryaKesmeVolt = 23.0;
float bateryaGeriYuklemeVolt = 25.0;
bool bateryaKritik = false; // histerezis durumu - bkz bateryaDurumHesapla()

void bateryaAyarlariYukle() {
  ayarPrefs.begin("ayarlar", true);
  bateryaKorumaAktif = ayarPrefs.getBool("bat_aktif", false);
  bateryaKesmeVolt = ayarPrefs.getFloat("bat_kesme", 23.0);
  bateryaGeriYuklemeVolt = ayarPrefs.getFloat("bat_geri", 25.0);
  ayarPrefs.end();
}
void bateryaAyarlariKaydet(bool aktif, float kesme, float geri) {
  bateryaKorumaAktif = aktif;
  bateryaKesmeVolt = kesme;
  bateryaGeriYuklemeVolt = geri;
  if (!aktif) bateryaKritik = false; // koruma kapatilinca kesik yukler hemen serbest kalsin
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putBool("bat_aktif", aktif);
  ayarPrefs.putFloat("bat_kesme", kesme);
  ayarPrefs.putFloat("bat_geri", geri);
  ayarPrefs.end();
}

void konteynerDonanimiInit() {
  pinMode(ALARM_LED_PIN, OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);
  pinMode(KONTEYNER_SIREN_PIN, OUTPUT);
  digitalWrite(KONTEYNER_SIREN_PIN, LOW);
  pinMode(KONTEYNER_LAMBA_PIN, OUTPUT);
  digitalWrite(KONTEYNER_LAMBA_PIN, LOW);
  pinMode(MQ6_POWER_PIN, OUTPUT);
  digitalWrite(MQ6_POWER_PIN, LOW); // ilk dongu mq6Poll()'da baslar, o ana kadar kapali
  // MQ6 analog (Vo) girisi bagli degilken havadan gurultu toplayip oynayan
  // degerler veriyordu (2026-08-25, kullanici bulgusu) - dahili zayif
  // pull-down (~45kOhm) ile bosta pin GND'ye sabitlenip gurultu kesiliyor.
  // Sahada gercek gaz testiyle DOGRULANDI: MQ6 op-amp/comparator tabanli
  // guclu cikisi pull-down'u sorunsuz eziyor, gercek gaz verilince deger hala
  // yukseliyor. GP2Y10'da AYNI sey denendi ama GERI ALINDI (bkz gp2y10Poll())
  // - o sensorun gercek duman sinyali cok kucuk olabilir, DIKKAT: PIR2_PIN'de
  // (asagida) de ayni fikir denenip geri alinmisti, zayif surusluler icin bu
  // pull-down riskli bir desen.
  pinMode(MQ6_ADC_PIN, INPUT_PULLDOWN);
  // INPUT_PULLDOWN denendi (float pin teorisiyle) ama kablo saglam oldugu
  // halde gercek hareketi de algilamaz hale getirdi (muhtemelen sensorun
  // zayif suruculu ciktisiyla dahili pull-down'in gerilim bolucu gibi
  // calismasi) - geri alindi, duz INPUT gercek hareketi guvenilir sekilde
  // yakaliyordu.
  pinMode(PIR2_PIN, INPUT);
  // Reed switch: kablolamaya gore kapali/acik seviyesi degisebilir -
  // ilk kurulumda gercek davranisi /api/durum -> konteyner.kapi_acik'tan
  // gozlemleyip gerekirse asagidaki karsilastirmayi (==HIGH) ters cevir.
  pinMode(KAPI_REED_PIN, INPUT_PULLUP);
  // Swan Quad PET PIR - reed switch ile ayni sekilde NC/COM kontak, kablolama
  // yonune gore ters gerekebilir (bkz SWAN_PIR_PIN yorumu, config.h).
  pinMode(SWAN_PIR_PIN, INPUT_PULLUP);
  pinMode(GP2Y10_LED_PIN, OUTPUT);
  digitalWrite(GP2Y10_LED_PIN, GP2Y10_LED_AKTIF_LOW ? HIGH : LOW); // ilk dongu gp2y10Poll()'da baslar, o ana kadar kapali (LED sonuk)
  irAliciBaslat(); // bkz asagida "IR KUMANDA - HAM KENAR YAKALAMA" bolumu
  DEBUG_PRINTLN("[KONTEYNER] IR/LED/PIR2/Reed/SwanPIR/GP2Y10 hazir");
}

// Acilis "hazir" onay sesi - setup() sonunda (hem power-on hem OTA/yazilim
// resetinde) BIR KEZ calinir. ALARM_LED_PIN aktif tip buzzer oldugundan
// (kendi osilatoru var, dogrudan HIGH/LOW ile calisir - bkz config.h/pinout)
// PWM/tone() ile perde/nota DEGISTIRILEMEZ, sadece ac/kapa suresi ayarlanabilir.
// Bu yuzden "melodi" tek notalik kisa-kisa-uzun bir ritim olarak yapildi
// (eskiden guc kesintisi sonrasi pinin bir an float kalmasindan kaynaklanan,
// kasitsiz "uzun biip" glitch'inin yerine gecen, KASITLI ve KISA bir onay sesi).
void acilisSesiCal() {
  const int ritim[] = {80, 80, 80, 80, 220};
  for (int i = 0; i < 5; i++) {
    digitalWrite(ALARM_LED_PIN, HIGH);
    delay(ritim[i]);
    digitalWrite(ALARM_LED_PIN, LOW);
    delay(70);
  }
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
void acilLambaGuncelle(bool konteynerAcilDurumParam); // asagida tanimli (Acil Durum Lambasi)

void alarmLedGuncelle() {
  static bool ledDurum = false;
  static unsigned long sonDegisimMs = 0;
  static bool onBipAktif = false;
  static unsigned long onBipBaslangicMs = 0;

  // Panik (elle acilan) VE gaz alarmi (MQ6 esigi asildi) - ikisi de enabled/
  // mod'dan BAGIMSIZ, aninda hedeflenir (MOD gecidi atlanir). FIX:
  // konteynerGazEtkin burada da (mq6Poll()'daki gibi) kontrol edilir - MQ6 guc
  // dongusunde CoGU zaman kapali oldugundan (10dk'da 60sn acik) konteynerGazVar
  // sadece o kisa pencerede yeniden hesaplaniyor; kullanici sensoru pasif
  // yaparsa bu degisikligin sonraki guc dongusune kadar (10dk'ya kadar) gec
  // yansimasi yerine BURADA aninda etkili olmasi saglanir.
  bool konteynerGazAlarmVar = konteynerGazEtkin && konteynerGazVar;
  bool konteynerAcilDurum = alarmStatus.panic_mode || konteynerGazAlarmVar;
  acilLambaGuncelle(konteynerAcilDurum);
  bool konteynerEskaleVar = konteynerAcilDurum || (konteynerAlarmEtkin && ((konteynerPirEtkin && konteynerPirEskalasyonOldu) || (konteynerKapiEtkin && kapi2Acik) || (konteynerSwanEtkin && konteynerSwanEskalasyonOldu) || (konteynerDumanEtkin && konteynerDumanVar)));
  // Susturmadan ONCEKI hedef durum - Sustur basildiginda SIREN kesin susar
  // ama LAMBA bu durum surdukce yanmaya devam eder (kullanici talebi: "sustur
  // siren'i kessin, lamba yansin"). Panik/gaz icin susturma zaten asagida
  // (konteynerBuzzerVar hesabinda) tamamen bypass edilir.
  bool konteynerCikisIstenir = false;
  if (konteynerEskaleVar) {
    if (konteynerAcilDurum) konteynerCikisIstenir = true; // Panik/gaz - susturmadan bagimsiz
    else if (alarmStatus.mode == 1) konteynerCikisIstenir = true; // Sesli
    else if (alarmStatus.mode == 3) konteynerCikisIstenir = konteynerOnayVerildi; // Onayli - onaydan sonra
    // mode==2 (Sessiz) ve panik/gaz degilse: false kalir, Telegram/banner yine de calisir
  }
  // Tetik animasyonu (2026-08-25, kullanici talebi, bkz config.h yorumu):
  // GAZ HARIC (patlayici gaz icin gecikme guvenlik riski - aninda/surekli
  // kalir, DEGISMEDI) her tetiklenmede (panik dahil - fiziksel Acil Durum
  // Butonu artik gercek panigi tetikliyor) siren ilk
  // KONTEYNER_TETIK_SIREN_GECIKME_MS sessiz kalir, sonra
  // KONTEYNER_TETIK_SIREN_ATIS_MS acik / KONTEYNER_TETIK_SIREN_ARALIK_MS
  // kapali seklinde tetik surdukce TEKRARLAYAN kisa atislar halinde calar
  // (eski kademeli gecikme/chirp/bekleme/aktif deseninin YERINI ALDI).
  bool konteynerAnimasyonluTetik = konteynerCikisIstenir && !konteynerGazAlarmVar;
  bool konteynerSirenRawHedef;
  if (konteynerGazAlarmVar) {
    konteynerSirenRawHedef = konteynerCikisIstenir; // aninda, surekli - degismedi
    konteynerSirenEpisodeMs = 0;
  } else if (konteynerAnimasyonluTetik) {
    if (konteynerSirenEpisodeMs == 0) konteynerSirenEpisodeMs = millis();
    unsigned long gecenMs = millis() - konteynerSirenEpisodeMs;
    if (gecenMs < KONTEYNER_TETIK_SIREN_GECIKME_MS) {
      konteynerSirenRawHedef = false;
    } else {
      unsigned long donguMs = (gecenMs - KONTEYNER_TETIK_SIREN_GECIKME_MS) % (KONTEYNER_TETIK_SIREN_ATIS_MS + KONTEYNER_TETIK_SIREN_ARALIK_MS);
      konteynerSirenRawHedef = donguMs < KONTEYNER_TETIK_SIREN_ATIS_MS;
    }
  } else {
    konteynerSirenEpisodeMs = 0;
    konteynerSirenRawHedef = false;
  }
  // Siren: hedef durum (yukaridaki tetik animasyonu uygulanmis) VE (panik VEYA susturulmamis).
  // FIX (kullanici bulgusu): gaz alarmi da konteynerAcilDurum'a dahil oldugu
  // icin susturmayi ATLIYORDU - "Sustur" basildiginda siren susmuyordu.
  // Panik (elle acilan, kullanicinin kendi ac/kapa anahtari) susturmadan
  // BAGIMSIZ kalmaya devam eder, ama gaz OTOMATIK bir sensor tetiklemesi -
  // diger tum otomatik tetikleyiciler (PIR/kapi/duman) gibi susturulabilmeli.
  bool konteynerBuzzerVar = konteynerSirenRawHedef && (alarmStatus.panic_mode || !konteynerSusturuldu);

  // Lamba icin MIN sure: alarm/kapi/PIR erken temizlense bile (Sustur
  // basilsin ya da bizzat eskalasyon bitsin) lamba en az
  // KONTEYNER_LAMBA_MIN_SURE_MS boyunca yanik kalir (kullanici talebi: "en
  // az 60sn lambalar yansın"). Manuel/onaylanmis-lamba-flasoru bu tutmaya
  // DAHIL DEGIL - onlarin kendi ani ac/kapa semantigi var.
  if (konteynerCikisIstenir) {
    konteynerLambaMinSureBaslangicMs = millis();
  } else if (konteynerLambaMinSureBaslangicMs != 0 && millis() - konteynerLambaMinSureBaslangicMs >= KONTEYNER_LAMBA_MIN_SURE_MS) {
    konteynerLambaMinSureBaslangicMs = 0;
  }
  bool konteynerCikisIstenirTutulmus = konteynerCikisIstenir || konteynerLambaMinSureBaslangicMs != 0;

  // Lamba icin MAX sure (kullanici talebi, enerji butcesi): alarm-tetikli
  // lamba (konteynerLambaOnayVerildi - "Sessiz (Lamba)" onayi da DAHIL,
  // manuel HARIC) kesintisiz KONTEYNER_LAMBA_MAX_SURE_MS'i (10dk) asarsa
  // zorla soner - siren'deki 2dk oto-sustur deseniyle simetrik.
  bool konteynerAlarmLambaHam = konteynerCikisIstenirTutulmus || konteynerLambaOnayVerildi;
  if (konteynerAcilDurum) {
    konteynerLambaSurekliBaslangicMs = 0; // panik/gaz disinda yeni bir normal tetiklenme sifirdan baslasin
  } else if (konteynerAlarmLambaHam) {
    if (konteynerLambaSurekliBaslangicMs == 0) konteynerLambaSurekliBaslangicMs = millis();
    else if (millis() - konteynerLambaSurekliBaslangicMs > KONTEYNER_LAMBA_MAX_SURE_MS) konteynerAlarmLambaHam = false;
  } else {
    konteynerLambaSurekliBaslangicMs = 0;
  }

  // Sensor arizasi/unutulmus tetiklenmede siren SINIRSIZ calmasin diye
  // EPIZOT BASLANGICINDAN itibaren gecen TOPLAM sure KONTEYNER_SIREN_MAX_MS'i
  // asarsa otomatik susturulur (panik/gaz haric - bunlar elle ac/kapat ya da
  // gaz kaynagi giderilene kadar kendiliginden susmamali). Kademeli zamanlama
  // nedeniyle konteynerBuzzerVar bekleme fazlarinda sik sik false oldugundan,
  // olcum "kesintisiz calma" yerine konteynerSirenEpisodeMs referans alinarak
  // yapilir (aksi halde 2dk'ya asla ulasilamaz).
  if (konteynerSirenEpisodeMs != 0 && !konteynerAcilDurum) {
    if (millis() - konteynerSirenEpisodeMs > KONTEYNER_SIREN_MAX_MS) {
      konteynerSusturuldu = true;
      konteynerBuzzerVar = false;
      konteynerOtoSusturBildirimBekliyor = true;
    }
  }

  // Siren + Lamba (ALARM_LED_PIN'deki kucuk LED+buzzer'dan AYRI, gercek role
  // uzerinden calisan donanim - bkz config.h). Siren, kucuk buzzer ile AYNI
  // kosulda aktif ama SABIT/surekli (role oldugu icin 400ms yanip-sonme YOK,
  // sik ac/kapa roleye zarar verir). Lamba, siren ile birlikte VEYA Onayli
  // modda "Sessiz (Lamba)" onayi verildiginde (konteynerLambaOnayVerildi) TEK
  // BASINA aktif - Sudepo Zonu'ndaki "Onayli - sadece lamba flasoru" secenegiyle
  // ayni mantik, artik Konteyner'de de gercek bir fiziksel karsiligi var.
  // konteynerLambaManuel: Kontrol sekmesinden elle ac/kapa - otomatik alarm
  // davranisiyla OR'lanir (alarm sururken elle kapatilsa bile alarm onu
  // yeniden yakar - siren gibi tamamen otomatiklestirilmedi, bilincli tercih:
  // kullanici "lambayi ac" dedigi surece, alarm bitse bile yanik kalsin).
  konteynerSirenAktif = konteynerBuzzerVar;
  // Batarya kritikse (bateryaKritik) lamba ZORLA kapatilir - guvenlik/alarm
  // cikisi olmadigindan (sadece aydinlatma) yuk kesme kapsaminda; siren
  // KASITLI OLARAK dokunulmuyor, her kosulda calismaya devam eder. Voltaj
  // toparlaninca bir sonraki dongude otomatik geri acilir - ayri bir "geri
  // ac" mantigi gerekmez, konteynerLambaManuel'e de dokunulmaz.
  konteynerLambaAktif = (konteynerAlarmLambaHam || konteynerLambaManuel) && !bateryaKritik;

  // Lamba pirpir animasyonu (tetik animasyonunun gorsel kismi): SADECE
  // animasyonlu grupta (gaz haric) ve episode'un ilk KONTEYNER_TETIK_LAMBA_FLASH_TOPLAM_MS'inde,
  // konteynerLambaAktif'in PIN CIKISI gecici olarak hizli yanip-sonmeye
  // override edilir - alttaki konteynerLambaAktif/min-max sure durumu
  // DEGISMEZ, sadece bu pencerede digitalWrite farkli yazilir.
  bool konteynerLambaPinAc = konteynerLambaAktif;
  if (konteynerAnimasyonluTetik && konteynerSirenEpisodeMs != 0) {
    unsigned long gecenMs = millis() - konteynerSirenEpisodeMs;
    if (gecenMs < KONTEYNER_TETIK_LAMBA_FLASH_TOPLAM_MS) {
      konteynerLambaPinAc = ((gecenMs / (KONTEYNER_TETIK_LAMBA_FLASH_PERIYOT_MS / 2)) % 2) == 0;
    }
  }
  digitalWrite(KONTEYNER_SIREN_PIN, konteynerSirenAktif ? HIGH : LOW);
  digitalWrite(KONTEYNER_LAMBA_PIN, konteynerLambaPinAc ? HIGH : LOW);

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
    // FIX: 1000ms "kisa bip" kullanicinin bahcede gozlemledigi kadar (~4-5sn)
    // uzun degildi ama yine de "tek kisa bip" hissi vermiyordu - 150ms'e
    // indirildi, gercek bir "bip" gibi hissettirsin, uzatilmis buzz gibi degil.
    if (millis() - onBipBaslangicMs >= 150) {
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
  bool kapiOncekiDurum = kapi2Acik;
  kapi2Acik = (digitalRead(KAPI_REED_PIN) == HIGH);
  swanPirVar = (digitalRead(SWAN_PIR_PIN) == HIGH);
  // konteynerDumanVar artik burada degil, ayri gp2y10Poll() dongusunde (2sn'de
  // bir) guncelleniyor - bu yuzden "onceki durum" burada normal local degisken
  // yerine STATIC olmali, aksi halde her cagirida dumanOncekiDurum daima
  // konteynerDumanVar'a esit cikar (kenar/yukselis hic yakalanamaz, Onayli
  // moddaki anlik onay-bekleme tetiklemesi kirilir).
  static bool dumanOncekiDurum = false;
  bool dumanBuTur = konteynerDumanVar;
  pir2HareketVar = (digitalRead(PIR2_PIN) == HIGH);

  // Devre disi birakilan sensorler HAM okumaya devam eder (tani/gosterge
  // icin, kz-pir/kz-kapi/kz-swan alanlarinda gorunsun) ama hicbir karara
  // (onay bekleme/eskalasyon/reset) katkida bulunmaz - bkz konteynerSwanEtkin
  // yorumu (Swan PIR henuz baglanmadigindan surekli true okuyor, aksi halde
  // asagidaki reset kosulunu sonsuza kadar engellerdi).
  bool kapiEfektif = konteynerKapiEtkin && kapi2Acik;
  bool dumanEfektif = konteynerDumanEtkin && konteynerDumanVar;

  // Kapi ve Duman ANLIK/kesin tetikleyiciler (bir "onay suresi" beklemesi
  // gerekmiyor) - bu yuzden Onayli modda yukselen kenarda DOGRUDAN onay
  // beklemeye alinir. Swan PIR artik (tutma/onay 0 degilse) PIR2 gibi
  // "bolum" tabanli oldugundan bu ANLIK blokta DEGIL, asagidaki kendi
  // eskalasyon kontrolunde onay bekleniyor'u tetikler.
  // ONCEDEN BUG (kapi icin): bu blok hic yoktu, sadece PIR'in kendi "bolum"
  // takibi konteynerOnayBekleniyor'u set ediyordu - PIR hic tetiklenmeden
  // sadece kapi acilirsa Onayli modda alarm sessizce hicbir sey yapmiyordu.
  if (((kapiEfektif && !kapiOncekiDurum) || (dumanEfektif && !dumanOncekiDurum)) && alarmStatus.mode == 3) {
    konteynerOnayBekleniyor = true;
  }
  dumanOncekiDurum = dumanBuTur; // bir sonraki cagiri icin sakla

  // Swan PIR - PIR2 ile AYNI Tutma/Onay Suresi mantigi (bkz konteynerSwanTutmaSaniye
  // yorumu). Devre disi oldugunda veya "sifir/sifir" (varsayilan, eski davranis)
  // durumunda ANINDA eskale eder - PIR2'nin boot-grace'i burada YOK, Swan'in
  // isinma sorunu (HC-SR505'in aksine) olmadigindan gerekmiyor.
  if (konteynerSwanEtkin) {
    if (swanPirVar && konteynerSwanBolumBaslangicMs == 0) {
      konteynerSwanBolumBaslangicMs = millis();
      konteynerSwanEskalasyonOldu = false;
    }
    if (swanPirVar) konteynerSwanSonHareketMs = millis();
    unsigned long swanTutmaMs = (unsigned long)konteynerSwanTutmaSaniye * 1000UL;
    konteynerSwanAlarmVar = swanPirVar || (konteynerSwanSonHareketMs != 0 && (millis() - konteynerSwanSonHareketMs <= swanTutmaMs));

    if (konteynerSwanAlarmVar && konteynerSwanBolumBaslangicMs != 0 && !konteynerSwanEskalasyonOldu) {
      unsigned long swanOnaySuresiMs = (unsigned long)konteynerSwanOnaySaniye * 1000UL;
      if (millis() - konteynerSwanBolumBaslangicMs >= swanOnaySuresiMs) {
        konteynerSwanEskalasyonOldu = true;
        if (alarmStatus.mode == 3) konteynerOnayBekleniyor = true;
      }
    }
    if (!konteynerSwanAlarmVar && konteynerSwanBolumBaslangicMs != 0) {
      konteynerSwanBolumBaslangicMs = 0;
      konteynerSwanEskalasyonOldu = false;
    }
  } else {
    konteynerSwanAlarmVar = false;
    konteynerSwanBolumBaslangicMs = 0;
    konteynerSwanEskalasyonOldu = false;
  }
  bool swanEfektif = konteynerSwanEtkin && konteynerSwanEskalasyonOldu;

  if (konteynerPirEtkin) {
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
    // konteynerPirSonHareketMs==0 => sentinel (hic hareket gorulmedi), 0'i
    // gercek bir zaman gibi yorumlamamak icin acikca kontrol ediliyor.
    konteynerPirAlarmVar = pir2HareketVar || (konteynerPirSonHareketMs != 0 && (millis() - konteynerPirSonHareketMs <= tutmaMs));

    // ESKALASYON: bolum, Onay Suresi'ni kesintisiz astiysa artik GERCEK alarm
    // (boot-grace penceresi icinde bilerek engellenir).
    if (konteynerPirAlarmVar && konteynerPirBolumBaslangicMs != 0 && !konteynerPirEskalasyonOldu) {
      unsigned long onaySuresiMs = (unsigned long)konteynerPirOnaySaniye * 1000UL;
      if (millis() - konteynerPirBolumBaslangicMs > onaySuresiMs && millis() >= KONTEYNER_PIR_BOOT_GRACE_MS) {
        konteynerPirEskalasyonOldu = true;
        if (alarmStatus.mode == 3) konteynerOnayBekleniyor = true;
      }
    }

    // PIR bolumu (kendi ic sayaci) bitti mi.
    if (!konteynerPirAlarmVar && konteynerPirBolumBaslangicMs != 0) {
      konteynerPirBolumBaslangicMs = 0;
      konteynerPirEskalasyonOldu = false;
    }
  } else {
    // PIR2 devre disi - eskalasyon durumunu tamamen sifirla, ham pir2HareketVar
    // (gosterge) etkilenmez.
    konteynerPirAlarmVar = false;
    konteynerPirBolumBaslangicMs = 0;
    konteynerPirEskalasyonOldu = false;
  }

  // Onay bayraklari: ONCEDEN sadece yukaridaki PIR-bolum kosuluna bagliydi -
  // kapidan gelen bir onay hic sifirlanmiyordu (kapi bolum takibine dahil
  // degildi). Artik NE PIR NE kapi NE Swan PIR bolumu aktif degilse (ucu de
  // kapandi/bitti) sifirlanir - hangi kaynaktan gelirse gelsin dogru zamanda
  // temizlenir. Swan icin BOLUM durumu (konteynerSwanAlarmVar) kullanilir,
  // sadece eskalasyon degil - PIR2'deki konteynerPirAlarmVar ile AYNI mantik
  // (onay suresi dolmadan bolum devam ederken erken sifirlanmasin diye).
  if (!konteynerPirAlarmVar && !kapiEfektif && !(konteynerSwanEtkin && konteynerSwanAlarmVar)) {
    konteynerOnayBekleniyor = false;
    konteynerOnayVerildi = false;
    konteynerLambaOnayVerildi = false;
    konteynerSusturuldu = false; // susturma da bolumle birlikte biter, sonraki tetiklenme sessiz kalmasin
  }
}

// ============================================================
// IR KUMANDA - HAM KENAR YAKALAMA (kutuphanesiz, kendi yazdigimiz)
// ============================================================
// GECMIS: Once IRremote kutuphanesi kullanildi, ama protokol tanima +
// "repeat kare" + "UNKNOWN/gurultu" mantigi ust uste birkac farkli
// kararsizliga yol acti (0x0 yanlis pozitif, tanidigi tus sayisi 2'de
// tikanma, basili tutunca "tanıyor", birakinca kayboluyor - kutuphanenin
// ic "son protokol" hafizasindan kalintı). Ayrica bu kartta IRremote'un
// LED-feedback pini gecersiz bir GPIO'ya (0xFF) dusup DAHA ONCE de
// tekrarlayan brownout resetlerine yol acmisti. Kullanicinin acik talebiyle kutuphane tamamen kaldirildi,
// yerine cok daha basit/seffaf bir yontem geldi: NEC/RC5/SIRC gibi HERHANGI
// bir protokolu "cozmeye" calismadan, IR alicinin sinyal pininde HER
// seviye degisimini (kenar) bir donanim interrupt'i ile mikrosaniye
// hassasiyetinde zaman damgalayip bir tampona yaziyoruz. Yeterince uzun
// bir sessizlik (IR_FRAME_GAP_US) gorulunce "kare bitti" sayilip, tum
// kenar araliklarindan (protokol ne olursa olsun) kararli bir hash
// uretiliyor - bu hash, o tusun "kodu" oluyor. Ayni tusun ardisik
// basimlarindaki birkac-yuz-mikrosaniyelik dogal jitter'i tolere etmek
// icin araliklar kovalara (200us) yuvarlaniyor.
#define IR_RAW_MAX 200          // bir karede beklenen en fazla kenar sayisi
#define IR_FRAME_GAP_US 15000UL // bu kadar sessizlik = kare bitti
#define IR_MIN_EDGES 10         // bundan az kenar = gercek IR gurultusu, at
#define IR_LOCKOUT_MS 400       // tusa basili tutarken gelen tekrar darbelerini yut (eski "repeat" filtresiyle ayni amac)

volatile uint16_t irRawBuf[IR_RAW_MAX];
volatile uint16_t irRawLen = 0;
volatile uint32_t irSonKenarMicros = 0;
portMUX_TYPE irMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR irKenarISR() {
  uint32_t simdi = micros();
  uint32_t fark = simdi - irSonKenarMicros;
  irSonKenarMicros = simdi;
  portENTER_CRITICAL_ISR(&irMux);
  if (fark > IR_FRAME_GAP_US) {
    irRawLen = 0; // yeni kare basliyor - onceki (anlamsiz uzun) sessizligi kaydetme
  } else if (irRawLen < IR_RAW_MAX) {
    irRawBuf[irRawLen++] = (fark > 65535UL) ? 65535 : (uint16_t)fark;
  }
  portEXIT_CRITICAL_ISR(&irMux);
}

void irAliciBaslat() {
  pinMode(IR_RECV_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(IR_RECV_PIN), irKenarISR, CHANGE);
}

// Gercek eslesme/ogrenme-modu isleme (asagidaki irEslesmeler tablosu ve
// komutCalistir()) dosyada daha ileride tanimli oldugundan, burada sadece
// kodu paylasilan degiskenlere yazip birakiyor - loop() hemen ardindan
// irKomutIsleVeCalistir()'i cagirir.
volatile bool irYeniKodVar = false;
uint32_t irSonKod = 0;
// TESHIS AMACLI: web'den ("Kumanda" sekmesi ogrenme durumu) seri kabloya
// ihtiyac olmadan neler oldugunu gormek icin.
String irSonDenemeProtokol = "-";
uint32_t irDenemeSayaci = 0;
uint16_t irSonRawlen = 0;

void irKumandaIsle() {
  static uint32_t sonIslemMs = 0;
  static uint16_t localBuf[IR_RAW_MAX];
  uint16_t localLen;

  portENTER_CRITICAL(&irMux);
  localLen = irRawLen;
  for (uint16_t i = 0; i < localLen; i++) localBuf[i] = irRawBuf[i];
  portEXIT_CRITICAL(&irMux);

  if (localLen == 0) return;
  if ((micros() - irSonKenarMicros) < IR_FRAME_GAP_US) return; // kare hala devam ediyor, bekle

  portENTER_CRITICAL(&irMux);
  irRawLen = 0; // kare bitti - tamponu hemen bosalt ki bir sonraki kare biriksin
  portEXIT_CRITICAL(&irMux);

  irDenemeSayaci++;
  irSonRawlen = localLen;

  if (millis() - sonIslemMs < IR_LOCKOUT_MS) {
    irSonDenemeProtokol = "tekrar/kilit";
    return; // tusa basili tutma sirasindaki tekrar kareleri - yoksay
  }
  if (localLen < IR_MIN_EDGES) {
    irSonDenemeProtokol = "gurultu(kisa)";
    return; // gercek IR gurultusu (gunes isigi, floresan vb.)
  }

  uint32_t hash = 2166136261UL; // FNV-1a
  for (uint16_t i = 0; i < localLen; i++) {
    uint16_t kova = localBuf[i] / 200; // ~200us kovaya yuvarla, jitter toleransi
    hash ^= kova;
    hash *= 16777619UL;
  }
  hash ^= localLen;
  hash *= 16777619UL;

  irSonKod = hash;
  irSonDenemeProtokol = "HAM(" + String(localLen) + " kenar)";
  irYeniKodVar = true;
  sonIslemMs = millis();
  DEBUG_PRINT("[IR] Ham kod: 0x");
  DEBUG_PRINTLN(String(irSonKod, HEX));
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

const char* alarmTetikleyiciAdlari[6] = {"Sudepo: Sol Kapi", "Sudepo: Sag Kapi", "Sudepo: PIR (Hareket)", "Sudepo: Su Seviyesi", "Sudepo: Kacak", "Sudepo: Sensor Hatasi"};

// konteynerPir/konteynerKapi: ESP8266'nin trigger_mask'inden BAGIMSIZ,
// ESP32'nin yerel Konteyner sensorleri (PIR2 + kapi reed) - ESP8266
// bitmask'ina karistirilmiyor (o mask ESP8266'nin kendi kodlamasi, ileride
// cakisma riski olmasin diye ayri tutuluyor).
String alarmTetikleyenMetni(uint8_t mask, bool panik, bool konteynerPir = false, bool konteynerKapi = false, bool konteynerSwan = false, bool konteynerDuman = false, bool konteynerGaz = false) {
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
    s += "Konteyner: HC505-1 PIR";
  }
  if (konteynerKapi) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner: Kapı";
  }
  if (konteynerSwan) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner: Swan PIR";
  }
  if (konteynerDuman) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner: Duman";
  }
  if (konteynerGaz) {
    if (s.length() > 0) s += ", ";
    s += "Konteyner: Gaz (MQ6)";
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
  bool konteynerPirVar = konteynerAlarmEtkin && konteynerPirEtkin && konteynerPirEskalasyonOldu;
  bool konteynerKapiVar = konteynerAlarmEtkin && konteynerKapiEtkin && kapi2Acik;
  bool konteynerSwanVar = konteynerAlarmEtkin && konteynerSwanEtkin && konteynerSwanEskalasyonOldu;
  bool konteynerDumanTetik = konteynerAlarmEtkin && konteynerDumanEtkin && konteynerDumanVar;
  // Gaz (MQ6), panik gibi konteynerAlarmEtkin'den BAGIMSIZ (bkz alarmLedGuncelle
  // konteynerAcilDurum) - burada da ayni sekilde zon ac/kapa anahtarina bakmaz.
  bool konteynerGazTetik = konteynerGazEtkin && konteynerGazVar;
  bool anyAlarm = (alarmStatus.enabled && mask != 0) || alarmStatus.panic_mode || konteynerPirVar || konteynerKapiVar || konteynerSwanVar || konteynerDumanTetik || konteynerGazTetik;
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
    telegramBekleyenMetin = "🌱 SuDepo: " + baslik + " | Tetikleyen: " + alarmTetikleyenMetni(mask, alarmStatus.panic_mode, konteynerPirVar, konteynerKapiVar, konteynerSwanVar, konteynerDumanTetik, konteynerGazTetik);
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

// ===== Gunluk Alarm Logu (2026-08-27 kullanici talebi) =====
// Telegram ac/kapa anahtarindan BAGIMSIZ - kullanici bildirimleri kapatsa
// bile log tutulmaya devam eder. Son 5 kayit RAM'de (dashboard'daki "Son 5
// Alarm" listesi icin, aninda erisim), TUMU SPIFFS'e (/alarm_log.csv) kalici
// olarak eklenir - reboot'a dayanikli "gunluk" gecmis.
#define ALARM_LOG_RAM_ADET 5
#define ALARM_LOG_DOSYASI "/alarm_log.csv"
#define ALARM_LOG_MAX_SATIR 300 // SPIFFS sinirli - asilirsa en eski yarisi silinir

struct AlarmLogKaydi { String zaman; String baslik; String tetikleyen; };
AlarmLogKaydi alarmLogRAM[ALARM_LOG_RAM_ADET];
uint8_t alarmLogRAMDolu = 0;
bool alarmLogOncekiVar = false;

void alarmLoguDosyayaEkle(const String& satir) {
  if (SPIFFS.exists(ALARM_LOG_DOSYASI)) {
    File f = SPIFFS.open(ALARM_LOG_DOSYASI, "r");
    int satirSayisi = 0;
    if (f) {
      while (f.available()) { f.readStringUntil('\n'); satirSayisi++; }
      f.close();
    }
    if (satirSayisi >= ALARM_LOG_MAX_SATIR) {
      File fr = SPIFFS.open(ALARM_LOG_DOSYASI, "r");
      String kalanlar;
      int atlanacak = satirSayisi / 2, i = 0;
      while (fr.available()) {
        String s = fr.readStringUntil('\n');
        if (i++ >= atlanacak) { kalanlar += s; kalanlar += "\n"; }
      }
      fr.close();
      File fw = SPIFFS.open(ALARM_LOG_DOSYASI, "w");
      if (fw) { fw.print(kalanlar); fw.close(); }
    }
  }
  File f = SPIFFS.open(ALARM_LOG_DOSYASI, "a");
  if (f) { f.println(satir); f.close(); }
}

void alarmLoguKaydet(const String& baslik, const String& tetikleyen) {
  String zaman = (zamanCacheStr == "-") ? "Bilinmiyor" : zamanCacheStr;
  for (int i = ALARM_LOG_RAM_ADET - 1; i > 0; i--) alarmLogRAM[i] = alarmLogRAM[i - 1];
  alarmLogRAM[0] = { zaman, baslik, tetikleyen };
  if (alarmLogRAMDolu < ALARM_LOG_RAM_ADET) alarmLogRAMDolu++;

  String satir = zaman + "," + baslik + "," + tetikleyen;
  satir.replace("\n", " "); satir.replace("\r", " "); satir.replace(",,", ", ,");
  alarmLoguDosyayaEkle(satir);
}

// telegramAlarmKontrolEt() ile AYNI "yeni alarm basladi" tespiti - bilerek
// tekrarlanan kucuk bir hesap (ortak bir fonksiyona cikarmak yerine), ikisi
// birbirinden BAGIMSIZ calissin diye (Telegram kapaliyken bile log tutulsun).
void alarmLoguKontrolEt() {
  uint8_t mask = alarmStatus.trigger_mask;
  bool konteynerPirVar = konteynerAlarmEtkin && konteynerPirEtkin && konteynerPirEskalasyonOldu;
  bool konteynerKapiVar = konteynerAlarmEtkin && konteynerKapiEtkin && kapi2Acik;
  bool konteynerSwanVar = konteynerAlarmEtkin && konteynerSwanEtkin && konteynerSwanEskalasyonOldu;
  bool konteynerDumanTetik = konteynerAlarmEtkin && konteynerDumanEtkin && konteynerDumanVar;
  bool konteynerGazTetik = konteynerGazEtkin && konteynerGazVar;
  bool anyAlarm = (alarmStatus.enabled && mask != 0) || alarmStatus.panic_mode || konteynerPirVar || konteynerKapiVar || konteynerSwanVar || konteynerDumanTetik || konteynerGazTetik;
  bool alarmVar = anyAlarm || alarmStatus.pending;

  if (alarmVar && !alarmLogOncekiVar) {
    String baslik = alarmStatus.panic_mode ? "PANIK AKTIF" : (alarmStatus.pending ? "ALARM - Onay Bekliyor" : "ALARM TETIKLENDI");
    String tetikleyen = alarmTetikleyenMetni(mask, alarmStatus.panic_mode, konteynerPirVar, konteynerKapiVar, konteynerSwanVar, konteynerDumanTetik, konteynerGazTetik);
    alarmLoguKaydet(baslik, tetikleyen);
  }
  alarmLogOncekiVar = alarmVar;
}

// anaGucPoll() (asagida tanimli, ADS1115 ana guc izleme) tarafindan set edilir.
bool anaGucBildirimBekliyor = false;
String anaGucBildirimMetni = "";

// bateryaKritik gecisinde (acilis/kapanis) bir kez bildirim gonderir - ayni
// bekle/retry deseni telegramAlarmKontrolEt ile ayni, ayri degiskenlerle.
void telegramBateryaKontrolEt() {
  static bool oncekiKritik = false;
  static bool bekliyor = false;
  static String metin;
  static unsigned long ilkDenemeMs = 0;

  if (!telegramBildirimAktif) { oncekiKritik = bateryaKritik; bekliyor = false; return; }

  if (bateryaKritik && !oncekiKritik) {
    MpptKilit kilit;
    metin = "🔋 SuDepo: DUSUK AKU (" + String(mpptData.battery_voltage, 1) + "V) - sulama ve konteyner lambasi devre disi birakildi";
    bekliyor = true; ilkDenemeMs = millis();
  } else if (!bateryaKritik && oncekiKritik) {
    MpptKilit kilit;
    metin = "🔋 SuDepo: Aku normale dondu (" + String(mpptData.battery_voltage, 1) + "V) - yukler tekrar aktif";
    bekliyor = true; ilkDenemeMs = millis();
  }
  oncekiKritik = bateryaKritik;

  if (bekliyor) {
    if (telegramMesajGonder(metin)) {
      bekliyor = false;
    } else if (millis() - ilkDenemeMs > TELEGRAM_RETRY_SURESI_MS) {
      bekliyor = false;
    }
  }
}

// Ana guc kademe gecisinde (anaGucPoll) bir kez bildirim gonderir - ayni
// bekle/retry deseni telegramBateryaKontrolEt ile ayni.
void telegramAnaGucKontrolEt() {
  static bool bekliyor = false;
  static String metin;
  static unsigned long ilkDenemeMs = 0;

  if (!telegramBildirimAktif) { anaGucBildirimBekliyor = false; bekliyor = false; return; }

  if (anaGucBildirimBekliyor) {
    anaGucBildirimBekliyor = false;
    metin = anaGucBildirimMetni;
    bekliyor = true; ilkDenemeMs = millis();
  }

  if (bekliyor) {
    if (telegramMesajGonder(metin)) {
      bekliyor = false;
    } else if (millis() - ilkDenemeMs > TELEGRAM_RETRY_SURESI_MS) {
      bekliyor = false;
    }
  }
}

// Konteyner sireni KONTEYNER_SIREN_MAX_MS'i asip otomatik susturuldugunda
// bir kez bildirim gonderir - ayni bekle/retry deseni telegramBateryaKontrolEt ile ayni.
void telegramKonteynerOtoSusturKontrolEt() {
  static bool bekliyor = false;
  static String metin;
  static unsigned long ilkDenemeMs = 0;

  if (!telegramBildirimAktif) { konteynerOtoSusturBildirimBekliyor = false; bekliyor = false; return; }

  if (konteynerOtoSusturBildirimBekliyor) {
    konteynerOtoSusturBildirimBekliyor = false;
    metin = "📦 Konteyner: Siren " + String(KONTEYNER_SIREN_MAX_MS / 60000UL) + " dakikadir kesintisiz caldigi icin otomatik susturuldu - sensoru kontrol edin";
    bekliyor = true; ilkDenemeMs = millis();
  }

  if (bekliyor) {
    if (telegramMesajGonder(metin)) {
      bekliyor = false;
    } else if (millis() - ilkDenemeMs > TELEGRAM_RETRY_SURESI_MS) {
      bekliyor = false;
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
void guvenliRestart(); // bkz tanimi asagida - BLE/WiFi'yi duzgunce kapatip ESP.restart() cagirir

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
    } else if (key == "BATTERY_LOW") {
      // ESP8266'nin kendi uyguladigi durum - bateryaKritik'in gonderdigimiz
      // komutun gercekten uygulandigini dogrulamasi icin (round-trip).
      esp8266BatteryLowAck = (value == "1");
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

void rs485_poll() {
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
// MPPT UART2 (RS232 / PI30-Voltronic protokolu) - AKU/PV/YUK OKUMA
// ============================================================
// FAZ 1 (bring-up): sadece Serial Monitor'a loglar, hicbir kesme/UI
// entegrasyonu YOK - kablolama (TX/RX pinleri, bkz config.h) sahada
// dogrulanana kadar hicbir aksiyon bu degere bagli olmamali.
//
// Mevcut ESP8266 RS485 hattindan (Serial1/UART1, RS485Kilit mutex'i)
// TAMAMEN AYRI bir bus/protokol - RS232, MAX3232 (RS232-TTL) uzerinden
// UART2. RS485 DEGIL, DE/RE yon pini YOK - full-duplex. BLE gorevi bu
// hatta hic dokunmadigindan mutex'e ihtiyac YOK.
HardwareSerial MpptSerial(MPPT_UART_NUM);

// PI30 protokolunun resmi CRC16 hesaplama yontemi ("Axpert...RS232
// Protocol" dokumaninin "4.1 CRC calibration method" eki - topluluk
// implementasyonlarinda (mpp-solar vb.) da ayni tablo/algoritma kullanilir).
// CRC ciktisinin herhangi bir byte'i '(' (0x28) veya <cr>/<lf> ile
// cakisirsa +1 kaydirilir, boylece cevap ayirstirken CRC baytlari veri
// govdesiyle karismaz.
static const uint16_t MPPT_CRC_TABLE[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

uint16_t mpptCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    uint8_t da = (uint8_t)(crc >> 8) >> 4;
    crc <<= 4;
    crc ^= MPPT_CRC_TABLE[da ^ (data[i] >> 4)];
    da = (uint8_t)(crc >> 8) >> 4;
    crc <<= 4;
    crc ^= MPPT_CRC_TABLE[da ^ (data[i] & 0x0f)];
  }
  uint8_t low = crc & 0xff;
  uint8_t high = (crc >> 8) & 0xff;
  if (low == 0x28 || low == 0x0d || low == 0x0a) low++;
  if (high == 0x28 || high == 0x0d || high == 0x0a) high++;
  return ((uint16_t)high << 8) | low;
}

// "QPIGS" gibi bir PI30 komutunu CRC16 + <cr> ekleyip gonderir, cihazin
// "(<veri><CRC><cr>" seklindeki cevabini okur. Basariliysa cevap govdesini
// (basindaki '(' ve sondaki 2 CRC byte + <cr> cikarilmis halde) 'out'a
// yazar ve true doner. Donanim/kablo henuz sahada dogrulanmadigi icin
// zaman asimi MPPT_RESPONSE_TIMEOUT_MS ile sinirli - loop() bu fonksiyonu
// hic cagirmaz, sadece ayri mpptTask() cagirir (bkz asagisi).
bool mpptSendCommand(const char *cmd, String &out) {
  size_t cmdLen = strlen(cmd);
  uint16_t crc = mpptCrc16((const uint8_t *)cmd, cmdLen);
  while (MpptSerial.available()) MpptSerial.read(); // eski/artik veriyi temizle

  MpptSerial.write((const uint8_t *)cmd, cmdLen);
  MpptSerial.write((uint8_t)(crc >> 8));
  MpptSerial.write((uint8_t)(crc & 0xff));
  MpptSerial.write('\r');
  MpptSerial.flush();

  String resp;
  unsigned long start = millis();
  bool gotCr = false;
  while (millis() - start < MPPT_RESPONSE_TIMEOUT_MS && !gotCr) {
    while (MpptSerial.available()) {
      char c = (char)MpptSerial.read();
      resp += c;
      if (c == '\r') { gotCr = true; break; }
    }
  }

  if (!gotCr || resp.length() < 4 || resp[0] != '(') return false;
  out = resp.substring(1, resp.length() - 3); // '(' ve son 2 CRC byte + <cr> cikarildi
  return true;
}

void mpptPoll(); // asagida tanimli, mpptTask() tarafindan cagriliyor

// Cihaz yanit vermezse istek basina MPPT_RESPONSE_TIMEOUT_MS kadar bloke
// olabilir - donanim henuz sahada dogrulanip/baglanmadan bu, loop()'u
// (web/RS485/buton islenmesi) her poll dongusunde donduruyordu ("tepkilerde
// belirgin yavaslama"). Bu yuzden ayri, dusuk oncelikli bir FreeRTOS
// task'ta calisiyor - loop() asla beklemez.
void mpptTask(void *pv) {
  for (;;) {
    mpptPoll();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void mppt_init() {
  MpptSerial.begin(MPPT_BAUDRATE, SERIAL_8N1, MPPT_UART_RX_PIN, MPPT_UART_TX_PIN);
  mpptDataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(mpptTask, "mpptTask", 4096, NULL, 1, NULL, 0);
  DEBUG_PRINTLN("[MPPT] UART2 RS232/PI30 hazir, ayri task'ta (FAZ 1 - sadece loglama)");
}

// ============================================================
// YEDEK AKU (ADC voltaj izleme) - 3x 12V 20Ah kullanilmis scooter akusu,
// PARALEL baglanip tek bir 12V ~60Ah yedek banka olusturur, SADECE dusuk
// guclu alarm elektroniginin 5V hattina TEK YONLU diyot-OR ile yedek olur
// (bkz config.h "Yedek Aku" bloğu). Deşarj (yedek besleme) tamamen PASIF
// donanimsal failover - kod hicbir role/yuk kesme kararini bu degere
// BAGLAMAZ. SARJ ARTIK Schulzz PWM solar sarj kontrolcusu tarafindan
// donanimsal yapiliyor (2026-08-24) - eski GPIO21 sarj rolesi mantigi
// TAMAMEN KALDIRILDI, bu fonksiyon artik SADECE izleme/gosterge amacli.
// ============================================================
struct YedekAkuData {
  float voltaj = 0.0;
  bool read_ok = false;
  unsigned long last_update_ms = 0;
};
YedekAkuData yedekAkuData;

const char* yedekAkuDurumMetni() {
  if (!yedekAkuData.read_ok) return "bilinmiyor";
  if (yedekAkuData.voltaj >= YEDEK_AKU_DOLU_V) return "dolu";
  if (yedekAkuData.voltaj <= YEDEK_AKU_ZAYIF_V) return "zayif";
  return "devrede";
}

void yedekAkuPoll() {
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < YEDEK_AKU_POLL_INTERVAL_MS) return;
  lastPoll = millis();

  int raw = analogRead(YEDEK_AKU_ADC_PIN);
  float adcVolt = (raw / 4095.0f) * 3.3f;
  float voltajHam = adcVolt * YEDEK_AKU_ADC_OLCEK;
  yedekAkuData.voltaj = (voltajHam < YEDEK_AKU_OLU_BOLGE_V) ? 0.0f : voltajHam * YEDEK_AKU_KALIBRASYON_KAZANC;
  yedekAkuData.read_ok = true;
  yedekAkuData.last_update_ms = millis();

  DEBUG_PRINT("[YedekAku] V="); DEBUG_PRINT(String(yedekAkuData.voltaj, 2));
  DEBUG_PRINT(" durum="); DEBUG_PRINTLN(yedekAkuDurumMetni());
}

// ============================================================
// ANA GUC IZLEME (ADS1115, I2C harici ADC) - 24V ana hattin 3 kademeli
// (Dusuk/Kritik/Acil) SADECE BILDIRIM amacli izlenmesi. Hicbir role/yuk
// otomatik kesilmez/acilmaz - kullanici karari: "ihtiyac duyarsam acarim"
// (bkz acilLambaManuel). Manuel I2C protokolu (kutuphanesiz, proje deseni -
// ahtOku() ile ayni tarzda): config register'a 2 byte yazip tek-atis
// (single-shot) donusum baslatilir, kisa bekleme sonrasi conversion
// register'dan 2 byte okunur.
// ============================================================
struct AnaGucData {
  float voltaj = 0.0f;
  bool read_ok = false;
  int kademe = 0; // 0=normal, 1=dusuk, 2=kritik, 3=acil
  unsigned long last_update_ms = 0;
};
AnaGucData anaGucData;

float anaGucEsik1Volt = ANA_GUC_ESIK1_V;
float anaGucEsik2Volt = ANA_GUC_ESIK2_V;
float anaGucEsik3Volt = ANA_GUC_ESIK3_V;

void anaGucEsikYukle() {
  ayarPrefs.begin("ayarlar", true);
  anaGucEsik1Volt = ayarPrefs.getFloat("ag_esik1", ANA_GUC_ESIK1_V);
  anaGucEsik2Volt = ayarPrefs.getFloat("ag_esik2", ANA_GUC_ESIK2_V);
  anaGucEsik3Volt = ayarPrefs.getFloat("ag_esik3", ANA_GUC_ESIK3_V);
  ayarPrefs.end();
}

void anaGucEsikKaydet(float e1, float e2, float e3) {
  anaGucEsik1Volt = e1; anaGucEsik2Volt = e2; anaGucEsik3Volt = e3;
  ayarPrefs.begin("ayarlar", false);
  ayarPrefs.putFloat("ag_esik1", e1);
  ayarPrefs.putFloat("ag_esik2", e2);
  ayarPrefs.putFloat("ag_esik3", e3);
  ayarPrefs.end();
}

// Tek-atis donusum baslat (AIN0 vs GND, PGA +-4.096V, 128SPS) ve ham 16-bit
// sonucu dondur. Basarisizsa false doner (I2C ACK alinamadi).
bool ads1115TekOlcum(int16_t &sonucOut) {
  Wire.beginTransmission(ADS1115_I2C_ADDR);
  Wire.write(0x01); // config register
  // MSB: OS=1(baslat) MUX=100(AIN0-GND) PGA=001(+-4.096V) MODE=1(tek-atis)
  Wire.write(0b11000011);
  // LSB: DR=100(128SPS) COMP_MODE=0 COMP_POL=0 COMP_LAT=0 COMP_QUE=11(devre disi)
  Wire.write(0b10000011);
  if (Wire.endTransmission() != 0) return false;

  delay(9); // 128SPS ~7.8ms/ornek + guvenlik payi

  Wire.beginTransmission(ADS1115_I2C_ADDR);
  Wire.write(0x00); // conversion register
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)ADS1115_I2C_ADDR, (uint8_t)2) != 2) return false;
  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();
  sonucOut = (int16_t)((msb << 8) | lsb);
  return true;
}

void anaGucPoll() {
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < ANA_GUC_POLL_INTERVAL_MS) return;
  lastPoll = millis();

  int16_t raw;
  if (!ads1115TekOlcum(raw)) {
    anaGucData.read_ok = false;
    return;
  }
  float adsVolt = raw * (4.096f / 32768.0f); // PGA +-4.096V, 16-bit isaretli
  anaGucData.voltaj = adsVolt * ANA_GUC_BOLUCU_ORAN;
  anaGucData.read_ok = true;
  anaGucData.last_update_ms = millis();

  int yeniKademe;
  if (anaGucData.voltaj <= anaGucEsik3Volt) yeniKademe = 3;
  else if (anaGucData.voltaj <= anaGucEsik2Volt) yeniKademe = 2;
  else if (anaGucData.voltaj <= anaGucEsik1Volt) yeniKademe = 1;
  else yeniKademe = 0;

  if (yeniKademe != anaGucData.kademe) {
    anaGucData.kademe = yeniKademe;
    if (yeniKademe > 0) {
      const char* kademeAdi = (yeniKademe == 3) ? "ACIL" : (yeniKademe == 2) ? "KRITIK" : "DUSUK";
      anaGucBildirimMetni = "⚡ Konteyner: Ana guc " + String(kademeAdi) + " seviyede (" + String(anaGucData.voltaj, 1) + "V) - acil durum lambasini gerekirse manuel acabilirsiniz";
      anaGucBildirimBekliyor = true;
    }
  }

  DEBUG_PRINT("[AnaGuc] V="); DEBUG_PRINT(String(anaGucData.voltaj, 2));
  DEBUG_PRINT(" kademe="); DEBUG_PRINTLN(anaGucData.kademe);
}

// ============================================================
// ACIL DURUM LAMBASI (GPIO21/ACIL_LAMBA_PIN, eski sarj rolesi MOSFET'i) -
// manuel web butonu VEYA panik/Konteyner-alarm durumunda DOGRUDAN/otomatik
// yanar (onay beklemeden - konteynerAcilDurum ile ayni oncelik). Ana guc
// dusuklugu SADECE bildirim gonderir, bu lambayi OTOMATIK tetiklemez -
// kullanici karari boyle ("ihtiyac duyarsam acarim").
// ============================================================
bool acilLambaManuel = false;
bool acilLambaAktif = false;

// konteynerAcilDurum (panik VEYA MQ6 gaz alarmi) alarmLedGuncelle() icinde
// hesaplaniyor (yerel degisken) - bu yuzden parametre olarak alinir, o
// fonksiyonun sonunda cagirilir.
void acilLambaGuncelle(bool konteynerAcilDurumParam) {
  acilLambaAktif = acilLambaManuel || alarmStatus.panic_mode || konteynerAcilDurumParam;
  digitalWrite(ACIL_LAMBA_PIN, acilLambaAktif ? HIGH : LOW);
}

bool panikTetikle(bool& panicActive, String& reply); // asagida tanimli (RS485 uzerinden ESP8266 ile senkron panik)

// Fiziksel Acil Durum Butonu (GPIO15, INPUT_PULLUP, aktif-LOW) - basisinda
// web arayuzundeki Panik butonuyla AYNI panikTetikle() cagrisini yapar
// (paralel calisir, ayni RS485 komutu, ayni ac/kapa toggle semantigi).
// Basit kenar debounce - PIR2/Kapi reed ile ayni tarzda ama tek-atis (edge)
// davranisi icin ek "onceki durum" degiskeni tutar.
// GUVENLIK: panikTetikle() RS485 uzerinden ACK bekler (1000ms x 3 deneme =
// en kotu ihtimalle ~3sn BLOKE eder). Kablo henuz disariya (SCART) tam
// cikarilmadigindan pin gevsek/gurultulu olabilir - salt 50ms kenar debounce'u
// boyle bir gurultuyu tam filtrelemeyebilir, art arda tetiklenirse loop() (ve
// web sunucusu) sn'lerce donuk kalir. Bu yuzden AYRICA bagimsiz bir soguma
// suresi (KONTEYNER_ACIL_BUTON_COOLDOWN_MS) var - kenar debounce'dan gecse
// bile gercek panikTetikle() cagrisi bu sure dolmadan tekrar calismaz.
void acilButonPoll() {
  static bool oncekiBasili = false;
  static unsigned long sonDegisimMs = 0;
  static unsigned long sonTetikMs = 0;
  bool basili = (digitalRead(ACIL_BUTON_PIN) == LOW);
  if (basili != oncekiBasili && millis() - sonDegisimMs > 50) { // 50ms debounce
    sonDegisimMs = millis();
    oncekiBasili = basili;
    Serial.printf("[ACIL_BUTON] kenar algilandi, basili=%d\n", basili);
    if (basili && millis() - sonTetikMs > KONTEYNER_ACIL_BUTON_COOLDOWN_MS) {
      sonTetikMs = millis();
      bool panicActive = false;
      String reply;
      Serial.println("[ACIL_BUTON] panikTetikle() cagriliyor...");
      bool ok = panikTetikle(panicActive, reply); // sadece basma aninda (basma->birakma degil) tetikle
      Serial.printf("[ACIL_BUTON] panikTetikle sonucu: rs485_ok=%d panicActive=%d panic_mode=%d\n", ok, panicActive, alarmStatus.panic_mode);
    }
  }
}

// ============================================================
// AHT10 (I2C sicaklik/nem) - Konteyner'e OZEL, ESP8266'nin hep 0.0 gonderdigi
// sensorData.temperature placeholder'iyla KARISTIRILMAZ (bkz config.h yorumu).
// Kutuphane KULLANILMIYOR - AHT10'un protokolu trivial: tetikle (0xAC 0x33
// 0x00 yaz), 80ms bekle, 6 byte oku, 20-bit ham deger -> sicaklik/nem.
// I2C okuma cok kisa surdugunden (birkac ms) ayri task/mutex GEREKMEZ.
// ============================================================
struct AhtData {
  float sicaklik = 0.0;
  float nem = 0.0;
  bool read_ok = false;
  unsigned long last_update_ms = 0;
};
AhtData ahtData;

bool ahtOku(float &sicaklikOut, float &nemOut) {
  Wire.beginTransmission(AHT10_I2C_ADDR);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  delay(80); // AHT10 olcum suresi (datasheet: >=75ms)

  if (Wire.requestFrom((int)AHT10_I2C_ADDR, 6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  if (b[0] & 0x80) return false; // busy biti hala 1 ise olcum hazir degil

  uint32_t humRaw = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t tempRaw = (((uint32_t)b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];

  nemOut = (humRaw / 1048576.0f) * 100.0f;       // 2^20 = 1048576
  sicaklikOut = (tempRaw / 1048576.0f) * 200.0f - 50.0f;
  return true;
}

void ahtPoll() {
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < AHT10_POLL_INTERVAL_MS) return;
  lastPoll = millis();

  float sicaklik, nem;
  if (ahtOku(sicaklik, nem)) {
    ahtData.sicaklik = sicaklik;
    ahtData.nem = nem;
    ahtData.read_ok = true;
    ahtData.last_update_ms = millis();
  } else {
    ahtData.read_ok = false;
  }

  DEBUG_PRINT("[AHT10] Sicaklik="); DEBUG_PRINT(String(ahtData.sicaklik, 1));
  DEBUG_PRINT(" Nem="); DEBUG_PRINT(String(ahtData.nem, 1));
  DEBUG_PRINT(" ok="); DEBUG_PRINTLN(ahtData.read_ok ? "1" : "0");
}

// ============================================================
// MQ6 (analog gaz sensoru) - artik alarma BAGLI (kullanici talebi: parlayici/
// patlayici gaz riski nedeniyle gaz esigi asilinca Konteyner alarmini
// tetikler, bkz alarmLedGuncelle() konteynerGazVar kullanimi). Esik
// konteynerGazEsikVolt (NVS, web'den ayarlanabilir), etkin/pasif anahtari
// konteynerGazEtkin.
// ============================================================
struct Mq6Data {
  int raw = 0;
  float volt = 0.0;
  unsigned long last_update_ms = 0;
  bool powered = false; // MQ6_POWER_PIN'in guncel durumu (durumJson icin, kullaniciya "neden deger degismiyor" belirsizligini onlemek icin)
};
Mq6Data mq6Data;

// Test Modu (2026-08-25, kullanici talebi): acilince MQ6'nin 10dk/60sn guc
// dongusu ATLANIR, isitici SUREKLI acik kalir - sahada gaz/cakmak testi
// yaparken "canli mi eski mi" belirsizligini ortadan kaldirir. Kalici
// DEGIL (NVS'e yazilmaz, reset sonrasi false'a doner) - testten sonra
// kapatilmasi UNUTULURSA bile pil tuketimi reset ile kendiliginden biter.
bool konteynerMq6TestModu = false;

// Guc dongusu: MQ6_POWER_PIN uzerinden MQ6'e MQ6_POWER_ON_MS kadar guc
// verilir (isinma+olcum penceresi), o pencere icinde MQ6_POLL_INTERVAL_MS'de
// bir analogRead alinir, pencere bitince guc kesilir. Kapali kaldigi surece
// mq6Data/konteynerGazVar SON OLCULEN degeri korur (diger Konteyner
// sensorlerindeki "son bilinen deger" deseniyle tutarli).
// 2026-08-26 (kullanici talebi): dongu periyodu ADAPTIF - gaz alarmi icin
// algilama gecikmesi (dongu KAPALI kaldigi sure) gunduz/bol-enerji saatlerinde
// risklidir, gece/dusuk akude ise pil tasarrufu onceliklidir:
//   - Ana guc (solar, anaGucData) >= MQ6_GUNDUZ_ANA_GUC_ESIK_V: isitici SUREKLI
//     acik (dongu YOK, gunduz enerji bol, gecikme sifir)
//   - Aksi halde yedek aku voltajina (yedekAkuData) gore 3 kademe: DOLU->3dk,
//     ORTA->6dk, ZAYIF->10dk (eski sabit davranis, aku dusukken en konservatif)
// konteynerMq6TestModu acikken de isitici surekli acik kalir (oncelikli).
unsigned long mq6EtkinCycleMs() {
  if (yedekAkuData.read_ok) {
    if (yedekAkuData.voltaj >= YEDEK_AKU_DOLU_V) return MQ6_CYCLE_MS_DOLU;
    if (yedekAkuData.voltaj <= YEDEK_AKU_ZAYIF_V) return MQ6_CYCLE_MS_ZAYIF;
    return MQ6_CYCLE_MS_ORTA;
  }
  return MQ6_CYCLE_MS_ZAYIF; // okuma yoksa en konservatif varsayilan
}

void mq6Poll() {
  unsigned long now = millis();
  bool gunduzSurekli = anaGucData.read_ok && (anaGucData.voltaj >= MQ6_GUNDUZ_ANA_GUC_ESIK_V);

  static unsigned long cycleStart = 0;
  unsigned long etkinCycleMs = mq6EtkinCycleMs();
  unsigned long gecenDongu = now - cycleStart;
  if (gecenDongu >= etkinCycleMs) {
    cycleStart = now;
    gecenDongu = 0;
  }
  bool acikOlmali = konteynerMq6TestModu || gunduzSurekli || (gecenDongu < MQ6_POWER_ON_MS);
  if (acikOlmali != mq6Data.powered) {
    mq6Data.powered = acikOlmali;
    digitalWrite(MQ6_POWER_PIN, mq6Data.powered ? HIGH : LOW);
  }
  if (!mq6Data.powered) return; // kapaliyken okuma alinmaz, son deger korunur

  static unsigned long lastPoll = 0;
  if (now - lastPoll < MQ6_POLL_INTERVAL_MS) return;
  lastPoll = now;

  // FIX 2 (2026-08-25): ESP32'nin bilinen davranisi - analogRead() pini ADC
  // moduna alirken dijital pull-down devresini KOPARIR, yani pinMode'u
  // okumadan ONCE cagirmak (ilk deneme) hicbir sey degistirmiyordu, cunku
  // hemen ardindan gelen analogRead() onu zaten sifirliyordu. Dogrusu:
  // pinMode'u okumadan SONRA tekrar uygulamak - iki okuma arasindaki (500ms)
  // boluk boyunca pin gercekten dusuk tutuluyor (bkz ESP32 forum: "Internal
  // Pull-ups don't work on GPIO25/32" tartismasi, ayni kok neden).
  mq6Data.raw = analogRead(MQ6_ADC_PIN);
  pinMode(MQ6_ADC_PIN, INPUT_PULLDOWN);
  mq6Data.volt = (mq6Data.raw / 4095.0f) * 3.3f;
  mq6Data.last_update_ms = now;
  konteynerGazVar = konteynerGazEtkin && (mq6Data.volt >= konteynerGazEsikVolt);
}

// ============================================================
// GP2Y1014AU0F / GP2Y1010AU0F (optik toz/duman sensoru) - EFS-903R'in
// yerine gecti (bkz config.h GP2Y10_* yorumu). MQ6'dan farkli olarak isitici
// elemani YOK - guc dongusune gerek yok, her GP2Y10_POLL_INTERVAL_MS'de bir
// tek LED darbesi (datasheet: ~320us) atilip Vo ADC'den okunur. Esik
// konteynerDumanEsikVolt (NVS, web'den ayarlanabilir), etkin/pasif anahtari
// konteynerDumanEtkin - konteynerDumanVar burada set edilir, kapi/Swan PIR
// ile ayni ANLIK/kesin tetikleyici mantigina (konteynerSensorleriOku()'daki
// dumanOncekiDurum/dumanEfektif) girer.
// ============================================================
struct Gp2y10Data {
  int raw = 0;
  float volt = 0.0;
  unsigned long last_update_ms = 0;
};
Gp2y10Data gp2y10Data;

void gp2y10Poll() {
  unsigned long now = millis();
  static unsigned long lastPoll = 0;
  if (now - lastPoll < GP2Y10_POLL_INTERVAL_MS) return;
  lastPoll = now;

  // Sharp datasheet uygulama devresi: LED'i ac, GP2Y10_SAMPLE_DELAY_US
  // bekle (sinyal bu noktada stabil), ADC oku, darbenin geri kalanini
  // (GP2Y10_LED_PULSE_US - GP2Y10_SAMPLE_DELAY_US) bekleyip LED'i kapat.
  // Toplam ~320us - loop()'u bloklamasi HC-SR04 tetik darbesiyle ayni
  // mertebede, ihmal edilebilir.
  digitalWrite(GP2Y10_LED_PIN, GP2Y10_LED_AKTIF_LOW ? LOW : HIGH); // yak
  delayMicroseconds(GP2Y10_SAMPLE_DELAY_US);
  gp2y10Data.raw = analogRead(GP2Y10_ADC_PIN);
  // PULL-DOWN GERI ALINDI (2026-08-25): MQ6'da (guclu op-amp/comparator
  // ciktili) sorunsuz calisti ama GP2Y10'da gercek duman sinyali cok kucuk
  // (mV mertebesinde, Voc+dV) olabilir - zayif da olsa (~45kOhm) pull-down
  // bunu PIR2'deki gibi (bkz konteynerDonanimiInit() yorumu) gerilim bolucu
  // gibi bastirmis olabilir: pins-asagi montaj+gercek dumanla bile 0.1V bile
  // oynama gorulmuyordu, oysa boztaki gurultu onceden (kablo kopukken) hafif
  // oynayabiliyordu. GP2Y10_ADC_PIN artik duz INPUT (pull yok) - MQ6_ADC_PIN
  // pull-down'u KALDI (orada calistigi dogrulandi).
  gp2y10Data.volt = (gp2y10Data.raw / 4095.0f) * 3.3f;
  delayMicroseconds(GP2Y10_LED_PULSE_US - GP2Y10_SAMPLE_DELAY_US);
  digitalWrite(GP2Y10_LED_PIN, GP2Y10_LED_AKTIF_LOW ? HIGH : LOW); // sondur

  gp2y10Data.last_update_ms = now;

  // Tek olcume gore ANINDA tetiklemek yerine (5-6m korumasiz kabloda EMI/
  // gurultu tek bir yanlis-pozitif okumaya sebep olabilir), esik asimi
  // art arda 2 olcumde (bu POLL_INTERVAL ile ~4sn) dogrulanmadan alarm
  // verilmez. Esigin ALTINA dusme ise ANINDA temizlenir (fail-safe).
  static uint8_t esikUstuSayaci = 0;
  bool esikAsildi = konteynerDumanEtkin && (gp2y10Data.volt >= konteynerDumanEsikVolt);
  if (esikAsildi) {
    if (esikUstuSayaci < 2) esikUstuSayaci++;
  } else {
    esikUstuSayaci = 0;
  }
  konteynerDumanVar = (esikUstuSayaci >= 2);
}

// bateryaKritik hesaba katilmadan, sadece SOC + net guc (yuk - PV) ile
// kaba bir "kalan saat" tahmini - MPPT'nin gercek bir SOC algoritmasina
// sahip olmasi (coulomb-counting) bizim voltaj-bazli tahmin etmemizden
// COK daha guvenilir, o yuzden 0x311A SOC register'i temel alinir.
float mpptKalanSaatHesapla() {
  if (mpptData.battery_soc < 0) return -1.0; // SOC hic okunamadi
  float netGucW = mpptData.load_power - mpptData.pv_power;
  if (netGucW <= 1.0) return -1.0; // sarj oluyor/dengede - "kalan sure" anlamsiz
  float kalanWh = BATTERY_CAPACITY_WH * (mpptData.battery_soc / 100.0f);
  return kalanWh / netGucW;
}

void mpptPoll() {
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < MPPT_POLL_INTERVAL_MS) return;
  lastPoll = millis();

  // QPIGS: tek istekte PV+aku+yuk (Axpert/PI30 "Device general status
  // parameters inquiry"). Cevap bosluklarla ayrilmis alanlardan olusur:
  // 0 grid V, 1 grid Hz, 2 AC cikis V, 3 AC cikis Hz, 4 AC gorunur guc,
  // 5 AC aktif guc(=yuk), 6 yuk yuzdesi, 7 BUS V, 8 aku V, 9 aku sarj akimi,
  // 10 aku SOC%, 11 isi, 12 PV akim, 13 PV voltaj, 14 SCC aku V, 15 aku
  // desarj akimi, 16 durum bitleri, 17 fan offset, 18 EEPROM ver,
  // 19 PV sarj gucu(W), 20 durum2, 21 grid-feed durumu, 22 ulke, 23 grid-feed guc
  String resp;
  bool ok = mpptSendCommand("QPIGS", resp);
  if (ok) {
    String fields[24];
    int fieldCount = 0;
    int fieldStart = 0;
    for (int i = 0; i <= (int)resp.length() && fieldCount < 24; i++) {
      if (i == (int)resp.length() || resp[i] == ' ') {
        fields[fieldCount++] = resp.substring(fieldStart, i);
        fieldStart = i + 1;
      }
    }
    if (fieldCount >= 20) {
      MpptKilit kilit;
      // Ana sayfada da gosterilen ozet alanlar
      mpptData.load_voltage = fields[2].toFloat();
      mpptData.load_power = fields[5].toFloat();
      mpptData.battery_voltage = fields[8].toFloat();
      mpptData.battery_soc = fields[10].toInt();
      mpptData.pv_current = fields[12].toFloat();
      mpptData.pv_voltage = fields[13].toFloat();
      mpptData.pv_power = fields[19].toFloat();
      mpptData.load_current = (mpptData.load_voltage > 1.0f) ? (mpptData.load_power / mpptData.load_voltage) : 0.0f;

      // Sadece "Invertor" sekmesinde gosterilen detay alanlar
      mpptData.grid_voltage = fields[0].toFloat();
      mpptData.grid_freq = fields[1].toFloat();
      mpptData.ac_out_freq = fields[3].toFloat();
      mpptData.apparent_power = fields[4].toFloat();
      mpptData.load_percent = fields[6].toInt();
      mpptData.bus_voltage = fields[7].toFloat();
      mpptData.battery_charge_current = fields[9].toFloat();
      mpptData.temperature_c = fields[11].toFloat();
      mpptData.battery_discharge_current = fields[15].toFloat();
      mpptData.status_bits = fields[16];
      // b7..b0 (bkz QPIGS dokumantasyonu): b4=yuk acik, b2=sarj acik,
      // b1=SCC(solar) sarj acik, b0=AC sarj acik
      if (fields[16].length() == 8) {
        mpptData.load_on = fields[16][3] == '1';        // b4
        mpptData.charging_on = fields[16][5] == '1';     // b2
        mpptData.scc_charging_on = fields[16][6] == '1'; // b1
        mpptData.ac_charging_on = fields[16][7] == '1';  // b0
      }

      mpptData.read_ok = true;
      mpptData.last_update_ms = millis();
    } else {
      MpptKilit kilit;
      mpptData.read_ok = false;
      DEBUG_PRINT("[MPPT] QPIGS cevabi eksik alan, alan sayisi=");
      DEBUG_PRINTLN(fieldCount);
    }
  } else {
    MpptKilit kilit;
    mpptData.read_ok = false;
    DEBUG_PRINTLN("[MPPT] QPIGS cevap yok/zaman asimi - kablolama henuz dogrulanmadi");
  }

  mpptData.kalan_saat = mpptKalanSaatHesapla();

  DEBUG_PRINT("[MPPT] Vbat="); DEBUG_PRINT(String(mpptData.battery_voltage, 2));
  DEBUG_PRINT(" PV="); DEBUG_PRINT(String(mpptData.pv_power, 1)); DEBUG_PRINT("W");
  DEBUG_PRINT(" Yuk="); DEBUG_PRINT(String(mpptData.load_power, 1)); DEBUG_PRINT("W");
  DEBUG_PRINT(" SOC="); DEBUG_PRINT(String(mpptData.battery_soc));
  DEBUG_PRINT(" ok="); DEBUG_PRINTLN(mpptData.read_ok ? "1" : "0");

  // QMOD: calisma modu (Sebeke/Aku/Sarj/Ariza vb.) - "Invertor" sekmesinde
  // gosterilir, ana sayfayi etkilemez. Salt-okunur, risksiz.
  String modResp;
  if (mpptSendCommand("QMOD", modResp) && modResp.length() >= 1) {
    MpptKilit kilit;
    mpptData.mode_code = modResp[0];
    switch (modResp[0]) {
      case 'P': mpptData.mode_text = "Acilis"; break;
      case 'S': mpptData.mode_text = "Bekleme (Standby)"; break;
      case 'L': mpptData.mode_text = "Sebeke (Line)"; break;
      case 'B': mpptData.mode_text = "Aku (Battery)"; break;
      case 'F': mpptData.mode_text = "ARIZA (Fault)"; break;
      case 'D': mpptData.mode_text = "Kapali (Shutdown)"; break;
      case 'C': mpptData.mode_text = "Sarj (Charge)"; break;
      case 'Y': mpptData.mode_text = "Bypass"; break;
      case 'E': mpptData.mode_text = "ECO"; break;
      default:  mpptData.mode_text = "Bilinmiyor (" + String(modResp[0]) + ")"; break;
    }
    mpptData.mode_read_ok = true;
  } else {
    MpptKilit kilit;
    mpptData.mode_read_ok = false;
  }

  // QPIWS: 36 bitlik uyari/ariza durumu - "Invertor" sekmesinde gosterilir.
  // Aktif olan bitler Turkce aciklamalarla listeye ceviriliyor.
  static const char* MPPT_WARN_ADLARI[36] = {
    "PV kaybi", "Invertor arizasi", "Bus asiri yuksek", "Bus asiri dusuk",
    "Bus yumusak hata", "Sebeke kesintisi", "Cikis kisa devre", "Invertor voltaji cok dusuk",
    "Invertor voltaji cok yuksek", "Asiri sicaklik", "Fan kilitli", "Aku voltaji yuksek",
    "Aku dusuk alarmi", "-", "Aku kesme (dusuk)", "Aku derating",
    "Asiri yuk", "EEPROM hatasi", "Invertor asiri akim", "Invertor yumusak hata",
    "Kendi kendini test hatasi", "Cikis DC voltaji asiri", "Aku devre disi (acik)", "Akim sensoru hatasi",
    "Aku kisa devre", "Guc siniri", "PV voltaji yuksek", "MPPT asiri yuk/PV asiri akim",
    "MPPT asiri yuk uyarisi", "Aku sarj icin cok dusuk", "DC/DC asiri akim", "Uzaktan ariza (D)",
    "Uzaktan ariza (D)", "Dusuk PV enerjisi", "Bus yumusak baslangicinda yuksek AC girisi", "Aku dengeleme"
  };
  String wsResp;
  if (mpptSendCommand("QPIWS", wsResp) && wsResp.length() >= 36) {
    MpptKilit kilit;
    mpptData.warn_raw = wsResp;
    String liste = "";
    for (int i = 0; i < 36; i++) {
      if (wsResp[i] == '1') {
        if (liste.length() > 0) liste += ", ";
        liste += MPPT_WARN_ADLARI[i];
      }
    }
    mpptData.warn_list = liste;
    mpptData.warn_read_ok = true;
  } else {
    MpptKilit kilit;
    mpptData.warn_read_ok = false;
  }

  // QPIRI: anma degerleri/ayarlar - nadiren degisir, sadece 60sn'de bir
  // sorgulanir (RS232 trafiğini gereksiz artirmamak icin).
  static unsigned long lastQpiriPoll = 0;
  if (millis() - lastQpiriPoll >= 60000UL) {
    lastQpiriPoll = millis();
    String riResp;
    if (mpptSendCommand("QPIRI", riResp)) {
      String rf[24];
      int rfCount = 0, rfStart = 0;
      for (int i = 0; i <= (int)riResp.length() && rfCount < 24; i++) {
        if (i == (int)riResp.length() || riResp[i] == ' ') {
          rf[rfCount++] = riResp.substring(rfStart, i);
          rfStart = i + 1;
        }
      }
      // Alan sirasi topluluk kaynaklarina (mpp-solar vb.) dayanir - DOGRULA,
      // cihazimizin gercek QPIRI cevabiyla karsilastirilmadi.
      if (rfCount >= 18) {
        MpptKilit kilit;
        mpptData.qpiri_batt_voltage = rf[7].toFloat();
        mpptData.qpiri_batt_recharge_v = rf[8].toFloat();
        mpptData.qpiri_batt_under_v = rf[9].toFloat();
        mpptData.qpiri_batt_bulk_v = rf[10].toFloat();
        mpptData.qpiri_batt_float_v = rf[11].toFloat();
        mpptData.qpiri_batt_type = rf[12].toInt();
        mpptData.qpiri_max_ac_charge_a = rf[13].toInt();
        mpptData.qpiri_max_charge_a = rf[14].toInt();
        mpptData.qpiri_out_source_priority = rf[16].toInt();
        mpptData.qpiri_charger_source_priority = rf[17].toInt();
        mpptData.qpiri_read_ok = true;
        mpptData.qpiri_last_update_ms = millis();
      } else {
        MpptKilit kilit;
        mpptData.qpiri_read_ok = false;
      }
    } else {
      MpptKilit kilit;
      mpptData.qpiri_read_ok = false;
    }
  }
}

// Histerezis: bateryaKesmeVolt'un altina inince kritik olur, ancak
// bateryaGeriYuklemeVolt'a CIKANA kadar kritik kalir - tam esik sinirinda
// role/komut cirpinmasini onler. FAIL-OPEN: MPPT verisi bayat/okunamiyorsa
// (kablo kopuk, register hala yanlis vb.) mevcut durum DEGISTIRILMEZ -
// MPPT baglantisi kopmasi sulama/lambayi gereksiz kesen YENI bir ariza
// noktasi olmasin diye bilincli tercih (bkz sensorData.last_update_ms<10000
// alarm-tazelik korumasiyla ayni felsefe).
void bateryaDurumHesapla() {
  if (!bateryaKorumaAktif) { bateryaKritik = false; return; }
  MpptKilit kilit;
  bool veriTaze = (millis() - mpptData.last_update_ms) < MPPT_STALE_MS;
  if (!veriTaze || !mpptData.read_ok) return;
  if (!bateryaKritik && mpptData.battery_voltage <= bateryaKesmeVolt) {
    bateryaKritik = true;
  } else if (bateryaKritik && mpptData.battery_voltage >= bateryaGeriYuklemeVolt) {
    bateryaKritik = false;
  }
}

// bateryaKritik degistiginde (sadece GECISTE, her dongude degil - retry-safe,
// PANIC/ALARM_MUTE explicit-set deseniyle ayni mantik) ESP8266'ya bildirilir
// ki sulama rolesini zorla kapatsin/serbest biraksin.
void bateryaRS485Bildir() {
  static bool oncekiKritik = false;
  if (bateryaKritik == oncekiKritik) return;
  String reply;
  String cmd = String("MASTER:SET_BATTERY_LOW=") + (bateryaKritik ? "1" : "0") + "\n";
  rs485_send_wait_ack(cmd.c_str(), reply, 1000, 3);
  oncekiKritik = bateryaKritik;
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
<title>Kalburum - Merkez Kontrol</title>
<style>
:root{--bg:#f6f8fa;--card:#fff;--text:#1f2937;--muted:#6b7280;--border:#e5e7eb;--border-strong:#4f7fe0;--primary:#2563eb;--accent:#10b981;--warn:#f59e0b;--danger:#ef4444;--danger-bg:#ffebee;--danger-bg-t:rgba(255,235,238,.6);--shadow:0 1px 3px rgba(0,0,0,.1)}
@media(prefers-color-scheme:dark){:root{--bg:#0b1220;--card:#111827;--text:#e5e7eb;--muted:#9ca3af;--border:#374151;--border-strong:#4a72c0;--primary:#60a5fa;--accent:#34d399;--warn:#fbbf24;--danger:#f87171;--danger-bg:#3a2222;--danger-bg-t:rgba(58,34,34,.6);--shadow:0 1px 3px rgba(0,0,0,.4)}}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;background:var(--bg);color:var(--text);padding:12px;-webkit-tap-highlight-color:transparent}
.container{max-width:1100px;margin:0 auto}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
.header h1{font-size:20px}
.header .meta{font-size:12px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:16px}
.card{background:var(--card);border:2.5px solid var(--border-strong);border-radius:12px;padding:16px;box-shadow:var(--shadow)}
.card h3,.card summary{font-size:15px;font-weight:700;color:var(--text);margin-bottom:8px;letter-spacing:.2px}
.card summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px}
.card summary::-webkit-details-marker{display:none}
.card summary::before{content:'▸';display:inline-block;font-size:12px;color:var(--muted);transition:transform .15s}
.card[open]>summary::before{transform:rotate(90deg)}
.card.zone-sudepo{border-left:4px solid #3b82f6}
.card.zone-konteyner{border-left:4px solid var(--warn)}
/* Genel sayfadaki kartların her biri kendi renginde sol kenarla ayırt edilsin
   (kullanıcı talebi) - sadece kategori/marka amaçlı, alarm/durum rengiyle
   (accent/warn/danger) karışmasın diye ayrı bir palet kullanılıyor. */
#dashboard>.row:first-of-type>.card:nth-child(1){border-left:4px solid #64748b}
#dashboard>.row:first-of-type>.card:nth-child(2){border-left:4px solid #f43f5e}
#dashboard .grid>.card:nth-child(1){border-left:4px solid #3b82f6}
#dashboard .grid>.card:nth-child(2){border-left:4px solid #06b6d4}
#dashboard .grid>.card:nth-child(3){border-left:4px solid #14b8a6}
#dashboard .grid>.card:nth-child(4){border-left:4px solid #f97316}
#dashboard .grid>.card:nth-child(5){border-left:4px solid #84cc16}
#dashboard .grid>.card:nth-child(6){border-left:4px solid #8b5cf6}
#dashboard .grid>.card:nth-child(7){border-left:4px solid #ec4899}
#dashboard .grid>.card:nth-child(8){border-left:4px solid #6366f1}
.subdet{margin-top:16px;border:2.5px solid var(--border-strong);border-radius:8px;padding:10px 12px}
.subdet summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px;font-size:14px;font-weight:700;color:var(--text)}
.subdet summary::-webkit-details-marker{display:none}
.subdet summary::before{content:'▸';display:inline-block;font-size:11px;color:var(--muted);transition:transform .15s}
.subdet[open]>summary::before{transform:rotate(90deg)}
.kpi{font-family:Consolas,'DejaVu Sans Mono','Courier New',monospace;font-size:26px;font-weight:700;letter-spacing:1px;display:inline-block;padding:4px 10px;border-radius:6px;background:#0a0f0a;color:#22ff66;text-shadow:0 0 2px rgba(34,255,102,.9),0 0 8px rgba(34,255,102,.55);border:1px solid #163d1e}
.kpi.danger{color:#ff3b3b;text-shadow:0 0 2px rgba(255,59,59,.9),0 0 8px rgba(255,59,59,.55);border-color:#611515;animation:pulse 1.2s infinite}
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
.sysdot{width:15px;height:15px;border-radius:50%;display:inline-block}
.sysdot.normal{background:var(--accent);box-shadow:0 0 7px var(--accent)}
.sysdot.kritik{background:var(--warn);box-shadow:0 0 7px var(--warn)}
.sysdot.tehlike{background:var(--danger);box-shadow:0 0 9px var(--danger);animation:softblink 1.8s ease-in-out infinite}
@keyframes softblink{0%,100%{opacity:1}50%{opacity:.3}}
.led{display:inline-block;width:11px;height:11px;border-radius:50%;background:var(--border);vertical-align:middle;margin-right:3px;transition:background .15s,box-shadow .15s}
.led.on{background:var(--danger);box-shadow:0 0 6px var(--danger)}
.led.ok{background:var(--accent);box-shadow:0 0 6px var(--accent)}
.led.pending{background:var(--warn);box-shadow:0 0 6px var(--warn);animation:pulse 1.2s infinite}
@keyframes pulse{0%{opacity:1}50%{opacity:.5}100%{opacity:1}}
#alarm-banner{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);z-index:1000;background:var(--danger-bg-t);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);color:var(--danger);border:2px solid var(--danger);padding:20px 24px;border-radius:12px;font-weight:700;font-size:17px;text-align:center;animation:pulse 1.2s infinite;box-shadow:0 8px 30px rgba(0,0,0,.35);max-width:90vw;width:420px}
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
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap;position:sticky;top:0;z-index:100;padding:8px 0;background:rgba(246,248,250,.75);backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px)}
@media(prefers-color-scheme:dark){.nav{background:rgba(11,18,32,.75)}}
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
      <h1>Kalburum Merkez Kontrol</h1>
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
    <button onclick="show('invertor')" id="nav-invertor">İnvertör</button>
    <button onclick="show('bilgiler')" id="nav-bilgiler">Bilgiler</button>
  </div>

  <div id="dashboard" class="section active">
    <div class="row" style="margin-top:0;gap:12px;align-items:stretch;flex-wrap:wrap">
      <div class="card" style="padding:8px 16px;flex:2;min-width:280px">
        <div style="display:flex;align-items:center;gap:10px;overflow-x:auto;white-space:nowrap">
          <b style="font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.4px">ESP8266</b>
          <span class="badge" id="esp8266-badge" style="font-size:11px;padding:2px 7px">Bekleniyor</span>
          <span class="badge" id="nano-badge" style="font-size:11px;padding:2px 7px">Nano: Bekleniyor</span>
          <span style="font-size:12px;color:var(--muted)">K1:<b id="d1">-</b></span>
          <span style="font-size:12px;color:var(--muted)">K2:<b id="d2">-</b></span>
          <span style="font-size:12px;color:var(--muted)">Röle:<b id="rl">-</b></span>
          <span style="font-size:12px;color:var(--muted)">Lamba:<b id="lm">-</b></span>
          <span style="font-size:12px;color:var(--muted)">NemR:<b id="mr">-</b></span>
        </div>
      </div>
      <div class="card" style="padding:8px 16px;flex:1;min-width:220px">
        <div class="row" style="margin-top:0;align-items:center;gap:8px">
          <span class="sysdot normal" id="alarm-dot"></span>
          <b style="font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.4px">Alarmlar</b>
          <span id="alarm-text" style="font-size:12px;font-weight:700"></span>
        </div>
        <div id="hata-box" style="margin-top:4px;color:var(--warn);font-size:12px"></div>
        <div id="alarm-log-list" style="margin-top:6px;font-size:11px;color:var(--muted);max-height:110px;overflow-y:auto">Yükleniyor...</div>
      </div>
    </div>
    <div class="grid">
      <div class="card"><h3>Depo Doluluk</h3><div class="kpi" id="kpi-pct">--</div><div class="bar"><div id="bar-pct"></div></div></div>
      <div class="card"><h3>Seviye</h3><div class="kpi" id="kpi-cm">--</div></div>
      <div class="card"><h3>Tahmini Hacim</h3><div class="kpi" id="kpi-litre">--</div></div>
      <div class="card"><h3>Dış Sıcaklık ve Nem</h3><div class="kpi" id="kpi-temp">--</div><div style="margin-top:6px;font-size:13px;color:var(--muted)">Nem: <b id="kpi-nem">--</b></div></div>
      <div class="card"><h3>Toprak Nem</h3><div class="kpi" id="kpi-moisture">--</div><div style="margin-top:8px;font-size:12px;color:var(--muted)">Ham: <b id="moisture-raw">-</b></div><div style="font-size:12px;color:var(--muted)">Çıkış: <b id="moisture-output">-</b> | Mod: <b id="moisture-mode">-</b></div></div>
      <div class="card"><h3>Akü (MPPT)</h3><div class="kpi" id="kpi-batarya">--</div><small id="batarya-soc"></small><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="batarya-durum">-</div><div style="margin-top:6px;font-size:12px;color:var(--muted)">☀️ Güneş: <b id="batarya-pv">-</b> | 🔌 Tüketim: <b id="batarya-yuk">-</b></div><div style="font-size:12px;color:var(--muted)" id="batarya-kalan">-</div><div style="margin-top:8px"><button class="btn" style="font-size:11px;padding:4px 10px" onclick="show('invertor')">Tüm invertör detayları →</button></div></div>
      <div class="card"><h3>Yedek Akü</h3><div class="kpi" id="kpi-yedek-aku">--</div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="yedek-aku-durum">-</div></div>
      <div class="card"><h3>Ana Güç</h3><div class="kpi" id="kpi-ana-guc">--</div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="ana-guc-durum">-</div></div>
    </div>
  </div>

  <div id="kontrol" class="section">
    <div class="card">
      <h3>Lamba</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">İki zonun da kendi lambası var - Sudepo'nunki tamamen elle, Konteyner'inki hem elle hem alarm/siren ile otomatik yanabilir (ikisi birbirini bastırmaz - alarm sürerken elle kapatsanız bile alarm onu yeniden yakar).</p>
      <div class="row">
        <button class="btn btn-primary" id="lamba-btn" onclick="toggleLamba()">Sudepo Zonu: Aç</button>
        <button class="btn btn-primary" id="konteyner-lamba-btn" onclick="toggleKonteynerLamba()">Konteyner Zonu: Aç</button>
      </div>
      <div id="lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="konteyner-lamba-sonuc" style="margin-top:4px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card" style="border:2px solid var(--danger)">
      <h3>🚨 Acil Durum Lambası</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">Ana güç düşük seviyeye inince (aşağıdaki eşikler) sadece bildirim gelir, lamba OTOMATİK açılmaz - gerek görürsen buradan manuel aç. Panik veya Konteyner alarmında ise otomatik/anında yanar.</p>
      <button class="btn" style="font-size:16px;padding:14px 20px;width:100%" id="acil-lamba-btn" onclick="toggleAcilLamba()">⚪ Acil Durum Lambası (Kapalı)</button>
      <div id="acil-lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
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

    <details class="card">
      <summary>Telegram Bildirimleri <small id="sum-telegram" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted)">Alarm YENİ başladığında (panik, kapı, PIR, kaçak vb. - Kalburum/Konteyner PIR'ı dahil) Telegram'a bildirim gönderir - sadece bu cihazın o an interneti varsa (örn. hotspot bağlıyken) çalışır.</p>
      <div class="row">
        <button class="btn" id="telegram-ac-kapa-btn" onclick="telegramAcKapa()">Yükleniyor...</button>
        <button class="btn btn-primary" onclick="telegramTest()">Test Mesajı Gönder</button>
      </div>
      <div id="telegram-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card zone-sudepo">
      <summary>🚰 Su Deposu Zonu - Ayarlar <small id="sum-sudepo" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
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
          <div><label class="sz-label">PIR Min. Tetiklenme</label><input class="input" type="number" min="1" max="8" id="sz_pirMinTetiklenme" onchange="szKaydet()"></div>
        </div>

        <details class="subdet" open>
          <summary>Sensörler - Aktif/Pasif</summary>
          <p style="font-size:12px;color:var(--muted)">Mod/zamandan bağımsız, her zaman geçerli genel anahtar - kablosuz/arızalı bir sensörü buradan tamamen devre dışı bırakabilirsiniz (Konteyner Zonu'ndaki sensör anahtarlarıyla aynı mantık).</p>
          <div class="sz-cbgrid" id="sz-grid-sensorEtkin"></div>
        </details>

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
      <summary>👁️ Konteyner Zonu - Konteyner Alarm Ayarları <small id="sum-konteyner" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>

      <p class="sz-label">Canlı Durum</p>
      <div class="row" style="gap:16px;align-items:center">
        <div><span class="led" id="kz-pir"></span> HC505-1 PIR</div>
        <div><span class="led" id="kz-swan"></span> Swan PIR</div>
        <div><span class="led" id="kz-kapi"></span> Kapı</div>
        <div><span class="led" id="kz-duman"></span> Duman</div>
        <div><span class="led" id="kz-gaz"></span> Gaz (MQ6)</div>
        <div><span class="led" id="kz-alarm"></span> Yerel Uyarı (LED/Buzzer) <small id="kz-alarm-txt" style="color:var(--muted)"></small></div>
        <div><span class="led" id="kz-siren"></span> Siren</div>
        <div><span class="led" id="kz-lamba"></span> Lamba</div>
      </div>

      <p class="sz-label" style="margin-top:12px">Sensör Aktif/Pasif</p>
      <div class="row" style="gap:16px">
        <label><input type="checkbox" id="kz_pirEtkin" onchange="konteynerSensorAktifKaydet()"> HC505-1 PIR Aktif</label>
        <label><input type="checkbox" id="kz_kapiEtkin" onchange="konteynerSensorAktifKaydet()"> Kapı Aktif</label>
        <label><input type="checkbox" id="kz_swanEtkin" onchange="konteynerSensorAktifKaydet()"> Swan PIR Aktif</label>
        <label><input type="checkbox" id="kz_dumanEtkin" onchange="konteynerSensorAktifKaydet()"> Duman Sensörü Aktif</label>
        <label><input type="checkbox" id="kz_gazEtkin" onchange="konteynerSensorAktifKaydet()"> Gaz (MQ6) Alarmı Aktif</label>
      </div>
      <p style="font-size:11px;color:var(--muted);margin-top:4px">Henüz kablolanmamış/arızalı bir sensörü buradan pasif yapabilirsiniz - pasifken hâlâ yukarıdaki "Canlı Durum"da ham değeri görünür ama alarma/banner'a hiç katkı yapmaz.</p>

      <details class="subdet" style="margin-top:12px">
        <summary>Gaz (MQ6) &amp; Duman (GP2Y10)</summary>
        <div class="row" style="gap:16px;margin-top:6px">
          <div style="font-size:12px;color:var(--muted)">MQ6: <b id="kz-mq6">-</b></div>
          <div style="font-size:12px;color:var(--muted)">GP2Y10 (duman/toz): <b id="kz-gp2y10">-</b></div>
        </div>
        <label style="display:block;margin-top:8px"><input type="checkbox" id="kz_mq6Test" onchange="konteynerMq6TestKaydet()"> MQ6 Test Modu (ısıtıcıyı sürekli açık tutar)</label>
        <p style="font-size:11px;color:var(--warn);margin-top:2px">Test Modu AÇIKKEN MQ6 değeri her zaman canlıdır (10dk/60sn döngüsü atlanır) - gaz/çakmak testi için kullanın, test bitince KAPATIN (sürekli ısıtıcı pil tüketir). Reset sonrası otomatik kapanır.</p>
        <div class="sz-grid" style="margin-top:8px">
          <div><label class="sz-label">Gaz Alarm Eşiği (Volt)</label><input class="input" type="number" step="0.1" min="0.1" max="3.3" id="kz_gazEsik" onchange="konteynerGazAyarKaydet()"></div>
          <div><label class="sz-label">Duman Alarm Eşiği (Volt)</label><input class="input" type="number" step="0.1" min="0.1" max="3.3" id="kz_dumanEsik" onchange="konteynerDumanAyarKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--warn);margin-top:4px">Kalibrasyon potu olmadığından bu eşikler tahmini varsayılan - sahada gerçek bir gaz/duman kaynağıyla test edip gerekirse ayarlayın. Gaz ve duman alarmı panik gibi davranır: mod/susturma/tetik animasyonundan bağımsız anında tam güçle çalar (gecikme istenmez).</p>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">MQ6'ün ısıtıcısı artık adaptif: gündüz ana güç (solar) ≥26V ise sürekli açık; gece/düşük güçte yedek akü doluluğuna göre 3dk/6dk/10dk'da bir 60sn açılıp kapanıyor (akü zayıfken en konservatif 10dk). Gösterilen değer, döngü kapalıyken en fazla o döngü kadar eski olabilir. GP2Y10'un ısıtıcısı yok, her zaman canlı (2sn'de bir tazelenir).</p>
      </details>

      <details class="subdet" style="margin-top:8px">
        <summary>HC505-1 PIR Hassasiyet</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">HC-SR505'in potansiyometresi yok, tetiklenince çıkışı sabit ~6-12sn HIGH'ta kalır. "Hassasiyet" bu yüzden iki kademeli süre mantığıyla ayarlanır: her yeni harekette hemen 1sn'lik bir bip/LED darbesi verilir (henüz alarm değil); hareket kesintisiz "Onay Süresi" kadar sürerse GERÇEK alarm sayılır.</p>
        <div class="sz-grid" style="margin-top:6px">
          <div><label class="sz-label">Tutma Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirTutma" onchange="konteynerPirKaydet()"></div>
          <div><label class="sz-label">Onay Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirOnay" onchange="konteynerPirKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">Tutma Süresi: hareket bittikten sonra ne kadar daha aktif sayılsın. Onay Süresi: kesintisiz hareketin kaç saniye sonra GERÇEK alarma dönüşeceği.</p>
      </details>

      <details class="subdet" style="margin-top:8px">
        <summary>Swan PIR Zamanlama</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">Swan Quad PET PIR'in kendi donanımsal debounce/evcil-hayvan-bağışıklığı var, bu yüzden varsayılan olarak (0/0) kapı gibi ANINDA eskale eder. Sahada yanlış tetiklenme yaşarsanız buraya da HC505-1 PIR'daki gibi bir Tutma/Onay süresi girebilirsiniz - 0 bırakılırsa davranış değişmez.</p>
        <div class="sz-grid" style="margin-top:6px">
          <div><label class="sz-label">Tutma Süresi (sn)</label><input class="input" type="number" min="0" max="120" id="kz_swanTutma" onchange="konteynerSwanKaydet()"></div>
          <div><label class="sz-label">Onay Süresi (sn)</label><input class="input" type="number" min="0" max="120" id="kz_swanOnay" onchange="konteynerSwanKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">0/0 = anında eskale (varsayılan/eski davranış). Onay Süresi &gt; 0 girilirse, hareket kesintisiz o kadar sürmeden GERÇEK alarma dönüşmez.</p>
      </details>

      <p style="font-size:11px;color:var(--muted);margin-top:10px">Tetik animasyonu (lamba pırpırı + atışlı siren) ve genel Alarm Modu (Sesli/Sessiz/Onaylı) ayarları için bkz üstteki Sudepo/Genel Alarm kartı - Konteyner Zonu aynı ortak sistemi kullanır.</p>
      <div id="kz-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>🔋 Batarya (MPPT) Koruma Ayarları <small id="sum-batkoruma" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Akü voltajı, MPPT şarj kontrolcüsünden ayrı bir RS232/PI30 hattı üzerinden okunur. Koruma AÇIK olduğunda, voltaj "Kesme" eşiğinin altına inince sulama rölesi ve Konteyner lambası otomatik kapatılır (siren/alarm etkilenmez); voltaj "Geri Yükleme" eşiğine çıkınca otomatik serbest kalır.</p>
      <p style="font-size:12px;color:var(--warn);margin-bottom:8px"><b>Not:</b> Register/ölçek değerleri sahada doğrulanana kadar koruma varsayılan olarak KAPALI kalır - önce Genel sekmesindeki "Akü" kartından gelen voltajı bir multimetreyle karşılaştırıp doğrulayın, sonra korumayı açın.</p>
      <div class="row">
        <button class="btn btn-primary" id="batarya-koruma-btn" onclick="bateryaKorumaToggle()">Korumayı Aç</button>
      </div>
      <div class="sz-grid" style="margin-top:10px">
        <div><label class="sz-label">Kesme Voltajı (V)</label><input class="input" type="number" step="0.1" min="0" id="batarya-kesme" onchange="bateryaEsikKaydet()"></div>
        <div><label class="sz-label">Geri Yükleme Voltajı (V)</label><input class="input" type="number" step="0.1" min="0" id="batarya-geri" onchange="bateryaEsikKaydet()"></div>
      </div>
      <div id="batarya-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>⚡ Ana Güç (24V) İzleme Eşikleri <small id="sum-anaguc" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Ana hat voltajı ADS1115 (I2C) ile izlenir. Eşiklerin altına inince SADECE bildirim (Telegram) gönderilir - hiçbir röle/lamba otomatik açılmaz. Acil Durum Lambasını Kontrol sekmesinden manuel açabilirsiniz.</p>
      <div class="sz-grid">
        <div><label class="sz-label">Düşük (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik1" onchange="anaGucEsikKaydet()"></div>
        <div><label class="sz-label">Kritik (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik2" onchange="anaGucEsikKaydet()"></div>
        <div><label class="sz-label">Acil (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik3" onchange="anaGucEsikKaydet()"></div>
      </div>
      <div id="ana-guc-esik-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>WiFi <small id="sum-wifi" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
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

    <details class="card">
      <summary>Nem Ayarları <small id="sum-nem" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
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

    <details class="card">
      <summary>Sistem</summary>
      <div class="row">
        <button class="btn btn-danger" onclick="restartSistem()">Yeniden Başlat</button>
      </div>
    </details>
  </div>

  <div id="kumanda" class="section">
    <details class="card zone-konteyner">
      <summary>📡 Konteyner Zonu - IR Kumanda</summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Konteynerdaki IR alıcıya kumanda tuşu tanımla - "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa bas, sonra hangi komutu çalıştıracağını seç. Birden fazla kumanda eklenebilir.</p>
      <div id="ir-liste" style="font-size:13px">Yükleniyor...</div>
      <div class="row" style="margin-top:10px">
        <button class="btn btn-primary" onclick="irOgrenBaslat()">➕ Yeni Tuş Öğren</button>
      </div>
      <div id="ir-ogren-durum" style="margin-top:8px;font-size:13px"></div>
    </details>
  </div>

  <div id="invertor" class="section">
    <div class="card">
      <h3 style="margin:0 0 10px">🔌 İnvertör (MPPT) — Tüm Değerler</h3>
      <p style="font-size:12px;color:var(--muted);margin-bottom:12px">RS232/PI30 üzerinden okunan tüm QPIGS/QMOD/QPIWS/QPIRI alanları — salt-okunur, buradan hiçbir ayar değiştirilmez. Bağlantı: <b id="inv-baglanti">-</b></p>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Durum</h4>
      <table class="table">
        <tr><td>Çalışma modu</td><td id="inv-mode">-</td></tr>
        <tr><td>Aktif uyarı/arıza</td><td id="inv-warn">-</td></tr>
        <tr><td>Yük çıkışı</td><td id="inv-load-on">-</td></tr>
        <tr><td>Şarj (genel/solar/AC)</td><td id="inv-charging">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Akü / Güneş / Yük (anlık)</h4>
      <table class="table">
        <tr><td>Akü voltajı</td><td id="inv-batt-v">-</td></tr>
        <tr><td>Akü SOC</td><td id="inv-soc">-</td></tr>
        <tr><td>Akü şarj akımı</td><td id="inv-batt-charge-a">-</td></tr>
        <tr><td>Akü deşarj akımı</td><td id="inv-batt-discharge-a">-</td></tr>
        <tr><td>PV voltajı</td><td id="inv-pv-v">-</td></tr>
        <tr><td>PV akımı</td><td id="inv-pv-a">-</td></tr>
        <tr><td>PV gücü</td><td id="inv-pv-w">-</td></tr>
        <tr><td>Yük voltajı (AC çıkış)</td><td id="inv-load-v">-</td></tr>
        <tr><td>Yük gücü (aktif/görünür)</td><td id="inv-load-w">-</td></tr>
        <tr><td>Yük yüzdesi</td><td id="inv-load-pct">-</td></tr>
        <tr><td>Kalan süre tahmini</td><td id="inv-kalan">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Şebeke / Sistem</h4>
      <table class="table">
        <tr><td>Şebeke voltajı/frekansı</td><td id="inv-grid">-</td></tr>
        <tr><td>AC çıkış frekansı</td><td id="inv-ac-hz">-</td></tr>
        <tr><td>BUS voltajı</td><td id="inv-bus-v">-</td></tr>
        <tr><td>Isı (kanat sıcaklığı)</td><td id="inv-temp">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Ayarlar / Anma Değerleri (QPIRI — 60sn'de bir okunur)</h4>
      <p style="font-size:11px;color:var(--muted);margin-bottom:6px">Bu bölümdeki alan sırası topluluk kaynaklarına dayanır, cihazın gerçek çıktısıyla henüz birebir doğrulanmadı — DOĞRULA.</p>
      <table class="table">
        <tr><td>Akü anma voltajı</td><td id="inv-ri-battv">-</td></tr>
        <tr><td>Akü yeniden şarj voltajı</td><td id="inv-ri-recharge">-</td></tr>
        <tr><td>Akü düşük (kesme) voltajı</td><td id="inv-ri-under">-</td></tr>
        <tr><td>Akü bulk voltajı</td><td id="inv-ri-bulk">-</td></tr>
        <tr><td>Akü float voltajı</td><td id="inv-ri-float">-</td></tr>
        <tr><td>Akü tipi</td><td id="inv-ri-type">-</td></tr>
        <tr><td>Maks. AC şarj akımı</td><td id="inv-ri-maxac">-</td></tr>
        <tr><td>Maks. şarj akımı</td><td id="inv-ri-maxchg">-</td></tr>
        <tr><td>Çıkış kaynağı önceliği</td><td id="inv-ri-outpri">-</td></tr>
        <tr><td>Şarj kaynağı önceliği</td><td id="inv-ri-chgpri">-</td></tr>
      </table>
    </div>
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
        <tr><td>-</td><td>6</td><td>GP2Y10 (duman/toz sensörü) analog çıkış (Vo)</td><td>ADC1 kanal 5 — 2026-08-20'de PIR2'den boşaltıldı</td></tr>
        <tr><td>D17</td><td>17</td><td>PIR HC-SR505 (Konteyner)</td><td>OUT ucu bu pine; VCC/GND sensörün kendi besleme uçlarından (mini tip, 3.3-5V) — eski GPIO6'dan buraya taşındı</td></tr>
        <tr><td>D7</td><td>7</td><td>Kapı Reed Switch</td><td>Bir ucu bu pine, diğer ucu GND'ye (dahili pull-up kullanılıyor, ek direnç gerekmez)</td></tr>
        <tr><td>D8</td><td>8</td><td>Alarm Sireni Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif (Sudepo Zonu'ndaki "Alarm Rölesi" ile aynı mantık)</td></tr>
        <tr><td>D9</td><td>9</td><td>Uyarı Lambası Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif - siren ile birlikte VEYA Onaylı modda "Sessiz (Lamba)" onayında tek başına yanar</td></tr>
        <tr><td>RX (UART2)</td><td>40</td><td>MAX3232 (MPPT, RS232)</td><td>R1OUT ucu bu pine (MAX3232 çip pin adı - modül silkscreen'i "TX"/"RX" farklı yazabilir, işleve göre bağlayın) - MPPT şarj kontrolcüden akü/PV/yük verisi, ESP8266 hattından (UART1) tamamen ayrı. MPPT'nin portu RS485 DEĞİL, RS232 (bkz not) - MAX485 kullanılmaz. Eski MAX485 modülü söküldükten (2026-08-14) sonra bu pinlere taşındı.</td></tr>
        <tr><td>TX (UART2)</td><td>41</td><td>MAX3232 (MPPT, RS232)</td><td>T1IN ucu bu pine</td></tr>
        <tr><td>ADC1</td><td>2</td><td>Yedek Akü (gerilim bölücü)</td><td>3x12V paralel yedek akü bankasının voltajını izler — salt-okunur, hiçbir röleyi tetiklemez</td></tr>
        <tr><td>-</td><td>21</td><td>Yedek Akü Şarj Rölesi</td><td>Ana 24V hattan yedek bankaya şarj yolu — sadece gündüz/güneş varken (MPPT <code>pv_power</code>) HIGH</td></tr>
        <tr><td>I2C SDA</td><td>36</td><td>AHT10 + ADS1115 (Sıcaklık/Nem, Ana Güç)</td><td>Konteyner'e özel, elle I2C protokolüyle okunur</td></tr>
        <tr><td>I2C SCL</td><td>42</td><td>AHT10 + ADS1115 (Sıcaklık/Nem, Ana Güç)</td><td>-</td></tr>
        <tr><td>ADC1</td><td>10</td><td>MQ6 (gaz sensörü, analog çıkış)</td><td>Eşik aşılınca panik gibi anında Konteyner alarmını tetikler (bkz Ayarlar → Konteyner Alarm Ayarları, Gaz Alarm Eşiği)</td></tr>
        <tr><td>-</td><td>16</td><td>MQ6 Güç Kontrolü (MOSFET/transistör modül)</td><td>2026-08-26: Adaptif döngü — gündüz ana güç ≥26V'da sürekli açık, gece/düşük güçte yedek akü doluluğuna göre 3dk/6dk/10dk'da bir 60sn açılıp kapanır (bkz main.cpp mq6EtkinCycleMs()) - modülün sinyal ucu bu pine</td></tr>
        <tr><td>-</td><td>18</td><td>GP2Y10 (duman/toz sensörü) LED sürücü kontrol</td><td>MOSFET modülü üzerinden, ~320us darbe — Ayarlar → Konteyner Alarm Ayarları, Duman Alarm Eşiği</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>Not:</b> Reed switch'in ve Swan PIR'in "açık/kapalı" okuma yönü (HIGH=açık mı kapalı mı) kablolamaya göre ters olabilir - <code>/api/status</code>'taki <code>konteyner.kapi_acik</code> / <code>konteyner.swan_pir</code> alanlarından gerçek davranışı görüp gerekirse kod tarafında (main.cpp, <code>konteynerSensorleriOku()</code>) tek satır değiştirerek düzeltilir. Siren/Lamba röleleriniz aktif-LOW ise aynı şekilde <code>alarmLedGuncelle()</code>'daki <code>digitalWrite</code> satırları ters çevrilir. MPPT bağlantısı için adım adım kılavuz: <code>docs/mppt-baglanti-kilavuzu.html</code>; yedek akü kablolaması için: <code>docs/yedek-aku-baglanti-kilavuzu.html</code>.</p>
      <p style="font-size:12px;color:var(--muted);margin-top:4px"><b>Serbest/kullanılabilir GPIO'lar</b> (ileride yeni eklenti için): 17, 18, 33, 34, 36, 42. <b>Asla kullanılmaması gerekenler:</b> 0, 3, 45, 46 (strapping/boot pinleri), 26-32 (Quad Flash için ayrılmış).</p>
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
        <tr><td>D4</td><td>Alarm rölesi</td><td>OUTPUT (varsayılan LOW=aktif, NC röle) - polarite web'den (/role/polarite) çalışırken de değiştirilip EEPROM'a kalıcı yazılabilir</td></tr>
        <tr><td>D5</td><td>Nem rölesi</td><td>OUTPUT</td></tr>
        <tr><td>D6</td><td>PIR HC-SR501 hareket sensörü</td><td>INPUT - ESP8266 PIN_READ:6 ile okur (Nano kodu değişmez)</td></tr>
        <tr><td>D7-D12</td><td>Yedek GPIO</td><td>ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik</td></tr>
        <tr><td>D13</td><td>Depo iç lamba rölesi</td><td>OUTPUT (sadece lamba)</td></tr>
        <tr><td>A0-A5</td><td>Yedek GPIO</td><td>Analog + digital I/O</td></tr>
        <tr><td>A6-A7</td><td>Yedek</td><td>Sadece analog input</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>GPIO Komut Protokolü (ESP→Nano):</b> PIN_MODE:<pin>,<mod> | PIN_WRITE:<pin>,<0/1> | PIN_READ:<pin> | PIN_READ_ALL</p>
    </details>

    <details class="card">
      <summary>WiFi & Sistem</summary>
      <div id="bilgi-sistem" style="font-size:13px">Yükleniyor...</div>
      <div id="build-info" style="font-size:12px;color:var(--muted);margin-top:8px">Yükleniyor...</div>
    </details>

    <details class="card">
      <summary>Güncelleme Aralıkları</summary>
      <div id="guncelleme-bilgi" style="font-size:13px">Yükleniyor...</div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">SSE (anlık push): ESP8266/Nano'dan yeni veri gelir gelmez, en geç 1sn'de bir yedek olarak. Tarayıcı 5sn'de bir de yedek polling yapar (SSE koparsa fark edilmesin diye).</p>
    </details>

    <details class="card">
      <summary>Kullanım Kılavuzu</summary>
      <div style="font-size:13px;line-height:1.6">
        <p><b>Alarm Modları</b> (Kontrol → Alarm): 1-Sesli (tetiklenince siren hemen çalışır), 2-Sessiz (siren çalışmaz, sadece bu sayfada/ESP8266'da bildirim), 3-Onaylı (tetiklenince onay bekler, "Tetiklenmeyi Onayla" ile sesli moda geçer). Bu mod artık hem Sudepo Zonu hem Kalburum/Konteyner için ORTAK - Konteyner PIR'ı eskale olup gerçek alarma dönüştüğünde de aynı moda göre davranır (bkz Ayarlar → Konteyner Alarm Ayarları).</p>
        <p><b>Sustur/Sireni Kapat:</b> Alarm koşulu sürse bile röleyi susturur; koşul temizlenince otomatik sıfırlanır.</p>
        <p><b>Panik:</b> Tetikleyicilerden bağımsız, elle aç/kapat anahtarı gibi çalışır - röleyi zorla açık tutar. Konteyner'in siren+lambasını da (Alarm Modu/etkin-pasif durumundan bağımsız) doğrudan tetikler.</p>
        <p><b>Kapı/PIR/Kaçak/Düşük seviye</b> tetikleyicileri ve gündüz/gece + mod bazlı senaryolar ESP8266 panelinin "Alarm" sekmesinden ayarlanır (bu panel sadece görüntüler ve mod/susturma/onay/panik komutlarını iletir).</p>
        <p><b>WiFi:</b> Ayarlar sekmesinden ev ağını tarayıp kaydedebilirsiniz; AP (<code id="ag-ap-bilgi">-</code>) STA bağlantısından bağımsız her zaman açıktır, ağ ayarları NVS'de kalıcıdır.</p>
        <p><b>Hava Durumu / Yağmur Tahmini:</b> Bahçenin sabit konumu için haftalık tahmin, bu cihazın interneti olduğu anda (örn. telefon hotspotu bağlıyken) otomatik çekilip hafızada saklanır. Bahçede kalıcı internet olmadığı için tahmin bayatlayabilir - 7 günden eski ise dikkate alınmaz ve sulama normal devam eder. Yarın yağmur bekleniyorsa bugünkü sulama otomatik atlanır (su israfını önlemek için).</p>
        <p><b>Telegram Bildirimleri:</b> Bir alarm YENİ tetiklendiğinde (panik, kapı, PIR, kaçak, sensör hatası) sayfa açık olmasa bile telefona Telegram mesajı gider - yalnızca cihazın o an interneti varsa (hotspot bağlıyken) çalışır, aksi halde birkaç dakika tekrar denenir. Bot token/chat ID <code>secrets.h</code> içinde saklanır.</p>
        <p><b>Mimari:</b> Nano (kapı/röle/lamba/PIR/nem çıkışı) ⇄ ESP8266 (sensörler + web) ⇄ RS485 ⇄ ESP32 (bu panel, MQTT yayını). Nano firmware'i genel amaçlı GPIO komutlarıyla (PIN_MODE/WRITE/READ) çalıştığı için donanım eklemelerinde çoğunlukla yeniden flaşlanması gerekmez.</p>
        <p><b>Kumanda:</b> IR kumanda tuş eşleştirmesi ayrı "Kumanda" sekmesinde - "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa basılınca kod yakalanır, sonra hangi komutu çalıştıracağı seçilir.</p>
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

// Alarm tetiklendiginde kisa bip - ESP8266 panelindeki ile ayni desen.
// Sadece "kapali -> acik" gecisinde calar, her renderUI'da degil.
let alarmOncekiDurum = false;
let sysDurumOncekiTehlike = false;
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

// Her ad hangi cihaza (Sudepo/Konteyner) ait oldugunu belirtir.
const tetikleyiciAdlari=['Sudepo: Sol Kapı','Sudepo: Sağ Kapı','Sudepo: PIR (Hareket)','Sudepo: Su Seviyesi','Sudepo: Kaçak','Sudepo: Sensör Hatası'];
function tetikleyenMetni(mask,panicAktif,konteynerPir,konteynerKapi,konteynerSwan,konteynerDuman,konteynerGaz){
  if(panicAktif) return 'Panik (elle açıldı)';
  const l=[];
  for(let i=0;i<6;i++) if(mask&(1<<i)) l.push(tetikleyiciAdlari[i]);
  if(konteynerPir) l.push('Konteyner: HC505-1 PIR');
  if(konteynerKapi) l.push('Konteyner: Kapı');
  if(konteynerSwan) l.push('Konteyner: Swan PIR');
  if(konteynerDuman) l.push('Konteyner: Duman');
  if(konteynerGaz) l.push('Konteyner: Gaz (MQ6)');
  return l.length?l.join(', '):'-';
}

function show(id){
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.nav button').forEach(b=>b.classList.remove('active'));
  document.getElementById('nav-'+id).classList.add('active');
  try{ localStorage.setItem('sonSekme', id); }catch(e){}
}
(function(){
  try{
    const sonSekme = localStorage.getItem('sonSekme');
    if(sonSekme && document.getElementById(sonSekme)) show(sonSekme);
  }catch(e){}
})();

function renderUI(d){
  const mo = d.moisture||{};
  // Su seviyesi - bu degerler ESP8266'dan geliyor (sensorData struct, RS485
  // ile dolduruluyor). ESP8266 baglantisi koparsa backend bu alanlari
  // SIFIRLAMIYOR, son bilinen degeri struct'ta tutmaya devam ediyor (bkz
  // durumJson main.cpp) - bu yuzden burada esp8266_online kontrolu YAPMAZSAK
  // bağlanti koptuktan cok sonra bile son gorulen (artik yanlis/eski) seviye
  // ekranda "canliymis gibi" kalirdi. FIX: baglanti yoksa "--" goster.
  const esp8266Baglı = d.esp8266_online !== false;
  // 7-segment dijital gosterge: normalde yesil, tehlikede (dusuk seviye
  // alarmi) kirmizi - bkz .kpi/.kpi.danger CSS.
  const depoTehlike = !!(d.alarm && d.alarm.low_level);
  ['kpi-pct','kpi-cm','kpi-litre'].forEach(id=>{ const el=$('#'+id); if(el) el.classList.toggle('danger', depoTehlike); });
  if(esp8266Baglı){
    $('#kpi-pct').textContent=(d.level_percent||0).toFixed(1)+'%';
    $('#kpi-cm').textContent=(d.level_cm||0).toFixed(1)+' cm';
    $('#kpi-litre').textContent=(d.level_liters||0).toFixed(0)+' L';
    $('#bar-pct').style.width=Math.max(0,Math.min(100,d.level_percent||0))+'%';
  } else {
    $('#kpi-pct').textContent='--';
    $('#kpi-cm').textContent='--';
    $('#kpi-litre').textContent='--';
    $('#bar-pct').style.width='0%';
  }
  // Dis Sicaklik/Nem - AHT10, Konteyner/ESP32'nin kendi yerel sensoru, ESP8266
  // baglantisindan BAGIMSIZ (eskiden burada ESP8266'nin hep 0.0 gonderdigi
  // olu bir yer tutucu - sensorData.temperature - gosteriliyordu).
  const kzAhtOk = d.konteyner && d.konteyner.aht_ok;
  $('#kpi-temp').textContent = kzAhtOk ? (d.konteyner.sicaklik||0).toFixed(1)+' °C' : '--';
  $('#kpi-nem').textContent = kzAhtOk ? (d.konteyner.nem||0).toFixed(1)+' %' : '--';
  // Alarm
  const ad=$('#alarm-dot'); let at='';
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
  const konteynerPirVar = konteynerEnabledMi && kz.pir_en!==false && !!kz.pir_alarm;
  const konteynerKapiVar = konteynerEnabledMi && kz.kapi_en!==false && !!kz.kapi_acik;
  const konteynerSwanVar = konteynerEnabledMi && kz.swan_en!==false && !!kz.swan_alarm;
  const konteynerDumanVar = konteynerEnabledMi && kz.duman_en!==false && !!kz.duman;
  // Gaz (MQ6) panik gibi davranir - backend'de konteynerAlarmEtkin'den de
  // BAGIMSIZ (bkz alarmLedGuncelle konteynerAcilDurum), bu yuzden burada da
  // konteynerEnabledMi ile gate'lenmez, sadece kendi Etkin anahtarina bakar.
  const konteynerGazVar = kz.gaz_en!==false && !!kz.gaz;
  // Panik, alarm sistemi kapali (enabled===false) olsa bile ESP8266 tarafinda
  // her seyin onunde calisir (bkz esp8266_slave main.cpp panicRoleAktif) - bu
  // yuzden panic iken enabled kontrolunu atlar, aksi halde alarm sistemi
  // kapatilmisken panik basilinca banner hic gorunmuyordu.
  const anyAlarm = !!(d.alarm && ((enabledMi && alarmMask !== 0) || d.alarm.panic || konteynerPirVar || konteynerKapiVar || konteynerSwanVar || konteynerDumanVar || konteynerGazVar));
  if(d.alarm){
    if(d.alarm.panic) at='PANİK AKTİF';
    else if(konteynerGazVar) at='ALARM: Konteyner gaz sızıntısı!';
    else if(d.alarm.leak) at='ALARM: Sudepo kaçak!';
    else if(d.alarm.low_level) at='ALARM: Sudepo düşük seviye!';
    else if(d.alarm.door) at='ALARM: Sudepo kapı açık!';
    else if(alarmMask & 4) at='ALARM: Sudepo hareket algılandı!';
    else if(alarmMask & 32) at='ALARM: Sudepo sensör hatası!';
    else if(konteynerPirVar) at='ALARM: Konteyner hareket!';
    else if(konteynerKapiVar) at='ALARM: Konteyner kapı açık!';
    else if(konteynerSwanVar) at='ALARM: Konteyner Swan PIR hareket!';
    else if(konteynerDumanVar) at='ALARM: Konteyner duman!';
  }
  // ESP8266/Nano ile RS485 iletisimi kopuksa "Sistem Normal" YANLIS bir
  // guven verir - kullanici talebi: iletisim sorunu varsa acikca belirt.
  // Gercek bir alarm zaten varsa onun metnine eklenir (ikisi de gorunur
  // olsun), yoksa "Sistem Normal" yerine dogrudan baglanti sorunu yazilir.
  const baglantiSorunu = (d.esp8266_online === false) || (d.nano_online === false);
  const digerSorun = (d.rtc_ok === false) || !!d.sensor_err;
  if(baglantiSorunu || digerSorun){
    const sorunMetni = (d.esp8266_online === false && d.nano_online === false) ? 'ESP8266 VE Nano bağlantısı yok'
      : (d.esp8266_online === false) ? 'ESP8266 (Sudepo) bağlantısı yok'
      : (d.nano_online === false) ? 'Nano bağlantısı yok'
      : (d.rtc_ok === false) ? 'RTC geçersiz' : 'Sensör hatası';
    at = at ? (at + ' | ' + sorunMetni) : sorunMetni;
  }
  // Sistem durumu 3 kademeli: Tehlike (kırmızı, soft yanıp söner) = aktif
  // alarm veya onay bekleniyor; Kritik (sarı) = alarm yok ama bağlantı/RTC/
  // sensör sorunu var; Normal (yeşil) = ikisi de yok. "Sistem Normal" metni
  // KALDIRILDI (kullanıcı talebi) - durum sadece ışıkla anlatılır, metin
  // sadece bir sorun/alarm varsa gösterilir.
  const bekliyorGenel = !!((d.alarm && d.alarm.pending) || (d.konteyner && d.konteyner.pending));
  const tehlikeVar = anyAlarm || bekliyorGenel;
  const kritikVar = !tehlikeVar && (baglantiSorunu || digerSorun);
  ad.className = 'sysdot ' + (tehlikeVar ? 'tehlike' : (kritikVar ? 'kritik' : 'normal'));
  $('#alarm-text').textContent = tehlikeVar ? (bekliyorGenel && !anyAlarm ? ('ONAY BEKLİYOR - '+at) : at) : (kritikVar ? at : '');
  // Yeni bir tehlike baslarsa listeyi hemen tazele (aksi halde periyodik
  // alarmLoguYukle() dongusune kadar - en fazla 15sn - eski liste gorunur).
  if(tehlikeVar && !sysDurumOncekiTehlike) alarmLoguYukle();
  sysDurumOncekiTehlike = tehlikeVar;
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
      // d.alarm.muted sadece Sudepo/ESP8266'yi yansitir, Konteyner ayri (bkz konteynerSusturuldu, main.cpp).
      const konteynerSusturulduMu = !!(d.konteyner && d.konteyner.susturuldu);
      const herhangiSusturulmus = (d.alarm && d.alarm.muted) || konteynerSusturulduMu;
      if(!panikAktif){
        if(herhangiSusturulmus) msg += ' (Susturuldu)';
        const tk = tetikleyenMetni((d.alarm&&d.alarm.trigger_mask)||0, false, konteynerPirVar, konteynerKapiVar, konteynerSwanVar, konteynerDumanVar, konteynerGazVar);
        msg += ' | Tetikleyen: '+tk;
      }
      let html = '⚠ '+msg;
      if(panikAktif){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/panic\')">Panik Kapat</button></div>';
      } else if(bekliyor){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/alarm/onayla\')">Sesli</button><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/onayla_lamba\')">Sessiz (Lamba)</button></div>';
      } else if(anyAlarm){
        const susLabel = herhangiSusturulmus ? 'Susturmayi Kaldir' : 'Sustur/Sireni Kapat';
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/mute\')">'+susLabel+'</button></div>';
      }
      ban.innerHTML = html;
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
  const sw=$('#sum-wifi'); if(sw) sw.textContent = d.wifi_baglibi&&d.wifi_ssid ? d.wifi_ssid : (d.wifi_ssid?'bağlanamadı':'tanımsız');
  // Cihaz durumları
  const esp8266Ok = d.esp8266_online !== false;
  const nanoOk    = d.nano_online !== false;
  const espBadge=$('#esp8266-badge'), nanoBadge=$('#nano-badge');
  espBadge.textContent=esp8266Ok?'OK':'Offline';
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
  // Konteyner Zonu PIR - durum + ayar alanlari (odaklıyken üzerine yazma).
  // Var/Yok metni yerine LED gostergesi: aktifken yanan (kirmizi glow), pasifken sonuk.
  const kzp=$('#kz-pir'); if(kzp) kzp.classList.toggle('on', !!kz.pir);
  const kzsw=$('#kz-swan'); if(kzsw) kzsw.classList.toggle('on', !!kz.swan_pir);
  const kzk=$('#kz-kapi'); if(kzk) kzk.classList.toggle('on', !!kz.kapi_acik);
  const kzav=$('#kz-duman'); if(kzav) kzav.classList.toggle('on', !!kz.duman);
  const kzgv=$('#kz-gaz'); if(kzgv) kzgv.classList.toggle('on', !!kz.gaz);
  const kzsic=$('#kz-sicaklik'); if(kzsic) kzsic.textContent = kz.aht_ok ? (kz.sicaklik||0).toFixed(1)+' °C' : '--';
  const kznem=$('#kz-nem'); if(kznem) kznem.textContent = kz.aht_ok ? (kz.nem||0).toFixed(1)+' %' : '--';
  const kzmq=$('#kz-mq6'); if(kzmq) kzmq.textContent = (kz.mq6_volt!=null) ? kz.mq6_volt.toFixed(2)+'V ('+kz.mq6_raw+') '+(kz.mq6_powered?'[CANLI]':'[ISITICI KAPALI - ESKI DEGER]') : '--';
  const kzmqt=$('#kz_mq6Test'); if(kzmqt && document.activeElement!==kzmqt) kzmqt.checked = !!kz.mq6_test_modu;
  const kzgp=$('#kz-gp2y10'); if(kzgp) kzgp.textContent = (kz.gp2y10_volt!=null) ? kz.gp2y10_volt.toFixed(2)+'V ('+kz.gp2y10_raw+')' : '--';
  const kza=$('#kz-alarm'); if(kza){ const kzAktif=kz.pir_alarm||(kz.kapi_en!==false&&kz.kapi_acik)||(kz.swan_en!==false&&kz.swan_alarm)||(kz.duman_en!==false&&kz.duman)||(kz.gaz_en!==false&&kz.gaz); kza.classList.toggle('on', !!kzAktif && !kz.pending); kza.classList.toggle('pending', !!kz.pending); const kzat=$('#kz-alarm-txt'); if(kzat) kzat.textContent = kz.pending?'ONAY BEKLİYOR':''; }
  const kzs=$('#kz-siren'); if(kzs) kzs.classList.toggle('on', !!kz.siren);
  const kzl=$('#kz-lamba'); if(kzl) kzl.classList.toggle('on', !!kz.lamba);
  const kzpe=$('#kz_pirEtkin'); if(kzpe && document.activeElement!==kzpe) kzpe.checked = kz.pir_en!==false;
  const kzke=$('#kz_kapiEtkin'); if(kzke && document.activeElement!==kzke) kzke.checked = kz.kapi_en!==false;
  const kzsue=$('#kz_swanEtkin'); if(kzsue && document.activeElement!==kzsue) kzsue.checked = kz.swan_en!==false;
  const kzale=$('#kz_dumanEtkin'); if(kzale && document.activeElement!==kzale) kzale.checked = kz.duman_en!==false;
  const kzge=$('#kz_gazEtkin'); if(kzge && document.activeElement!==kzge) kzge.checked = kz.gaz_en!==false;
  const kzges=$('#kz_gazEsik'); if(kzges && !kzges.matches(':focus') && !yakinKorumali('kz_gazEsik') && kz.gaz_esik!=null) kzges.value=kz.gaz_esik;
  const kzdes=$('#kz_dumanEsik'); if(kzdes && !kzdes.matches(':focus') && !yakinKorumali('kz_dumanEsik') && kz.duman_esik!=null) kzdes.value=kz.duman_esik;
  const kzpt=$('#kz_pirTutma'); if(kzpt && !kzpt.matches(':focus') && !yakinKorumali('kz_pirTutma') && kz.pir_tutma!=null) kzpt.value=kz.pir_tutma;
  const kzpo=$('#kz_pirOnay'); if(kzpo && !kzpo.matches(':focus') && !yakinKorumali('kz_pirOnay') && kz.pir_onay!=null) kzpo.value=kz.pir_onay;
  const kzst=$('#kz_swanTutma'); if(kzst && !kzst.matches(':focus') && !yakinKorumali('kz_swanTutma') && kz.swan_tutma!=null) kzst.value=kz.swan_tutma;
  const kzso=$('#kz_swanOnay'); if(kzso && !kzso.matches(':focus') && !yakinKorumali('kz_swanOnay') && kz.swan_onay!=null) kzso.value=kz.swan_onay;
  // Nano IO - dashboard (K1/K2/R/LAMBA hepsi ESP8266'nin tek RS485 mesajinda
  // geliyor - esp8266Baglı degilse hepsi ayni sekilde bayat kalir, level/
  // sicaklik icin kullanilan ayni tazelik esigiyle '--' gosterilir)
  $('#d1').textContent=esp8266Ok?(d.nano.door1?'Açık':'Kapalı'):'--';
  $('#d2').textContent=esp8266Ok?(d.nano.door2?'Açık':'Kapalı'):'--';
  $('#rl').textContent=esp8266Ok?(d.nano.relay?'AKTİF':'Pasif'):'--';
  $('#lm').textContent=esp8266Ok?(d.nano.lamp?'Açık':'Kapalı'):'--';
  // Nem röle (RS485 Cihaz Durumu)
  const mr=$('#mr'); if(mr) mr.textContent=esp8266Ok?((d.moisture&&d.moisture.output)?'Açık':'Kapalı'):'--';
  // === BUTON METİNLERİ - SUNUCUDAN GELIR, local state YOK ===
  // Her buton her zaman taze sunucu verisiyle guncellenir - eskiden busySet
  // ile "islemde" butonlar atlaniyordu, bu da tepkinin gec/tikanik hissi
  // vermesine neden oluyordu (kullanici bildirdi), kaldirildi.
  $('#lamba-btn').textContent = 'Sudepo Zonu: ' + (esp8266Ok ? (d.nano.lamp ? 'Kapat' : 'Aç') : '--');
  { const klb=$('#konteyner-lamba-btn'); if(klb) klb.textContent = 'Konteyner Zonu: ' + ((d.konteyner&&d.konteyner.lamba) ? 'Kapat' : 'Aç'); }
  $('#alarm-btn').textContent = 'Sudepo Zonu: ' + ((d.alarm&&d.alarm.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç');
  { const kab=$('#konteyner-alarm-btn'); if(kab) kab.textContent = 'Konteyner Zonu: ' + ((d.konteyner&&d.konteyner.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç'); }
  { const ss=$('#sum-sudepo'); if(ss) ss.textContent = (d.alarm&&d.alarm.enabled!==false) ? 'Aktif' : 'Kapalı'; }
  { const sk=$('#sum-konteyner'); if(sk) sk.textContent = (d.konteyner&&d.konteyner.enabled!==false) ? 'Aktif' : 'Kapalı'; }
  { const pb=$('#panic-btn'); if(pb) pb.textContent = (d.alarm&&d.alarm.panic) ? 'Panik Açık' : 'Panik'; }
  { const ams=$('#alarm-mod-sel'); if(ams && d.alarm && d.alarm.mode) ams.value=String(d.alarm.mode); }
  { const amb=$('#alarm-mute-btn'); if(amb) amb.textContent = (d.alarm&&d.alarm.muted) ? 'Susturma Kaldir' : 'Sustur/Sireni Kapat'; }
  { const msb=$('#moisture-settings-toggle-btn'); if(msb) msb.textContent = mo.output ? 'Kapat' : 'Aç'; }
  { const sab=$('#moisture-settings-auto-btn'); if(sab) sab.textContent = mo.auto ? 'Manuel' : 'Otomatik'; }
  if(typeof d.telegram_aktif==='boolean'){ telegramAktifBilinen=d.telegram_aktif; const tb=$('#telegram-ac-kapa-btn'); if(tb) tb.textContent=telegramAktifBilinen?'🔔 Bildirimler Açık':'🔕 Bildirimler Kapalı'; const st=$('#sum-telegram'); if(st) st.textContent=telegramAktifBilinen?'Açık':'Kapalı'; }
  // Nem verileri - ESP8266 uzerinden geliyor, ayni tazelik esigiyle korunur
  // (bkz. level/sicaklik icin yukarida yapilan esp8266Baglı fix'i - kullanici
  // ayni donmus-deger sorununun nem icin de var oldugunu bildirdi)
  const mkpi=$('#kpi-moisture'); if(mkpi) mkpi.textContent=esp8266Ok?(mo.percent||0).toFixed(1)+'%':'--';
  const mraw=$('#moisture-raw'); if(mraw) mraw.textContent=esp8266Ok?(mo.raw||0):'--';
  const mout=$('#moisture-output'); if(mout) mout.textContent=esp8266Ok?(mo.output?'Açık':'Kapalı'):'--';
  const mmod=$('#moisture-mode'); if(mmod) mmod.textContent=esp8266Ok?(mo.auto?'Otomatik':'Manuel'):'--';
  // Ayarlar sekmesi nem göstergeleri
  const smv=$('#settings-moisture-val'); if(smv) smv.textContent=esp8266Ok?(mo.percent||0).toFixed(1)+'%':'--';
  const smo=$('#settings-moisture-out'); if(smo) smo.textContent=esp8266Ok?(mo.output?'Açık':'Kapalı'):'--';
  const smm=$('#settings-moisture-mod'); if(smm) smm.textContent=esp8266Ok?(mo.auto?'Otomatik':'Manuel'):'--';
  { const sn=$('#sum-nem'); if(sn) sn.textContent = esp8266Ok ? ((mo.output?'Açık':'Kapalı')+' - '+(mo.auto?'Otomatik':'Manuel')) : 'Bağlantı yok'; }
  const sml=$('#moisture-settings-low'); if(sml&&!sml.matches(':focus')&&!yakinKorumali('moisture-settings-low')) sml.value=mo.low||0;
  const smh=$('#moisture-settings-high'); if(smh&&!smh.matches(':focus')&&!yakinKorumali('moisture-settings-high')) smh.value=mo.high||0;
  // Batarya (MPPT) - kendi ayri "online" bayragi var (esp8266Ok'tan bagimsiz,
  // MPPT UART2 hatti ESP8266 RS485'inden tamamen ayri bir bus)
  const bat = d.battery || {};
  const batOk = bat.online !== false;
  const bkpi=$('#kpi-batarya'); if(bkpi){ bkpi.textContent = batOk ? (bat.voltage||0).toFixed(1)+' V' : '--'; bkpi.classList.toggle('danger', !!(batOk && bat.kritik)); }
  const bsoc=$('#batarya-soc'); if(bsoc) bsoc.textContent = (batOk && bat.soc!=null && bat.soc>=0) ? ('('+bat.soc+'%)') : '';
  const bdur=$('#batarya-durum');
  if(bdur){
    if(!bat.koruma_aktif) bdur.textContent = batOk ? 'İzleniyor (koruma kapalı)' : 'Bağlantı yok';
    else bdur.textContent = !batOk ? 'Bağlantı yok' : (bat.kritik ? 'KRİTİK - sulama/lamba kesildi' : 'Normal');
    bdur.style.color = (batOk && bat.kritik) ? 'var(--danger)' : '';
  }
  const bpv=$('#batarya-pv'); if(bpv) bpv.textContent = batOk ? (bat.pv_watt||0).toFixed(0)+'W ('+(bat.pv_amp||0).toFixed(1)+'A)' : '--';
  const byuk=$('#batarya-yuk'); if(byuk) byuk.textContent = batOk ? (bat.load_watt||0).toFixed(0)+'W ('+(bat.load_amp||0).toFixed(1)+'A)' : '--';
  const bkalan=$('#batarya-kalan');
  if(bkalan){
    if(!batOk) bkalan.textContent='';
    else if(bat.kalan_saat==null || bat.kalan_saat<0) bkalan.textContent = (bat.pv_watt>bat.load_watt) ? '🔆 Şarj oluyor' : '';
    else bkalan.textContent = '⏳ Bu tüketimle ~'+bat.kalan_saat.toFixed(1)+' saat kaldı';
  }
  { const bkb=$('#batarya-koruma-btn'); if(bkb) bkb.textContent = bat.koruma_aktif ? 'Korumayı Kapat' : 'Korumayı Aç'; const sbk=$('#sum-batkoruma'); if(sbk) sbk.textContent = bat.koruma_aktif ? 'Açık' : 'Kapalı'; }
  // İnvertör sekmesi - tum QPIGS/QMOD/QPIWS/QPIRI detaylari
  {
    const inv = bat.inverter || {};
    const set = (id, val) => { const el = $(id); if(el) el.textContent = val; };
    set('#inv-baglanti', batOk ? '✅ Bağlı' : '❌ Bağlantı yok');
    set('#inv-mode', inv.mode_ok ? (inv.mode_text||'-') : '-');
    set('#inv-warn', !inv.warn_ok ? '-' : (inv.warn_list ? ('⚠️ '+inv.warn_list) : '✅ Uyarı/arıza yok'));
    set('#inv-load-on', batOk ? (inv.load_on?'Açık':'Kapalı') : '-');
    set('#inv-charging', batOk ? ((inv.charging_on?'Genel:Açık':'Genel:Kapalı')+' / '+(inv.scc_charging_on?'Solar:Açık':'Solar:Kapalı')+' / '+(inv.ac_charging_on?'AC:Açık':'AC:Kapalı')) : '-');
    set('#inv-batt-v', batOk ? (bat.voltage||0).toFixed(2)+' V' : '-');
    set('#inv-soc', batOk && bat.soc!=null ? bat.soc+' %' : '-');
    set('#inv-batt-charge-a', batOk ? (inv.batt_charge_a||0).toFixed(1)+' A' : '-');
    set('#inv-batt-discharge-a', batOk ? (inv.batt_discharge_a||0).toFixed(1)+' A' : '-');
    set('#inv-pv-v', batOk ? (bat.pv_volt||0).toFixed(1)+' V' : '-');
    set('#inv-pv-a', batOk ? (bat.pv_amp||0).toFixed(1)+' A' : '-');
    set('#inv-pv-w', batOk ? (bat.pv_watt||0).toFixed(0)+' W' : '-');
    set('#inv-load-v', batOk ? (bat.load_volt||0).toFixed(1)+' V' : '-');
    set('#inv-load-w', batOk ? (bat.load_watt||0).toFixed(0)+' W / '+(inv.apparent_va||0).toFixed(0)+' VA' : '-');
    set('#inv-load-pct', batOk ? (inv.load_pct||0)+' %' : '-');
    set('#inv-kalan', (batOk && bat.kalan_saat!=null && bat.kalan_saat>=0) ? ('~'+bat.kalan_saat.toFixed(1)+' saat') : '-');
    set('#inv-grid', batOk ? (inv.grid_volt||0).toFixed(1)+'V / '+(inv.grid_hz||0).toFixed(1)+'Hz' : '-');
    set('#inv-ac-hz', batOk ? (inv.ac_out_hz||0).toFixed(1)+' Hz' : '-');
    set('#inv-bus-v', batOk ? (inv.bus_volt||0).toFixed(1)+' V' : '-');
    set('#inv-temp', batOk ? (inv.temp_c||0).toFixed(0)+' °C' : '-');
    const battTipleri = {0:'AGM',1:'Sulu',2:'Kullanıcı',3:'PYL',4:'SH'};
    const oncelikler = {0:'Şebeke önce',1:'Solar önce',2:'SBU',3:'Sadece solar'};
    set('#inv-ri-battv', inv.ri_ok ? (inv.ri_batt_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-recharge', inv.ri_ok ? (inv.ri_batt_recharge_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-under', inv.ri_ok ? (inv.ri_batt_under_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-bulk', inv.ri_ok ? (inv.ri_batt_bulk_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-float', inv.ri_ok ? (inv.ri_batt_float_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-type', inv.ri_ok ? (battTipleri[inv.ri_batt_type]||('#'+inv.ri_batt_type)) : '-');
    set('#inv-ri-maxac', inv.ri_ok ? inv.ri_max_ac_charge_a+' A' : '-');
    set('#inv-ri-maxchg', inv.ri_ok ? inv.ri_max_charge_a+' A' : '-');
    set('#inv-ri-outpri', inv.ri_ok ? (oncelikler[inv.ri_out_priority]||('#'+inv.ri_out_priority)) : '-');
    set('#inv-ri-chgpri', inv.ri_ok ? (oncelikler[inv.ri_charger_priority]||('#'+inv.ri_charger_priority)) : '-');
  }
  // Yedek Aku - pasif donanimsal failover, sadece bilgi amacli gosterge
  const yak = d.yedek_aku || {};
  const yakOk = yak.guncel !== false;
  const yakKpi=$('#kpi-yedek-aku'); if(yakKpi){ yakKpi.textContent = yakOk ? (yak.volt||0).toFixed(1)+' V' : '--'; yakKpi.classList.toggle('danger', !!(yakOk && yak.durum==='zayif')); }
  const yakDurum=$('#yedek-aku-durum');
  if(yakDurum){
    const etiket = {dolu:'✅ Dolu/boşta (devrede değil)', devrede:'🔋 Devrede', zayif:'⚠️ Zayıf - şarj/değişim gerekir', bilinmiyor:'-'};
    yakDurum.textContent = yakOk ? (etiket[yak.durum]||yak.durum||'-') : 'Bağlantı yok';
    yakDurum.style.color = (yakOk && yak.durum==='zayif') ? 'var(--danger)' : '';
  }
  // Ana Guc (ADS1115) - 3 kademeli bildirim + Acil Durum Lambasi
  const ag = d.ana_guc || {};
  const agOk = ag.ok !== false;
  const agKpi=$('#kpi-ana-guc'); if(agKpi){ agKpi.textContent = agOk ? (ag.volt||0).toFixed(1)+' V' : '--'; agKpi.classList.toggle('danger', !!(agOk && ag.kademe>=2)); }
  const agDurum=$('#ana-guc-durum');
  if(agDurum){
    const kademeEtiket = {0:'✅ Normal', 1:'⚠️ Düşük', 2:'🔶 Kritik', 3:'🔴 Acil'};
    agDurum.textContent = agOk ? (kademeEtiket[ag.kademe]||'-') : 'Bağlantı yok';
    const sag=$('#sum-anaguc'); if(sag) sag.textContent = agOk ? (ag.volt||0).toFixed(1)+'V - '+(kademeEtiket[ag.kademe]||'-') : 'Bağlantı yok';
    agDurum.style.color = (agOk && ag.kademe>=2) ? 'var(--danger)' : '';
  }
  const ae1=$('#ana-guc-esik1'); if(ae1&&!ae1.matches(':focus')&&!yakinKorumali('ana-guc-esik1')&&ag.esik1!=null) ae1.value=ag.esik1;
  const ae2=$('#ana-guc-esik2'); if(ae2&&!ae2.matches(':focus')&&!yakinKorumali('ana-guc-esik2')&&ag.esik2!=null) ae2.value=ag.esik2;
  const ae3=$('#ana-guc-esik3'); if(ae3&&!ae3.matches(':focus')&&!yakinKorumali('ana-guc-esik3')&&ag.esik3!=null) ae3.value=ag.esik3;
  const alBtn=$('#acil-lamba-btn');
  if(alBtn){
    alBtn.textContent = ag.acil_lamba ? '🔴 ACİL LAMBA AÇIK (Kapat)' : '⚪ Acil Durum Lambası (Kapalı)';
    alBtn.style.background = ag.acil_lamba ? 'var(--danger)' : '';
  }
  const bkv=$('#batarya-kesme'); if(bkv&&!bkv.matches(':focus')&&!yakinKorumali('batarya-kesme')&&bat.kesme_volt!=null) bkv.value=bat.kesme_volt;
  const bgv=$('#batarya-geri'); if(bgv&&!bgv.matches(':focus')&&!yakinKorumali('batarya-geri')&&bat.geri_volt!=null) bgv.value=bat.geri_volt;
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
  if(herhangiBirAlanaYaziliyorMu()) return;
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
// Kullanici herhangi bir input/select/textarea'da yaziyorken (odaklanmisken)
// arka plandaki otomatik guncellemeler (SSE + guncelle()) O ANKI render'i
// tamamen atlar - "input/selectbox'lar agresif, yazarken siliniyor" tipi
// sorunlarin KOKUNDEN onlenmesi icin genel bir kilit (tek tek her alana
// :focus kontrolu eklemek yerine, hicbir yazma islemi arka plan
// guncellemesiyle asla yarismasin diye tum renderUI() cagrilarini kapsar).
function herhangiBirAlanaYaziliyorMu(){
  const a = document.activeElement;
  if(!a) return false;
  const t = a.tagName;
  return t==='INPUT' || t==='SELECT' || t==='TEXTAREA';
}
function connectSSE(){
  if(_es) return;
  _es = new EventSource('/events');
  _es.onmessage = function(e){
    if(herhangiBirAlanaYaziliyorMu()) return;
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

// sendCommand: butonu KİLİTLEMİYOR/disable etmiyor artık - tekrar basma
// engeli, komut surerken butonda gorunen "yasak/dur" imleci gibi UI
// surtunmesi gecikme hissi yaratiyordu. RS485 tarafinda gercek komut zaten
// mutex ile guvenli sekilde siraya giriyor (bkz RS485Kilit), SET_* komutlari
// da idempotent (PANIC/ALARM_MUTE dahil) - art arda hizli tiklamak zararsiz.
function sendCommand(btnId, path, resultId, label){
  api(path).then(d=>{
    if(resultId){ const el=$(resultId); if(el) el.textContent=d.mesaj||''; }
  }).finally(()=>{
    // Sunucudan taze veri çek — buton metnini renderUI yazar
    fetch('/api/status')
      .then(r=>r.json())
      .then(d=>{ renderUI(d); })
      .catch(()=>{})
      .finally(()=>{
        // Sonucu birkaç saniye göster sonra temizle
        if(resultId) setTimeout(()=>{ const el=$(resultId); if(el) el.textContent=''; }, 4000);
      });
  });
}

function toggleLamba(){
  // Mevcut durumu buton metninden değil, sunucu state'inden bil
  // Butonda 'Kapat' yazıyorsa lamba açık demek → hedef=0
  const acik = $('#lamba-btn').textContent.trim().endsWith('Kapat');
  sendCommand('#lamba-btn', '/api/lamba?durum='+(acik?0:1), '#lamba-sonuc');
}
function toggleKonteynerLamba(){
  const acik = $('#konteyner-lamba-btn').textContent.trim().endsWith('Kapat');
  sendCommand('#konteyner-lamba-btn', '/api/konteyner/lamba?durum='+(acik?0:1), '#konteyner-lamba-sonuc');
}
function toggleAcilLamba(){
  const acik = $('#acil-lamba-btn').textContent.includes('AÇIK');
  sendCommand('#acil-lamba-btn', '/api/acil-lamba?durum='+(acik?0:1), '#acil-lamba-sonuc');
}
function anaGucEsikKaydet(){
  yakinDuzenlendi('ana-guc-esik1'); yakinDuzenlendi('ana-guc-esik2'); yakinDuzenlendi('ana-guc-esik3');
  const e1=$('#ana-guc-esik1').value, e2=$('#ana-guc-esik2').value, e3=$('#ana-guc-esik3').value;
  api('/api/ana-guc-esik?esik1='+encodeURIComponent(e1)+'&esik2='+encodeURIComponent(e2)+'&esik3='+encodeURIComponent(e3)).then(()=>{
    $('#ana-guc-esik-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#ana-guc-esik-sonuc').textContent='Hata oluştu'; });
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
// Banner butonlari icin - lamba/alarm butonlarindaki ayni gecikme-hissi
// sorunu (disable + tekrar-tiklama engeli) burada da vardi, ayni sekilde
// kaldirildi - RS485/durum tarafinda gercek koruma zaten mutex+idempotent
// komutlarla saglaniyor (bkz sendCommand aciklamasi).
function bannerAksiyon(btn, path){
  api(path).finally(()=>{
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

function konteynerSwanKaydet(){
  yakinDuzenlendi('kz_swanTutma'); yakinDuzenlendi('kz_swanOnay');
  const tutma=$('#kz_swanTutma').value;
  const onay=$('#kz_swanOnay').value;
  api('/api/konteyner/swan_ayar?tutma='+encodeURIComponent(tutma)+'&onay='+encodeURIComponent(onay)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerSensorAktifKaydet(){
  const pir = $('#kz_pirEtkin').checked?1:0;
  const kapi = $('#kz_kapiEtkin').checked?1:0;
  const swan = $('#kz_swanEtkin').checked?1:0;
  const duman = $('#kz_dumanEtkin').checked?1:0;
  const gaz = $('#kz_gazEtkin').checked?1:0;
  api('/api/konteyner/sensor_aktif?pir='+pir+'&kapi='+kapi+'&swan='+swan+'&duman='+duman+'&gaz='+gaz).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerMq6TestKaydet(){
  const durum = $('#kz_mq6Test').checked?1:0;
  api('/api/konteyner/mq6_test?durum='+durum).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerGazAyarKaydet(){
  yakinDuzenlendi('kz_gazEsik');
  const esik=$('#kz_gazEsik').value;
  api('/api/konteyner/gaz_ayar?esik='+encodeURIComponent(esik)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerDumanAyarKaydet(){
  yakinDuzenlendi('kz_dumanEsik');
  const esik=$('#kz_dumanEsik').value;
  api('/api/konteyner/duman_ayar?esik='+encodeURIComponent(esik)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function bateryaKorumaToggle(){
  const acik = $('#batarya-koruma-btn').textContent.trim().startsWith('Korumayı Kapat');
  api('/api/batarya/ayar?aktif='+(acik?0:1)).then(()=>{
    $('#batarya-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#batarya-sonuc').textContent='Hata oluştu'; });
}
function bateryaEsikKaydet(){
  yakinDuzenlendi('batarya-kesme'); yakinDuzenlendi('batarya-geri');
  const kesme=$('#batarya-kesme').value;
  const geri=$('#batarya-geri').value;
  api('/api/batarya/ayar?kesme='+encodeURIComponent(kesme)+'&geri='+encodeURIComponent(geri)).then(()=>{
    $('#batarya-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#batarya-sonuc').textContent='Hata oluştu'; });
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
  $('#sz-grid-sensorEtkin').innerHTML=szGridHtml('sz_Etkin');
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
    szSetTrigger('sz_Etkin', d.alarmSensorEtkin==null?255:d.alarmSensorEtkin);
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
    alarmSensorEtkin:szCalcTrigger('sz_Etkin'),
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
function alarmLoguYukle(){
  api('/api/alarm/log').then(list=>{
    const el=$('#alarm-log-list'); if(!el) return;
    if(!Array.isArray(list) || list.length===0){ el.textContent='Kayıtlı alarm yok'; return; }
    el.innerHTML = list.map(k=>'<div style="padding:3px 0;border-bottom:1px solid var(--border)"><b>'+(k.zaman||'-')+'</b> - '+(k.baslik||'-')+' <span style="color:var(--muted)">('+(k.tetikleyen||'-')+')</span></div>').join('');
  }).catch(()=>{});
}
setInterval(alarmLoguYukle, 15000); alarmLoguYukle();

// === IR KUMANDA - OGRENME/ESLESTIRME ===
const irKomutAdlari={LAMBA_TOGGLE:'Lamba Aç/Kapat (tek tuş)',LAMBA_AC:'Lamba Aç',LAMBA_KAPAT:'Lamba Kapat',ALARM_TOGGLE:'Alarm Aç/Kapat (tek tuş)',ALARM_AC:'Alarm Aç',ALARM_KAPAT:'Alarm Kapat','ALARM_MOD=1':'Mod: Sesli','ALARM_MOD=2':'Mod: Sessiz','ALARM_MOD=3':'Mod: Onaylı',ALARM_SUSTUR:'Sustur',ALARM_ONAYLA:'Onayla',KAPI_TOGGLE:'Kapı Aç/Kapat (tek tuş)',KAPI_AC:'Kapı Aç',KAPI_KAPAT:'Kapı Kapat',PANIK:'Panik'};
let irOgrenPolling=null;
function irListesiYukle(){
  fetch('/api/ir/liste').then(r=>r.json()).then(list=>{
    const el=$('#ir-liste'); if(!el) return;
    if(!Array.isArray(list)||!list.length){ el.innerHTML='<p class="muted">Henüz tanımlı tuş yok.</p>'; return; }
    el.innerHTML=list.map(e=>
      '<div class="row" style="justify-content:space-between;align-items:center;padding:4px 0;border-bottom:1px solid var(--input-border)">'
      +'<span>'+(e.etiket||e.komut)+' <span class="muted" style="font-size:11px">(0x'+e.kod+')</span></span>'
      +'<button class="btn-sil" onclick="irSil(\''+e.kod+'\')">🗑</button></div>'
    ).join('');
  }).catch(()=>{});
}
function irOgrenBaslat(){
  fetch('/api/ir/ogren_baslat').then(()=>{
    $('#ir-ogren-durum').innerHTML='Kumandada bir tuşa basın... (20sn içinde)';
    if(irOgrenPolling) clearInterval(irOgrenPolling);
    irOgrenPolling=setInterval(irOgrenKontrolEt, 800);
  }).catch(()=>{});
}
function irOgrenKontrolEt(){
  const buIstekIcinPolling = irOgrenPolling; // bkz asagidaki "GEC GELEN YANIT" notu
  fetch('/api/ir/ogren_durum').then(r=>r.json()).then(d=>{
    // GEC GELEN YANIT KORUMASI: 800ms'de bir istek atiliyor, fetch()'ler
    // gonderildikleri sirayla DONMEK ZORUNDA DEGIL - kod yakalanip form
    // gosterildikten (irOgrenPolling=null yapilip interval durdurulduktan)
    // SONRA, daha ONCE gonderilmis ama gec gelen bir istek burada hala
    // "hazir:false" ile cozulup formu (select dahil) "Kumandada bir tusa
    // basin..." yazisiyla EZIYORDU - kullanicinin "secim yapamiyorum, liste
    // hemen kayboluyor, hizli davranirsam yakaliyorum" sikayetinin sebebi
    // buydu. Bu istegi atarken gecerli olan polling referansi artik
    // gecersizse (baska bir yanit zaten islenip interval durdurulmus/
    // yeniden baslatilmissa), bu YANITI TAMAMEN YOKSAY.
    if (buIstekIcinPolling !== irOgrenPolling) return;
    const teshis=' <span class="muted" style="font-size:11px">(teşhis: '+d.denemeSayisi+' deneme, son protokol: '+d.sonProtokol+', darbe: '+d.sonRawlen+')</span>';
    if(d.hazir){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      irKodAtamaFormuGoster(d.kod);
    } else if(d.zamanAsimi){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      $('#ir-ogren-durum').innerHTML='Zaman aşımı, tuş algılanamadı - tekrar deneyin.'+(d.denemeSayisi>0?' (Kumanda algılandı ama protokolü tanınamadı - "'+d.sonProtokol+'" olarak geldi, bu genelde desteklenmeyen/bozuk sinyal demektir.)':' (Hiç IR sinyali algılanmadı - alıcıya doğru mu tutuyorsunuz?)');
    } else {
      $('#ir-ogren-durum').innerHTML='Kumandada bir tuşa basın... (20sn içinde)'+teshis;
    }
  }).catch(()=>{});
}
function irKodAtamaFormuGoster(kod){
  let secenekler='';
  for(const k in irKomutAdlari) secenekler+='<option value="'+k+'">'+irKomutAdlari[k]+'</option>';
  $('#ir-ogren-durum').innerHTML='Kod alındı: <b>0x'+kod+'</b><br>'
    +'<select id="ir-komut-sec" style="margin-top:6px">'+secenekler+'</select><br>'
    +'<input id="ir-not" class="input" placeholder="Not: örn. kumandanın kırmızı tuşu (opsiyonel)" style="margin-top:6px;width:100%;max-width:280px">'
    +'<br><button class="btn btn-yesil" onclick="irKaydet(\''+kod+'\')" style="margin-top:6px">Kaydet</button>';
}
function irKaydet(kod){
  const sel=$('#ir-komut-sec'); const komut=sel.value; const komutAdi=sel.options[sel.selectedIndex].text;
  const not_=($('#ir-not')?.value||'').trim();
  const etiket=not_?(komutAdi+' — '+not_):komutAdi;
  fetch('/api/ir/kaydet?kod='+kod+'&komut='+encodeURIComponent(komut)+'&etiket='+encodeURIComponent(etiket)).then(()=>{
    $('#ir-ogren-durum').innerHTML='Kaydedildi ✓';
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
  // Invertor detay alanlari (QPIGS tam liste + QMOD + QPIWS + QPIRI)
  // eklendiginden buffer 1536 -> 4096'ya cikarildi.
  DynamicJsonDocument doc(4096);

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
  doc["konteyner"]["swan_pir"] = swanPirVar; // ham anlik pin durumu (Canli Durum gostergesi icin)
  doc["konteyner"]["swan_alarm"] = konteynerSwanEskalasyonOldu; // eskale olmus (GERCEK) alarm - bkz konteynerPirAyar ile ayni desen
  doc["konteyner"]["swan_tutma"] = konteynerSwanTutmaSaniye;
  doc["konteyner"]["swan_onay"] = konteynerSwanOnaySaniye;
  doc["konteyner"]["pir"] = pir2HareketVar;
  doc["konteyner"]["pir_en"] = konteynerPirEtkin;
  doc["konteyner"]["kapi_en"] = konteynerKapiEtkin;
  doc["konteyner"]["swan_en"] = konteynerSwanEtkin;
  doc["konteyner"]["duman"] = konteynerDumanVar;
  doc["konteyner"]["duman_en"] = konteynerDumanEtkin;
  doc["konteyner"]["gaz"] = konteynerGazVar;
  doc["konteyner"]["gaz_en"] = konteynerGazEtkin;
  doc["konteyner"]["gaz_esik"] = konteynerGazEsikVolt;
  doc["konteyner"]["duman_esik"] = konteynerDumanEsikVolt;
  doc["konteyner"]["sicaklik"] = ahtData.sicaklik;
  doc["konteyner"]["nem"] = ahtData.nem;
  doc["konteyner"]["aht_ok"] = ahtData.read_ok;
  doc["konteyner"]["mq6_raw"] = mq6Data.raw;
  doc["konteyner"]["mq6_volt"] = mq6Data.volt;
  doc["konteyner"]["mq6_powered"] = mq6Data.powered; // false ise deger eski (guc dongusunun kapali fazinda), bkz mq6Poll()
  doc["konteyner"]["mq6_test_modu"] = konteynerMq6TestModu;
  doc["konteyner"]["gp2y10_raw"] = gp2y10Data.raw;
  doc["konteyner"]["gp2y10_volt"] = gp2y10Data.volt;
  doc["konteyner"]["pir_alarm"] = konteynerPirEskalasyonOldu; // eskale olmus (GERCEK) alarm
  doc["konteyner"]["susturuldu"] = konteynerSusturuldu;
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

  {
    MpptKilit kilit;
    doc["battery"]["voltage"] = mpptData.battery_voltage;
    doc["battery"]["online"] = ((millis() - mpptData.last_update_ms) < MPPT_STALE_MS) && mpptData.read_ok;
    doc["battery"]["pv_volt"] = mpptData.pv_voltage;
    doc["battery"]["pv_amp"] = mpptData.pv_current;
    doc["battery"]["pv_watt"] = mpptData.pv_power;
    doc["battery"]["load_volt"] = mpptData.load_voltage;
    doc["battery"]["load_amp"] = mpptData.load_current;
    doc["battery"]["load_watt"] = mpptData.load_power;
    doc["battery"]["soc"] = mpptData.battery_soc;
    doc["battery"]["kalan_saat"] = mpptData.kalan_saat;

    // "Invertor" sekmesi icin detay alanlar - ana sayfa bunlari kullanmaz.
    JsonObject inv = doc.createNestedObject("inverter");
    inv["grid_volt"] = mpptData.grid_voltage;
    inv["grid_hz"] = mpptData.grid_freq;
    inv["ac_out_hz"] = mpptData.ac_out_freq;
    inv["apparent_va"] = mpptData.apparent_power;
    inv["load_pct"] = mpptData.load_percent;
    inv["bus_volt"] = mpptData.bus_voltage;
    inv["temp_c"] = mpptData.temperature_c;
    inv["batt_charge_a"] = mpptData.battery_charge_current;
    inv["batt_discharge_a"] = mpptData.battery_discharge_current;
    inv["load_on"] = mpptData.load_on;
    inv["charging_on"] = mpptData.charging_on;
    inv["scc_charging_on"] = mpptData.scc_charging_on;
    inv["ac_charging_on"] = mpptData.ac_charging_on;
    inv["mode_code"] = String(mpptData.mode_code);
    inv["mode_text"] = mpptData.mode_text;
    inv["mode_ok"] = mpptData.mode_read_ok;
    inv["warn_list"] = mpptData.warn_list;
    inv["warn_ok"] = mpptData.warn_read_ok;
    inv["ri_batt_v"] = mpptData.qpiri_batt_voltage;
    inv["ri_batt_recharge_v"] = mpptData.qpiri_batt_recharge_v;
    inv["ri_batt_under_v"] = mpptData.qpiri_batt_under_v;
    inv["ri_batt_bulk_v"] = mpptData.qpiri_batt_bulk_v;
    inv["ri_batt_float_v"] = mpptData.qpiri_batt_float_v;
    inv["ri_batt_type"] = mpptData.qpiri_batt_type;
    inv["ri_max_ac_charge_a"] = mpptData.qpiri_max_ac_charge_a;
    inv["ri_max_charge_a"] = mpptData.qpiri_max_charge_a;
    inv["ri_out_priority"] = mpptData.qpiri_out_source_priority;
    inv["ri_charger_priority"] = mpptData.qpiri_charger_source_priority;
    inv["ri_ok"] = mpptData.qpiri_read_ok;
  }
  doc["battery"]["koruma_aktif"] = bateryaKorumaAktif;
  doc["battery"]["kritik"] = bateryaKritik;
  doc["battery"]["kesme_volt"] = bateryaKesmeVolt;
  doc["battery"]["geri_volt"] = bateryaGeriYuklemeVolt;
  doc["battery"]["esp8266_ack"] = esp8266BatteryLowAck;

  doc["yedek_aku"]["volt"] = yedekAkuData.voltaj;
  doc["yedek_aku"]["durum"] = yedekAkuDurumMetni();
  doc["yedek_aku"]["guncel"] = ((millis() - yedekAkuData.last_update_ms) < (YEDEK_AKU_POLL_INTERVAL_MS * 3)) && yedekAkuData.read_ok;

  doc["ana_guc"]["volt"] = anaGucData.voltaj;
  doc["ana_guc"]["kademe"] = anaGucData.kademe;
  doc["ana_guc"]["ok"] = anaGucData.read_ok;
  doc["ana_guc"]["esik1"] = anaGucEsik1Volt;
  doc["ana_guc"]["esik2"] = anaGucEsik2Volt;
  doc["ana_guc"]["esik3"] = anaGucEsik3Volt;
  doc["ana_guc"]["acil_lamba"] = acilLambaAktif;
  doc["ana_guc"]["acil_lamba_manuel"] = acilLambaManuel;

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
    guvenliRestart();
  }
}

// ============ DOSYADAN OTA (bin dosyasi web'den yuklenir) ============
void handleFileUploadUpdate() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  delay(100);
  guvenliRestart();
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
  bool ok = esp8266KayitYedekle();
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" +
              String(ok ? "Yedeklendi" : "Yedekleme basarisiz") + "\"}");
}

void handleAPI_KayitGeriYukle() {
  bool ok = esp8266KayitGeriYukle();
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

void handleAPI_KonteynerSwanAyar() {
  // PIR2'den FARKLI: 0 gecerli (varsayilan) - "aninda eskale" eski davranisi
  // korur, bkz konteynerSwanTutmaSaniye yorumu.
  uint16_t tutma = konteynerSwanTutmaSaniye;
  uint16_t onay = konteynerSwanOnaySaniye;
  if (server.hasArg("tutma")) tutma = (uint16_t)server.arg("tutma").toInt();
  if (server.hasArg("onay")) onay = (uint16_t)server.arg("onay").toInt();
  konteynerSwanAyarKaydet(tutma, onay);
  server.send(200, "application/json", "{\"basarili\":true,\"tutma\":" + String(konteynerSwanTutmaSaniye) + ",\"onay\":" + String(konteynerSwanOnaySaniye) + "}");
}

String jsonKacir(const String& s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  return out;
}

void handleAPI_AlarmLog() {
  String j = "[";
  for (uint8_t i = 0; i < alarmLogRAMDolu; i++) {
    if (i) j += ",";
    j += "{\"zaman\":\"" + jsonKacir(alarmLogRAM[i].zaman) + "\",\"baslik\":\"" + jsonKacir(alarmLogRAM[i].baslik) + "\",\"tetikleyen\":\"" + jsonKacir(alarmLogRAM[i].tetikleyen) + "\"}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

// Her Konteyner sensorunun kendi aktif/pasif anahtari - verilmeyen parametre
// mevcut degerini korur (tek tek de degistirilebilsin diye).
void handleAPI_KonteynerSensorAktif() {
  bool pir = konteynerPirEtkin, kapi = konteynerKapiEtkin, swan = konteynerSwanEtkin, duman = konteynerDumanEtkin, gaz = konteynerGazEtkin;
  if (server.hasArg("pir")) pir = server.arg("pir").toInt() != 0;
  if (server.hasArg("kapi")) kapi = server.arg("kapi").toInt() != 0;
  if (server.hasArg("swan")) swan = server.arg("swan").toInt() != 0;
  if (server.hasArg("duman")) duman = server.arg("duman").toInt() != 0;
  if (server.hasArg("gaz")) gaz = server.arg("gaz").toInt() != 0;
  konteynerSensorAktifKaydet(pir, kapi, swan, duman, gaz);
  server.send(200, "application/json", "{\"basarili\":true,\"pir\":" + String(pir ? "true" : "false") +
              ",\"kapi\":" + String(kapi ? "true" : "false") + ",\"swan\":" + String(swan ? "true" : "false") +
              ",\"duman\":" + String(duman ? "true" : "false") + ",\"gaz\":" + String(gaz ? "true" : "false") + "}");
}

void handleAPI_KonteynerMq6Test() {
  if (server.hasArg("durum")) konteynerMq6TestModu = server.arg("durum").toInt() != 0;
  server.send(200, "application/json", "{\"basarili\":true,\"test_modu\":" + String(konteynerMq6TestModu ? "true" : "false") + "}");
}

void handleAPI_KonteynerGazAyar() {
  float esik = konteynerGazEsikVolt;
  if (server.hasArg("esik")) {
    esik = server.arg("esik").toFloat();
    if (esik < 0.1f) esik = 0.1f;
    if (esik > 3.3f) esik = 3.3f;
  }
  konteynerGazAyarKaydet(esik);
  server.send(200, "application/json", "{\"basarili\":true,\"esik\":" + String(konteynerGazEsikVolt, 2) + "}");
}

void handleAPI_KonteynerDumanAyar() {
  float esik = konteynerDumanEsikVolt;
  if (server.hasArg("esik")) {
    esik = server.arg("esik").toFloat();
    if (esik < 0.1f) esik = 0.1f;
    if (esik > 3.3f) esik = 3.3f;
  }
  konteynerDumanAyarKaydet(esik);
  server.send(200, "application/json", "{\"basarili\":true,\"esik\":" + String(konteynerDumanEsikVolt, 2) + "}");
}

// Batarya (MPPT) koruma ayarlari - "aktif" parametresi verilmezse mevcut
// deger korunur (sadece esikleri degistirmek icin ayri cagri yapilabilsin).
void handleAPI_BateryaAyar() {
  bool aktif = bateryaKorumaAktif;
  float kesme = bateryaKesmeVolt;
  float geri = bateryaGeriYuklemeVolt;
  if (server.hasArg("aktif")) aktif = server.arg("aktif").toInt() != 0;
  if (server.hasArg("kesme")) kesme = server.arg("kesme").toFloat();
  if (server.hasArg("geri")) geri = server.arg("geri").toFloat();
  if (geri <= kesme) geri = kesme + 1.0; // histerezis icin gecerli sira garantisi
  bateryaAyarlariKaydet(aktif, kesme, geri);
  server.send(200, "application/json", "{\"basarili\":true,\"aktif\":" + String(aktif ? "true" : "false") +
              ",\"kesme\":" + String(kesme, 1) + ",\"geri\":" + String(geri, 1) + "}");
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
  bool ok = rs485_send_wait_ack(acik ? "MASTER:SET_LAMBA=1\n" : "MASTER:SET_LAMBA=0\n", reply, 1000, 3);
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

// Konteyner'in KENDI elle ac/kapa lamba anahtari - Sudepo'nun /api/lamba'sinin
// aksine RS485 YOK, tamamen yerel/anlik (bkz konteynerLambaManuel,
// alarmLedGuncelle icinde otomatik alarm davranisiyla OR'lanir).
void handleAPI_KonteynerLamba() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  konteynerLambaManuel = (server.arg("durum").toInt() == 1);
  // FIX: "Kapat" sadece konteynerLambaManuel'i false yapiyordu - lamba
  // konteynerLambaAktif = konteynerBuzzerVar || konteynerLambaOnayVerildi ||
  // konteynerLambaManuel OR'una bagli oldugundan, "Sessiz (Lamba)" onayi
  // (konteynerLambaOnayVerildi) eskiden verilip PIR hala aktif oldugu icin
  // henuz sifirlanmamissa (bkz konteynerSensorleriOku - sadece PIR/kapi
  // tamamen bittiginde temizlenir) kullanicinin "Kapat" tiklamasi hicbir
  // sey yapmiyordu, lamba yanik kaliyordu. "Kapat" artik bu otomatik
  // onay bayragini da temizleyip gercek bir zorla-kapatma davranisi.
  if (!konteynerLambaManuel) konteynerLambaOnayVerildi = false;
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(konteynerLambaManuel ? "Lamba Acik" : "Lamba Kapali") + "\"}");
}

// Acil Durum Lambasi - web/fiziksel buton manuel ac/kapa. Panik/Konteyner
// alarminda zaten otomatik yanar (bkz acilLambaGuncelle), bu sadece manuel
// bayragi degistirir - gercek pin durumu her zaman acilLambaGuncelle()'da hesaplanir.
void handleAPI_AcilLamba() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  acilLambaManuel = (server.arg("durum").toInt() == 1);
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(acilLambaManuel ? "Acil Lamba Acik" : "Acil Lamba Kapali") + "\"}");
}

void handleAPI_AnaGucEsik() {
  float e1 = anaGucEsik1Volt, e2 = anaGucEsik2Volt, e3 = anaGucEsik3Volt;
  if (server.hasArg("esik1")) e1 = server.arg("esik1").toFloat();
  if (server.hasArg("esik2")) e2 = server.arg("esik2").toFloat();
  if (server.hasArg("esik3")) e3 = server.arg("esik3").toFloat();
  // Kademeler mantikli sirada kalsin: esik1 > esik2 > esik3
  if (e2 >= e1) e2 = e1 - 0.1f;
  if (e3 >= e2) e3 = e2 - 0.1f;
  anaGucEsikKaydet(e1, e2, e3);
  server.send(200, "application/json", "{\"basarili\":true,\"esik1\":" + String(e1, 1) + ",\"esik2\":" + String(e2, 1) + ",\"esik3\":" + String(e3, 1) + "}");
}

bool alarmAyarla(bool aktif, String& reply) {
  // NOT: timeout/deneme sayisi bir ara 400ms/2'ye dusurulmustu (banner
  // butonlarini hizlandirmak icin) ama bu projede RS485 hat cakismasi daha
  // once gercek bir sorun oldugundan (bkz proje notlari) komutlarin
  // ulasmasini guvenilmez hale getirdi - 1000ms/3'e geri alindi.
  bool ok = rs485_send_wait_ack(aktif ? "MASTER:SET_ALARM=1\n" : "MASTER:SET_ALARM=0\n", reply, 1000, 3);
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
  bool ok = rs485_send_wait_ack((String("MASTER:SET_ALARM_MOD=") + mod + "\n").c_str(), reply, 1000, 3);
  if (ok) {
    alarmStatus.mode = mod; alarmStatus.muted = false; alarmStatus.pending = false; last_rs485_update_ms = millis();
    // Konteyner'in yerel onay bayraklarini da mod degisince temizle - ONCEDEN
    // BUG: mod=3'te "Sessiz (Lamba)" onaylanmisken mod Sessiz'e (2) gecilince
    // konteynerLambaOnayVerildi eski onaydan true kaldigi icin lamba yanik
    // kaliyordu ("Sessiz modda hicbir fiziksel cikis olmaz" kuralini bozuyordu).
    konteynerOnayBekleniyor = false;
    konteynerOnayVerildi = false;
    konteynerLambaOnayVerildi = false;
  }
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
  // ONCEDEN BUG: "MASTER:ALARM_MUTE" KOSULSUZ TOGGLE komutuydu -
  // rs485_send_wait_ack ACK gelmezse AYNI komutu 3 kere tekrar gonderir; ACK
  // kaybolup komut ESP8266'ya yine de ulasmis olursa (RS485 hat cakismasi bu
  // projede daha once gercek bir sorun olmustu) susturma yanlislikla iki kez
  // tetiklenip ESKI haline donebiliyordu. Artik ESP32 istenen HEDEF degeri
  // hesaplayip acikca gonderiyor - kac kere tekrar gonderilirse gonderilsin
  // sonuc ayni (idempotent).
  // Tek buton iki bagimsiz bayragi kontrol eder, toggle hedefi ikisinin
  // OR'una gore (yoksa sadece biri susturulmusken buton kilitli kalirdi).
  bool hedef = !(alarmStatus.muted || konteynerSusturuldu);
  konteynerSusturuldu = hedef; // RS485/ESP8266 sonucundan bagimsiz hemen uygulanir
  bool ok = rs485_send_wait_ack((String("MASTER:ALARM_MUTE=") + (hedef ? "1" : "0") + "\n").c_str(), reply, 1000, 3);
  if (ok) { alarmStatus.muted = hedef; last_rs485_update_ms = millis(); }
  return ok;
}

void handleAPI_AlarmMute() {
  String reply;
  bool ok = alarmSustur(reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"muted\":" + String(alarmStatus.muted ? "true" : "false") + ",\"reply\":\"" + reply + "\"}");
}

bool alarmOnayla(String& reply) {
  bool ok = rs485_send_wait_ack("MASTER:ALARM_ONAYLA\n", reply, 1000, 3);
  if (ok) { alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  // Konteyner'in kendi onayi ESP8266/RS485'ten BAGIMSIZ (yerel bayrak) -
  // ESP8266 cevrimdisi olsa bile Kalburum'un onayi calissin. ONCEDEN BUG:
  // burasi Konteyner'de HICBIR SEY beklemiyor olsa bile (orn. sadece
  // Sudepo'nun kacak alarmi onaylaniyordu) konteynerOnayVerildi'yi
  // KOSULSUZ true yapiyordu - sonraki, tamamen ILGISIZ bir Konteyner
  // tetiklemesinde kullanici hic onaylamadan siren/lamba dogrudan
  // calisiyordu. Artik sadece GERCEKTEN bekleyen bir Konteyner onayi varsa
  // isaretleniyor.
  if (konteynerOnayBekleniyor) {
    konteynerOnayVerildi = true; // "Sesli" onay - buzzer devreye girer
  }
  konteynerOnayBekleniyor = false;
  return ok;
}

void handleAPI_AlarmOnayla() {
  String reply;
  bool ok = alarmOnayla(reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Onaylandi" : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_AlarmOnaylaLamba() {
  String reply;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_ONAYLA_LAMBA\n", reply, 1000, 3);
  if (ok) { alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  // Konteyner'in KONTEYNER_LAMBA_PIN uzerinden gercek bir lamba ciktisi var -
  // "Sessiz" secildigi icin buzzer/siren ATILMAZ (konteynerOnayVerildi false
  // kalir), ama lamba tek basina aktif olur. Sadece GERCEKTEN bekleyen bir
  // Konteyner onayi varsa isaretlenir (bkz alarmOnayla() ayni duzeltme).
  if (konteynerOnayBekleniyor) {
    konteynerLambaOnayVerildi = true;
  }
  konteynerOnayBekleniyor = false;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Sadece lamba flasoru aktif" : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_MoistureToggle() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_MOISTURE=1\n" : "MASTER:SET_MOISTURE=0\n", reply, 1000, 3);
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
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_MOISTURE_AUTO=1\n" : "MASTER:SET_MOISTURE_AUTO=0\n", reply, 1000, 3);
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
  bool okLow = rs485_send_wait_ack(cmdLow.c_str(), replyLow, 1000, 3);
  bool okHigh = rs485_send_wait_ack(cmdHigh.c_str(), replyHigh, 1000, 3);
  if (okLow && okHigh) {
    sensorData.moisture_low = low;
    sensorData.moisture_high = high;
    last_rs485_update_ms = millis();
  }
  bool ok = okLow && okHigh;
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Esikler ayarlandi" : "Komut hatasi") + "\",\"replyLow\":\"" + replyLow + "\",\"replyHigh\":\"" + replyHigh + "\"}");
}

bool kapiAyarla(bool acik, String& reply) {
  bool ok = rs485_send_wait_ack(acik ? "MASTER:SET_KAPI=1\n" : "MASTER:SET_KAPI=0\n", reply, 1000, 3);
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
  String reply;
  bool ok = rs485_send_wait_ack("MASTER:GET_AYARLAR\n", reply, 1000, 3);
  if (ok) {
    int idx = reply.indexOf("AYARLAR=");
    veri = (idx >= 0) ? reply.substring(idx + 8) : "";
  }
  return ok;
}

bool sudepoAyarlarKaydet(const String& veri, String& reply) {
  bool ok = rs485_send_wait_ack(("MASTER:SET_AYARLAR=" + veri + "\n").c_str(), reply, 1000, 3);
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
    "alarmMod", "alarmSensorEtkin", "alarmMaskSesli", "alarmMaskSessiz", "alarmMaskOnayli",
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
// ESP8266'daki /role/panic ile aynı davranış.
// RS485 üzerinden MASTER:PANIC=1/0 gönderir (HEDEF değer acikca belirtilir -
// ONCEDEN kosulsuz toggle komutuydu: ACK kaybolup rs485_send_wait_ack ayni
// komutu tekrar gonderirse panik yanlislikla iki kez tetiklenip eski haline
// donebiliyordu, guvenlik-kritik bir ozellik icin ciddi bir riskti. Artik
// idempotent - kac kere tekrar gonderilirse gonderilsin sonuc ayni),
// ESP8266 ACK:PANIC=1 veya ACK:PANIC=0 ile yeni durumu döndürür.
//
// panikTetikleHedef(hedef,...): hedefi ACIKCA alir. alarmStatus.panic_mode'u
// ONCE LOKAL olarak yazar (Konteyner'in kendi sireni/lambasi/acil lambasi
// - bkz alarmLedGuncelle, acilLambaGuncelle - RS485 sonucundan TAMAMEN
// BAGIMSIZ aninda tetiklenir), SONRA Sudepo/ESP8266'yi RS485 ile best-effort
// senkron eder. Eskiden panikTetikle() sadece RS485 ACK basarili olursa
// panic_mode'u guncelliyordu - bu, Sudepo tarafi kapali/baglantisizken
// fiziksel Acil Buton'un Konteyner'in KENDI alarmini bile tetiklememesine
// sebep oluyordu (SAHADA dogrulanan bug, guvenlik-kritik bir buton icin
// kabul edilemezdi, bkz memory project_acil_buton_rs485_blok_riski).
bool panikTetikleHedef(bool hedef, bool& panicActive, String& reply) {
  alarmStatus.panic_mode = hedef; // lokal etki once, RS485'ten bagimsiz
  panicActive = hedef;
  bool ok = rs485_send_wait_ack((String("MASTER:PANIC=") + (hedef ? "1" : "0") + "\n").c_str(), reply, 1000, 3);
  if (ok) {
    // ACK yanıtından Sudepo'nun gercek durumunu cöz: "ACK:PANIC=1/0"
    int eqIdx = reply.indexOf("PANIC=");
    if (eqIdx >= 0) {
      panicActive = (reply.substring(eqIdx + 6).startsWith("1"));
      alarmStatus.panic_mode = panicActive;
    }
    last_rs485_update_ms = millis();
  }
  return ok;
}

bool panikTetikle(bool& panicActive, String& reply) {
  return panikTetikleHedef(!alarmStatus.panic_mode, panicActive, reply);
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
    ",\"zamanAsimi\":" + String(zamanAsimi ? "true" : "false") +
    ",\"sonProtokol\":\"" + irSonDenemeProtokol + "\"" +
    ",\"denemeSayisi\":" + String(irDenemeSayaci) +
    ",\"sonRawlen\":" + String(irSonRawlen) + "}";
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

// Duz ESP.restart() BLE (NimBLE) baglantisi acikken veya WiFi radyosu tam
// kapatilmadan cagrilirsa bazen RF/PHY kalibrasyonunu "kirli" birakiyor -
// belirti: yazilim resetinden sonra WiFi/web sayfasi hic gelmiyor, ancak
// komple guc kesilip verilince (gercek power-on, PHY sifirdan kalibre
// olur) sorunsuz acilir. Duzeltme: restart'tan ONCE BLE denetleyiciyi
// duzgunce serbest birak (NimBLEDevice::deinit) ve WiFi radyosunu tamamen
// kapat (disconnect+mode(WIFI_OFF)) - boylece ESP.restart() radyo hala
// calisirken degil, temiz bir durumdan cagrilir.
void guvenliRestart() {
#if ENABLE_BLE
  NimBLEDevice::deinit(true);
#endif
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(150);
  ESP.restart();
}

void handleAPI_Restart() {
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Yeniden baslatiliyor\"}");
  delay(500);
  guvenliRestart();
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
  server.on("/api/konteyner/swan_ayar", handleAPI_KonteynerSwanAyar);
  server.on("/api/alarm/log", handleAPI_AlarmLog);
  server.on("/api/konteyner/sensor_aktif", handleAPI_KonteynerSensorAktif);
  server.on("/api/konteyner/mq6_test", handleAPI_KonteynerMq6Test);
  server.on("/api/konteyner/gaz_ayar", handleAPI_KonteynerGazAyar);
  server.on("/api/konteyner/duman_ayar", handleAPI_KonteynerDumanAyar);
  server.on("/api/batarya/ayar", handleAPI_BateryaAyar);
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUpload, handleFirmwareUploadProgress);
  server.on("/firmware/esp8266.bin", HTTP_GET, handleFirmwareServe);
  server.on("/api/firmware/durum", handleFirmwareDurum);
  server.on("/api/lamba", handleAPI_Lamba);
  server.on("/api/konteyner/lamba", handleAPI_KonteynerLamba);
  server.on("/api/acil-lamba", handleAPI_AcilLamba);
  server.on("/api/ana-guc-esik", handleAPI_AnaGucEsik);
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

WiFiMulti wifiMulti;

void wifi_connect() {
  // ESP8266'daki gibi: AP her zaman acik (STA basarisiz olsa da paneline
  // erisim kaybolmasin), STA kayitli ag varsa ona baglanir.
  wifiCredYukle();
  bool ozelAg = savedSSID.length() > 0;

  DEBUG_PRINTLN("[WiFi] Connecting...");

  WiFi.mode(WIFI_AP_STA);
  // WiFi.setSleep(false) BILEREK KULLANILMIYOR: modem-sleep'i kapatmak WiFi
  // radyosunu SUREKLI tam guc/aktif modda tutar (normalde sinyal araliklarinda
  // kisilip ortalama akimi dusurur) - bu kartin besleme kaynagi zaten marjinal
  // oldugu sahada dogrulandi (bkz proje hafizasi: USB'de acilis brownout'u,
  // orijinal DC beslemede "anlik dusus sonra duzeldi"). Sureki yuksek WiFi
  // akimi bu marjinal kaynakta kalici/tekrar eden brownout'u tetikleyebilir -
  // bir kez denenip bu riskten dolayi geri alindi. Besleme guclendirilirse
  // (kondansator/daha iyi adaptor) IR zamanlama iyilestirmesi icin tekrar
  // denenebilir, ama o zamana kadar KAPALI kalmali.
  // WiFi.softAP sessizce basarisiz olabilir (ornegin AP_PASSWORD 8 karakterden
  // kisaysa WPA2 gereksinimini karsilamaz) - donus degeri kontrol edilmezse
  // cihaz farkedilmeden sifresiz/varsayilan (ESP_xxxxxx) AP'ye duser. Bir kez
  // basimiza geldi (bkz proje hafizasi), o yuzden artik loglaniyor.
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4)) {
    DEBUG_PRINTLN("[WiFi] UYARI: softAP baslatilamadi! (sifre >=8 karakter mi?)");
  }
  WiFi.softAPConfig(
    IPAddress(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4),
    IPAddress(AP_IP_OCTET_1, AP_IP_OCTET_2, AP_IP_OCTET_3, AP_IP_OCTET_4),
    IPAddress(255, 255, 255, 0)
  );

  // Ozel ag kaydedilmisse (web arayuzunden) sadece o denenir. Yoksa iki
  // varsayilan ag da eklenir (WIFI_SSID/WIFI_SSID2) - WiFiMulti taranan
  // aglar arasindan menzilde/bilinen olana (en guclu sinyalliye) baglanir.
  if (ozelAg) {
    wifiMulti.addAP(savedSSID.c_str(), savedPass.c_str());
  } else {
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
    wifiMulti.addAP(WIFI_SSID2, WIFI_PASSWORD2);
  }

  int attempts = 0;
  unsigned long start_ms = millis();
  while (wifiMulti.run() != WL_CONNECTED && attempts < 20) {
    if (millis() - start_ms > 100) {  // 100ms per attempt (no blocking)
      DEBUG_PRINT(".");
      attempts++;
      start_ms = millis();
      yield();  // ESP32 diğer görevleri yapsın
    }
  }

  // Statik IP sadece varsayilan/birincil ag (WIFI_SSID) icin gecerli - hem
  // ozel kaydedilmis ag hem de ikincil varsayilan ag (WIFI_SSID2, farkli bir
  // fiziksel ag/router - orn. telefon hotspot'u) muhtemelen tamamen farkli
  // bir subnet/gateway kullanir, config.h'daki sabit IP orada gecersiz olur
  // ve baglantiyi bozar - o durumlarda DHCP'ye birakilir.
  if (WiFi.status() == WL_CONNECTED && !ozelAg && WiFi.SSID() == String(WIFI_SSID)) {
    WiFi.config(
      IPAddress(WIFI_STATIC_IP_OCTET_1, WIFI_STATIC_IP_OCTET_2, WIFI_STATIC_IP_OCTET_3, WIFI_STATIC_IP_OCTET_4),
      IPAddress(WIFI_GATEWAY_OCTET_1, WIFI_GATEWAY_OCTET_2, WIFI_GATEWAY_OCTET_3, WIFI_GATEWAY_OCTET_4),
      IPAddress(WIFI_SUBNET_OCTET_1, WIFI_SUBNET_OCTET_2, WIFI_SUBNET_OCTET_3, WIFI_SUBNET_OCTET_4),
      IPAddress(WIFI_DNS1_OCTET_1, WIFI_DNS1_OCTET_2, WIFI_DNS1_OCTET_3, WIFI_DNS1_OCTET_4),
      IPAddress(WIFI_DNS2_OCTET_1, WIFI_DNS2_OCTET_2, WIFI_DNS2_OCTET_3, WIFI_DNS2_OCTET_4)
    );
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

// wifi_connect() SADECE setup()'ta bir kez calisiyordu - STA baglantisi
// (router kesintisi, sinyal dususu, DHCP lease sorunu vb.) koptuktan sonra
// loop()'ta HICBIR yeniden baglanma denemesi yoktu, cihaz elle resetlenene
// kadar web arayuzune (STA IP uzerinden) bir daha asla erisilemiyordu -
// AP/RS485/lokal islevler etkilenmedigi icin "sistem calisiyor ama sayfa
// acilmiyor" seklinde kafa karistirici bir belirti veriyordu (2026-08-26,
// sahada `ping`in "Destination host unreachable" dondurmesiyle dogrulandi).
// WiFi.reconnect() asenkron/bloke etmeyen bir cagri - periyodik (15sn'de
// bir, sadece kopukken) tekrar tetiklenir.
void wifiReconnectPoll() {
  static unsigned long sonDenemeMs = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - sonDenemeMs < 15000) return;
  sonDenemeMs = millis();
  DEBUG_PRINTLN("[WiFi] STA bagli degil, yeniden baglanma deneniyor...");
  WiFi.reconnect();
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
  Serial.begin(115200);
  delay(1000);
  resetSebebiYazdir();

  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("🟢 SuDepoSis v2 - ESP32-S3 Master");
  DEBUG_PRINTLN("========================================");
  
  // RS485 Initialize
  rs485_init();
  mppt_init();
  pinMode(ACIL_LAMBA_PIN, OUTPUT);
  digitalWrite(ACIL_LAMBA_PIN, LOW); // guvenli varsayilan: acil lamba kapali
  pinMode(ACIL_BUTON_PIN, INPUT_PULLUP);
  anaGucEsikYukle();
  Wire.begin(AHT10_SDA_PIN, AHT10_SCL_PIN); // AHT10 I2C - ADS1115 de ayni hatta (farkli adres)
  Wire.setClock(50000); // AHT10 7m CAT5 uzerinden - kapasitans/yukselme suresi icin 100kHz yerine 50kHz

  // SPIFFS - kayit yedekleme icin (bkz esp8266KayitYedekle/GeriYukle)
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("[SPIFFS] Baslatilamadi");
  }
  weatherYukle();
  irEslesmeYukle();
  telegramAyarYukle();
  konteynerPirAyarYukle();
  konteynerSwanAyarYukle();
  konteynerAlarmAyarYukle();
  konteynerSensorAktifYukle();
  konteynerGazAyarYukle();
  konteynerDumanAyarYukle();
  bateryaAyarlariYukle();

  // WiFi Connect
  wifi_connect();
  setup_ota();

  // mDNS - MDNS_NAME zaten debug loglarinda/JSON'da "kalburum.local" olarak
  // gosteriliyordu ama MDNS.begin() hic cagrilmiyordu, yani .local adresi
  // gercekte hicbir zaman calismiyordu. AP modunda da (WiFi.softAP zaten
  // basladi) STA baglantisi olmasa bile MDNS calisir.
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    DEBUG_PRINT("[mDNS] Basladi: "); DEBUG_PRINT(MDNS_NAME); DEBUG_PRINTLN(".local");
  } else {
    DEBUG_PRINTLN("[mDNS] Baslatilamadi");
  }

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

  acilisSesiCal(); // "hazir" onay sesi - bkz fonksiyon yorumu (konteynerDonanimiInit ustunde)
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

  wifiReconnectPoll(); // STA koparsa periyodik (15sn'de bir) yeniden baglanmayi dener

  // Web Server handle
  server.handleClient();
  ArduinoOTA.handle();

  // RS485 Polling
  rs485_poll();

  // MPPT (ayri bus/UART2) - aku voltaji okuma ayri bir FreeRTOS task'ta yapiliyor
  // (bkz mpptTask/mppt_init) ki donanim yanit vermedigi surece loop()'u bloklamasin;
  // burada sadece son okunan degerle kritik-durum histerezisi hesaplanir.
  bateryaDurumHesapla();
  bateryaRS485Bildir();

  // Yedek Aku (GPIO2 ADC) - duz analogRead, bloke olmaz, dogrudan loop()'ta
  yedekAkuPoll();
  anaGucPoll(); // Ana guc (ADS1115/I2C, 3 kademeli bildirim)
  acilButonPoll(); // Fiziksel Acil Durum butonu (GPIO15)
  ahtPoll();  // AHT10 sicaklik/nem (I2C, kisa surer, bloke olmaz)
  mq6Poll();  // MQ6 (analog, alarma bagli - bkz konteynerGazVar)
  gp2y10Poll();  // GP2Y10 duman/toz sensoru (analog, alarma bagli - bkz konteynerDumanVar)

  // Konteyner donanimi - sadece okuma/yerel LED, alarm mantigina yazmiyor
  konteynerSensorleriOku();
  alarmLedGuncelle();
  irKumandaIsle();
  irKomutIsleVeCalistir();

  // Hava durumu / yagmur tahmini - WiFi baglandiginda veya periyodik
  weatherKontrolEt();

  // Alarm baslarsa Telegram'a bildirim gonder
  telegramAlarmKontrolEt();
  alarmLoguKontrolEt();
  zamanCacheGuncelle();
  telegramBateryaKontrolEt();
  telegramKonteynerOtoSusturKontrolEt();
  telegramAnaGucKontrolEt();

  // BLE - bagli telefona periyodik anlik veri
#if ENABLE_BLE
  bleDurumBildir();
#endif

  // MQTT
  mqtt_connect();
  mqtt_publish();
  
  // Alarm Kontrol - ONCEDEN BUG: ESP8266 baglantisi hic gelmemisken/koptugunda
  // sensorData.level_percent varsayilan 0.0'da kalip ALARM_LEVEL_PERCENT'in
  // altinda oldugundan yanlislikla "Dusuk seviye!" alarmi gosteriyordu. Artik
  // sadece veri TAZEYSE (esp8266_online ile ayni 10sn esigi) degerlendiriliyor,
  // bayat veride mevcut/varsayilan durum korunuyor.
  if ((millis() - sensorData.last_update_ms) < 10000) {
    alarmStatus.low_level_alarm = (sensorData.level_percent < ALARM_LEVEL_PERCENT);
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