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
  if (WiFi.status() != WL_CONNECTED) { weatherDurum = "WiFi bagli degil"; return false; }

  String url = String(WEATHER_FORECAST_API) + "?latitude=" + String(GARDEN_LATITUDE, 6) +
               "&longitude=" + String(GARDEN_LONGITUDE, 6) +
               "&daily=precipitation_sum&forecast_days=" + String(WEATHER_FORECAST_DAYS) + "&timezone=auto";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    weatherDurum = "HTTP hata: " + String(code) + " (" + http.errorToString(code) + ")";
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    weatherDurum = "JSON parse hatasi";
    return false;
  }
  JsonArray dates = doc["daily"]["time"].as<JsonArray>();
  JsonArray precip = doc["daily"]["precipitation_sum"].as<JsonArray>();
  int n = min((int)precip.size(), WEATHER_FORECAST_DAYS);
  if (n < 2) { weatherDurum = "API yanitinda gun verisi eksik"; return false; }

  for (int i = 0; i < n; i++) {
    weatherForecastDates[i] = dates[i].as<String>();
    weatherForecastMm[i] = precip[i].as<float>();
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
      DEBUG_PRINTLN("[Weather] Heap dusuk, bu dongu atlaniyor");
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
  unsigned long leak_start_ms = 0;
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
// BILEREK sadece OKUMA/yerel LED kontrolu yapiyor - mevcut alarm/RS485/BLE
// mantigina henuz baglanmadi. Kullanici IR kumanda kodlarini bildirip PIR2/
// reed'in alarm sistemine tam olarak nasil entegre olmasini istedigini
// onaylayana kadar boyle kalacak - calisan mevcut sistemi bozma riskini
// sifira indirmek icin.
bool kapi2Acik = false;       // Konteyner reed switch - true = kapi acik
bool pir2HareketVar = false;  // Konteyner PIR - true = hareket var

void konteynerDonanimiInit() {
  pinMode(ALARM_LED_PIN, OUTPUT);
  digitalWrite(ALARM_LED_PIN, LOW);
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
// mevcut alarm durumunu SADECE okur (banner'in gorunurlugüyle ayni mantik),
// hicbir alarm degiskenine yazmaz. ESP8266 tarafindaki Sesli/Sessiz mod
// ayrimindan BAGIMSIZ - kullanicinin talebiyle ("gercek her alarmda calissin")
// konteynerdaki bu yerel uyari her zaman aktif, LED ile ayni ritimde (400ms) yanip soner/oter.
void alarmLedGuncelle() {
  static bool ledDurum = false;
  static unsigned long sonDegisimMs = 0;
  bool alarmVar = (alarmStatus.enabled && alarmStatus.trigger_mask != 0) || alarmStatus.panic_mode || alarmStatus.pending;
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

const char* alarmTetikleyiciAdlari[6] = {"Sol Kapi", "Sag Kapi", "PIR (Hareket)", "Su Seviyesi", "Kacak", "Sensor Hatasi"};

String alarmTetikleyenMetni(uint8_t mask, bool panik) {
  if (panik) return "Panik (elle acildi)";
  String s = "";
  for (int i = 0; i < 6; i++) {
    if (mask & (1 << i)) {
      if (s.length() > 0) s += ", ";
      s += alarmTetikleyiciAdlari[i];
    }
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
  bool anyAlarm = (alarmStatus.enabled && mask != 0) || alarmStatus.panic_mode;
  bool alarmVar = anyAlarm || alarmStatus.pending;

  if (alarmVar && !telegramOncekiAlarmVar) {
    // Yeni alarm basladi - mesaj hazirla, ilk denemeyi hemen yap.
    String baslik = alarmStatus.panic_mode ? "PANIK AKTIF" :
                     (alarmStatus.pending ? "ALARM - Onay Bekliyor" : "ALARM TETIKLENDI");
    telegramBekleyenMetin = "🌱 SuDepo: " + baslik + " | Tetikleyen: " + alarmTetikleyenMetni(mask, alarmStatus.panic_mode);
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
      // sensor hatasi - log
    } else if (key == "RTC") {
      // RTC durumu - log
    } else if (key == "LEAK") {
      alarmStatus.leak_alarm = (value == "1");
    } else if (key == "LEAK_DK") {
      alarmStatus.leak_start_ms = value.toInt() * 60000UL;
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
  String html = R"html(
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
.card h3{font-size:14px;color:var(--muted);margin-bottom:8px}
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
      <div class="row">
        <button class="btn btn-accent" id="alarm-btn" onclick="toggleAlarm()">Alarmı Kapat</button>
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
    <div class="card">
      <h3>OTA Güncelleme</h3>
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
    </div>

    <div class="card">
      <h3>ESP8266 Firmware Deposu</h3>
      <p style="font-size:12px;color:var(--muted)">Bahcede internet olmadigindan, ESP8266'nin "URL'den Guncelle" kutusuna GitHub yerine buradaki adresi yaz - ikisi ayni WiFi agindayken calisir. ESP8266'nin web arayuzu artik firmware'in icine gomulu oldugu icin tek dosya (esp8266.bin) yeterli.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="fw-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <input type="file" id="fwDosya" accept=".bin">
        <button class="btn btn-primary" onclick="firmwareYukle()">Yukle</button>
      </div>
      <div id="fw-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Kayit Yedekleme</h3>
      <p style="font-size:12px;color:var(--muted)">ESP8266'nin kayitlar.csv dosyasinin yedegi - donanim arizasi/factory reset gibi durumlarda buradan geri yukleyebilirsin.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="yedek-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <button class="btn btn-primary" onclick="kayitYedekle()">Simdi Yedekle</button>
        <button class="btn btn-warn" onclick="kayitGeriYukle()">ESP8266'ya Geri Yukle</button>
      </div>
      <div id="yedek-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Hava Durumu / Yağmur Tahmini</h3>
      <p style="font-size:12px;color:var(--muted)">Sabit konum (bahçe) - internet varken (örn. telefon hotspot'u) otomatik çekilir. 7 günden eski tahmin dikkate alınmaz, o durumda sulama normal devam eder.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="weather-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <button class="btn btn-warn" onclick="weatherKontrolEt()">Şimdi Kontrol Et</button>
      </div>
      <div id="weather-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="weather-haftalik" style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap"></div>
    </div>

    <div class="card">
      <h3>Telegram Bildirimleri</h3>
      <p style="font-size:12px;color:var(--muted)">Alarm YENİ başladığında (panik, kapı, PIR, kaçak vb.) Telegram'a bildirim gönderir - sadece bu cihazın o an interneti varsa (örn. hotspot bağlıyken) çalışır.</p>
      <div class="row">
        <button class="btn btn-primary" onclick="telegramTest()">Test Mesajı Gönder</button>
      </div>
      <div id="telegram-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>WiFi</h3>
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
    </div>

    <div class="card">
      <h3>Nem Ayarları</h3>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)">
        Nem: <b id="settings-moisture-val">-</b> | Çıkış: <b id="settings-moisture-out">-</b> | Mod: <b id="settings-moisture-mod">-</b>
      </div>
      <div class="row">
        <button class="btn btn-primary" id="moisture-settings-toggle-btn" onclick="toggleMoisture()">Aç/Kapat</button>
        <button class="btn btn-accent" id="moisture-settings-auto-btn" onclick="toggleMoistureAuto()">Otomatik / Manuel</button>
      </div>
      <div style="margin-top:10px;">
        <label style="display:block;font-size:12px;color:var(--muted);">Alt Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-low" min="0" max="100" style="width:120px;display:inline-block;" value="0">
        <label style="display:block;font-size:12px;color:var(--muted);margin-top:8px;">Üst Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-high" min="0" max="100" style="width:120px;display:inline-block;" value="0">
        <button class="btn btn-warn" style="margin-top:10px;" onclick="setMoistureThresholds()">Kaydet</button>
      </div>
      <div id="moisture-settings-msg" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Sistem</h3>
      <div class="row">
        <button class="btn btn-danger" onclick="restartSistem()">Yeniden Başlat</button>
      </div>
    </div>
  </div>

  <div id="bilgiler" class="section">
    <div class="card">
      <h3>ESP32 Master Pinout</h3>
      <table class="table">
        <tr><th>Pin</th><th>GPIO</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>RX (UART1)</td><td>16</td><td>MAX485 RS485</td><td>RO (Alıcı)</td></tr>
        <tr><td>TX (UART1)</td><td>17</td><td>MAX485 RS485</td><td>DI (Verici)</td></tr>
        <tr><td>D2</td><td>2</td><td>MAX485 RS485</td><td>DE/RE (Enable)</td></tr>
        <tr><td>UART0</td><td>1/3</td><td>Debug Serial</td><td>9600 baud</td></tr>
      </table>
    </div>

    <div class="card">
      <h3>ESP8266 Slave Pinout</h3>
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
    </div>

    <div class="card">
      <h3>Arduino Nano IO (v2 Pin Planı)</h3>
      <table class="table">
        <tr><th>Pin</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>D0 (RX)</td><td>ESP8266 TX</td><td>Seri haberleşme</td></tr>
        <tr><td>D1 (TX)</td><td>ESP8266 RX</td><td>Seri haberleşme</td></tr>
        <tr><td>D2</td><td>Kapı 1 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D3</td><td>Kapı 2 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D4</td><td>Alarm rölesi</td><td>OUTPUT (HIGH=aktif)</td></tr>
        <tr><td>D5</td><td>Nem rölesi</td><td>OUTPUT</td></tr>
        <tr><td>D6</td><td>PIR hareket sensörü</td><td>INPUT - ESP8266 PIN_READ:6 ile okur (Nano kodu değişmez)</td></tr>
        <tr><td>D7-D12</td><td>Yedek GPIO</td><td>ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik</td></tr>
        <tr><td>D13</td><td>Depo iç lamba rölesi</td><td>OUTPUT (sadece lamba)</td></tr>
        <tr><td>A0-A5</td><td>Yedek GPIO</td><td>Analog + digital I/O</td></tr>
        <tr><td>A6-A7</td><td>Yedek</td><td>Sadece analog input</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>GPIO Komut Protokolü (ESP→Nano):</b> PIN_MODE:<pin>,<mod> | PIN_WRITE:<pin>,<0/1> | PIN_READ:<pin> | PIN_READ_ALL</p>
    </div>

    <div class="card">
      <h3>WiFi & Sistem</h3>
      <div id="bilgi-sistem" style="font-size:13px">Yükleniyor...</div>
      <div id="build-info" style="font-size:12px;color:var(--muted);margin-top:8px">Yükleniyor...</div>
    </div>

    <div class="card">
      <h3>Güncelleme Aralıkları</h3>
      <div id="guncelleme-bilgi" style="font-size:13px">Yükleniyor...</div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">SSE (anlık push): ESP8266/Nano'dan yeni veri gelir gelmez, en geç 1sn'de bir yedek olarak. Tarayıcı 5sn'de bir de yedek polling yapar (SSE koparsa fark edilmesin diye).</p>
    </div>

    <div class="card">
      <h3>IR Kumanda Ayarları</h3>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Konteynerdaki IR alıcıya (herhangi bir kızılötesi kumanda) tuş tanımlayın - "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa basın, sonra hangi komutu çalıştıracağını seçin. Birden fazla kumanda eklenebilir.</p>
      <div id="ir-liste" style="font-size:13px">Yükleniyor...</div>
      <button class="btn btn-mavi" style="margin-top:10px" onclick="irOgrenBaslat()">➕ Yeni Tuş Öğren</button>
      <div id="ir-ogren-durum" style="margin-top:8px;font-size:13px"></div>
    </div>

    <div class="card">
      <h3>Kullanım Kılavuzu</h3>
      <div style="font-size:13px;line-height:1.6">
        <p><b>Alarm Modları</b> (Kontrol → Alarm): 1-Sesli (tetiklenince siren hemen çalışır), 2-Sessiz (siren çalışmaz, sadece bu sayfada/ESP8266'da bildirim), 3-Onaylı (tetiklenince onay bekler, "Tetiklenmeyi Onayla" ile sesli moda geçer).</p>
        <p><b>Sustur/Sireni Kapat:</b> Alarm koşulu sürse bile röleyi susturur; koşul temizlenince otomatik sıfırlanır.</p>
        <p><b>Panik:</b> Tetikleyicilerden bağımsız, elle aç/kapat anahtarı gibi çalışır - röleyi zorla açık tutar.</p>
        <p><b>Kapı/PIR/Kaçak/Düşük seviye</b> tetikleyicileri ve gündüz/gece + mod bazlı senaryolar ESP8266 panelinin "Alarm" sekmesinden ayarlanır (bu panel sadece görüntüler ve mod/susturma/onay/panik komutlarını iletir).</p>
        <p><b>WiFi:</b> Ayarlar sekmesinden ev ağını tarayıp kaydedebilirsiniz; AP (<code id="ag-ap-bilgi">-</code>) STA bağlantısından bağımsız her zaman açıktır, ağ ayarları NVS'de kalıcıdır.</p>
        <p><b>Hava Durumu / Yağmur Tahmini:</b> Bahçenin sabit konumu için haftalık tahmin, bu cihazın interneti olduğu anda (örn. telefon hotspotu bağlıyken) otomatik çekilip hafızada saklanır. Bahçede kalıcı internet olmadığı için tahmin bayatlayabilir - 7 günden eski ise dikkate alınmaz ve sulama normal devam eder. Yarın yağmur bekleniyorsa bugünkü sulama otomatik atlanır (su israfını önlemek için).</p>
        <p><b>Telegram Bildirimleri:</b> Bir alarm YENİ tetiklendiğinde (panik, kapı, PIR, kaçak, sensör hatası) sayfa açık olmasa bile telefona Telegram mesajı gider - yalnızca cihazın o an interneti varsa (hotspot bağlıyken) çalışır, aksi halde birkaç dakika tekrar denenir. Bot token/chat ID <code>secrets.h</code> içinde saklanır.</p>
        <p><b>Mimari:</b> Nano (kapı/röle/lamba/PIR/nem çıkışı) ⇄ ESP8266 (sensörler + web) ⇄ RS485 ⇄ ESP32 (bu panel, MQTT yayını). Nano firmware'i genel amaçlı GPIO komutlarıyla (PIN_MODE/WRITE/READ) çalıştığı için donanım eklemelerinde çoğunlukla yeniden flaşlanması gerekmez.</p>
      </div>
    </div>
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
function tetikleyenMetni(mask,panicAktif){
  if(panicAktif) return 'Panik (elle acildi)';
  const l=[];
  for(let i=0;i<6;i++) if(mask&(1<<i)) l.push(tetikleyiciAdlari[i]);
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
  // Su seviyesi
  $('#kpi-pct').textContent=(d.level_percent||0).toFixed(1)+'%';
  $('#kpi-cm').textContent=(d.level_cm||0).toFixed(1)+' cm';
  $('#kpi-litre').textContent=(d.level_liters||0).toFixed(0)+' L';
  $('#kpi-temp').textContent=(d.temperature||0).toFixed(1)+' °C';
  $('#bar-pct').style.width=Math.max(0,Math.min(100,d.level_percent||0))+'%';
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
  // Panik, alarm sistemi kapali (enabled===false) olsa bile ESP8266 tarafinda
  // her seyin onunde calisir (bkz esp8266_slave main.cpp panicRoleAktif) - bu
  // yuzden panic iken enabled kontrolunu atlar, aksi halde alarm sistemi
  // kapatilmisken panik basilinca banner hic gorunmuyordu.
  const anyAlarm = !!(d.alarm && ((d.alarm.enabled !== false && alarmMask !== 0) || d.alarm.panic));
  ad.className = anyAlarm ? 'dot alarm' : 'dot active';
  if(d.alarm){
    if(d.alarm.panic) at='PANİK AKTİF';
    else if(d.alarm.leak) at='ALARM: Kaçak!';
    else if(d.alarm.low_level) at='ALARM: Düşük seviye!';
    else if(d.alarm.door) at='ALARM: Kapı açık!';
    else if(alarmMask & 4) at='ALARM: Hareket algılandı!';
    else if(alarmMask & 32) at='ALARM: Sensör hatası!';
  }
  $('#alarm-text').textContent=at;
  // Buyuk uyari banner'i - ESP8266'daki gibi, tetiklendiginde sayfanin
  // her sekmesinde gorunur olsun diye header'in hemen altina konuldu.
  const ban=$('#alarm-banner');
  if(ban){
    const bekliyor = d.alarm && d.alarm.pending;
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
        const tk = tetikleyenMetni((d.alarm&&d.alarm.trigger_mask)||0, false);
        msg += ' | Tetikleyen: '+tk;
      }
      let html = '⚠ '+msg;
      if(panikAktif){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="togglePanic()">Panik Kapat</button></div>';
      } else if(bekliyor){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="alarmOnayla()">Sesli</button><button class="btn btn-warn" onclick="alarmOnaylaLamba()">Sessiz (Lamba)</button></div>';
      } else if(anyAlarm){
        const susLabel = (d.alarm && d.alarm.muted) ? 'Susturmayi Kaldir' : 'Sustur/Sireni Kapat';
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-warn" onclick="alarmMute()">'+susLabel+'</button></div>';
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
  if(!busySet.has('#alarm-btn')) $('#alarm-btn').textContent = (d.alarm&&d.alarm.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç';
  if(!busySet.has('#panic-btn')){ const pb=$('#panic-btn'); if(pb) pb.textContent = (d.alarm&&d.alarm.panic) ? 'Panik Açık' : 'Panik'; }
  if(!busySet.has('#alarm-mod-sel')){ const ams=$('#alarm-mod-sel'); if(ams && d.alarm && d.alarm.mode) ams.value=String(d.alarm.mode); }
  if(!busySet.has('#alarm-mute-btn')){ const amb=$('#alarm-mute-btn'); if(amb) amb.textContent = (d.alarm&&d.alarm.muted) ? 'Susturma Kaldir' : 'Sustur/Sireni Kapat'; }
  if(!busySet.has('#moisture-settings-toggle-btn')){ const msb=$('#moisture-settings-toggle-btn'); if(msb) msb.textContent = mo.output ? 'Kapat' : 'Aç'; }
  if(!busySet.has('#moisture-settings-auto-btn')){ const sab=$('#moisture-settings-auto-btn'); if(sab) sab.textContent = mo.auto ? 'Manuel' : 'Otomatik'; }
  // Nem verileri
  const mkpi=$('#kpi-moisture'); if(mkpi) mkpi.textContent=(mo.percent||0).toFixed(1)+'%';
  const mraw=$('#moisture-raw'); if(mraw) mraw.textContent=mo.raw||0;
  const mout=$('#moisture-output'); if(mout) mout.textContent=mo.output?'Açık':'Kapalı';
  const mmod=$('#moisture-mode'); if(mmod) mmod.textContent=mo.auto?'Otomatik':'Manuel';
  // Ayarlar sekmesi nem göstergeleri
  const smv=$('#settings-moisture-val'); if(smv) smv.textContent=(mo.percent||0).toFixed(1)+'%';
  const smo=$('#settings-moisture-out'); if(smo) smo.textContent=mo.output?'Açık':'Kapalı';
  const smm=$('#settings-moisture-mod'); if(smm) smm.textContent=mo.auto?'Otomatik':'Manuel';
  const ml=$('#moisture-low'); if(ml&&!ml.matches(':focus')) ml.value=mo.low||0;
  const mh=$('#moisture-high'); if(mh&&!mh.matches(':focus')) mh.value=mo.high||0;
  const sml=$('#moisture-settings-low'); if(sml&&!sml.matches(':focus')) sml.value=mo.low||0;
  const smh=$('#moisture-settings-high'); if(smh&&!smh.matches(':focus')) smh.value=mo.high||0;
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
  const low=parseInt($('#moisture-settings-low').value)||0;
  const high=parseInt($('#moisture-settings-high').value)||0;
  if(low<0||low>100||high<0||high>100||low>=high){
    $('#moisture-settings-msg').textContent='0-100 arası ve alt < üst olmalı'; return;
  }
  sendCommand(null,'/api/moisture/threshold?low='+low+'&high='+high,'#moisture-settings-msg');
}
function toggleAlarm(){
  const aktif = $('#alarm-btn').textContent.trim() === 'Alarmı Kapat';
  sendCommand('#alarm-btn', '/api/alarm?aktif='+(aktif?0:1), '#alarm-sonuc');
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
function alarmOnayla(){
  sendCommand(null, '/api/alarm/onayla', '#alarm-sonuc');
}
function alarmOnaylaLamba(){
  sendCommand(null, '/api/alarm/onayla_lamba', '#alarm-sonuc');
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
const irKomutAdlari={LAMBA_AC:'Lamba Aç',LAMBA_KAPAT:'Lamba Kapat',ALARM_AC:'Alarm Aç',ALARM_KAPAT:'Alarm Kapat','ALARM_MOD=1':'Mod: Sesli','ALARM_MOD=2':'Mod: Sessiz','ALARM_MOD=3':'Mod: Onaylı',ALARM_SUSTUR:'Sustur',ALARM_ONAYLA:'Onayla',KAPI_AC:'Kapı Aç',KAPI_KAPAT:'Kapı Kapat',PANIK:'Panik'};
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
  fetch('/api/ir/ogren_durum').then(r=>r.json()).then(d=>{
    if(d.hazir){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      irKodAtamaFormuGoster(d.kod);
    } else if(d.zamanAsimi){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      $('#ir-ogren-durum').innerHTML='Zaman aşımı, tuş algılanamadı - tekrar deneyin.';
    }
  }).catch(()=>{});
}
function irKodAtamaFormuGoster(kod){
  let secenekler='';
  for(const k in irKomutAdlari) secenekler+='<option value="'+k+'">'+irKomutAdlari[k]+'</option>';
  $('#ir-ogren-durum').innerHTML='Kod alındı: <b>0x'+kod+'</b><br>'
    +'<select id="ir-komut-sec" style="margin-top:6px">'+secenekler+'</select> '
    +'<button class="btn btn-yesil" onclick="irKaydet(\''+kod+'\')" style="margin-top:6px">Kaydet</button>';
}
function irKaydet(kod){
  const sel=$('#ir-komut-sec'); const komut=sel.value; const etiket=sel.options[sel.selectedIndex].text;
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
  server.send(200, "text/html", html);
}

String durumJson() {
  DynamicJsonDocument doc(768);

  doc["level_cm"] = sensorData.level_cm;
  doc["level_percent"] = sensorData.level_percent;
  doc["level_liters"] = sensorData.level_liters;
  doc["temperature"] = sensorData.temperature;
  doc["night_mode"] = sensorData.night_mode;
  
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

  doc["konteyner"]["kapi_acik"] = kapi2Acik;
  doc["konteyner"]["pir"] = pir2HareketVar;

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
  } else if (komut == "ALARM_AC" || komut == "ALARM_KAPAT") {
    ok = alarmAyarla(komut == "ALARM_AC", reply);
    mesaj = ok ? (komut == "ALARM_AC" ? "ALARM=1" : "ALARM=0") : "ALARM";
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

// Test 1: Nano D13 LED kontrolü
// ESP32 -> RS485 -> ESP8266 (MASTER:SET_LAMBA) -> Nano (LAMBA_ON) -> D13 HIGH
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
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUpload, handleFirmwareUploadProgress);
  server.on("/firmware/esp8266.bin", HTTP_GET, handleFirmwareServe);
  server.on("/api/firmware/durum", handleFirmwareDurum);
  server.on("/api/lamba", handleAPI_Lamba);
  server.on("/api/moisture", handleAPI_MoistureToggle);
  server.on("/api/moisture/auto", handleAPI_MoistureAuto);
  server.on("/api/moisture/threshold", handleAPI_MoistureThreshold);
  server.on("/api/alarm", handleAPI_Alarm);
  server.on("/api/alarm/mod", handleAPI_AlarmMod);
  server.on("/api/alarm/mute", handleAPI_AlarmMute);
  server.on("/api/alarm/onayla", handleAPI_AlarmOnayla);
  server.on("/api/alarm/onayla_lamba", handleAPI_AlarmOnaylaLamba);
  server.on("/api/kapi", handleAPI_Kapi);
  server.on("/api/panic", handleAPI_Panic);
  server.on("/api/wifi", handleAPI_Wifi);
  server.on("/api/wifi/scan", handleAPI_WifiScan);
  server.on("/api/restart", handleAPI_Restart);
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