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
#include "../include/config.h"

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
// yagmurTahminiKontrolEt() bunu kullanir, bu yuzden ileri bildirim gerekir.
bool rs485_send_wait_ack(const char* data, String& response, unsigned long timeout_ms, uint8_t max_attempts);

// ============ Hava Durumu / Yagmur Tahmini - Konum (NVS'de kalici) ============
String savedIl = "";
String savedIlce = "";
double savedLat = 0.0;
double savedLon = 0.0;
bool rainSkipEnabled = false;
bool rainForecastTomorrow = false;
bool rainLocationValid = false;
unsigned long lastRainCheckMs = 0;
String lastRainCheckStr = "-";
#define RAIN_FORECAST_DAYS 7
String rainForecastDates[RAIN_FORECAST_DAYS];
float rainForecastMm[RAIN_FORECAST_DAYS];
int rainForecastCount = 0;

// NOT: Bu ayarlar onceden ESP32 Preferences (NVS) ile saklaniyordu, ama
// kullanicida tekrar tekrar kaydedilmeme sorunu yasandi (sebebi netlesmedi -
// muhtemelen bu boarddaki NVS partisyonuyla ilgili). Bu oturumda kayit
// yedekleme ve firmware deposu icin zaten guvenilir calistigini kanitladigimiz
// SPIFFS dosyasina tasindi.
#define LOC_AYAR_DOSYASI "/hava_ayarlari.json"

void locPrefsYukle() {
  File f = SPIFFS.open(LOC_AYAR_DOSYASI, "r");
  if (f) {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
      savedIl = doc["il"] | "";
      savedIlce = doc["ilce"] | "";
      savedLat = doc["lat"] | 0.0;
      savedLon = doc["lon"] | 0.0;
      rainSkipEnabled = doc["skipOn"] | false;
    }
    f.close();
  }
  rainLocationValid = (savedLat != 0.0 || savedLon != 0.0);
  DEBUG_PRINTLN(String("[HAVA] Yuklendi: il=") + savedIl + " ilce=" + savedIlce + " gecerli=" + String(rainLocationValid ? "1" : "0"));
}

void locPrefsKaydet() {
  DynamicJsonDocument doc(512);
  doc["il"] = savedIl;
  doc["ilce"] = savedIlce;
  doc["lat"] = savedLat;
  doc["lon"] = savedLon;
  doc["skipOn"] = rainSkipEnabled;
  File f = SPIFFS.open(LOC_AYAR_DOSYASI, "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
    DEBUG_PRINTLN(String("[HAVA] Kaydedildi: il=") + savedIl + " ilce=" + savedIlce);
  } else {
    DEBUG_PRINTLN("[HAVA] KAYIT HATASI - SPIFFS dosyasi acilamadi");
  }
}

// RFC3986 percent-encode - Turkce karakterler ve bosluklar icin gerekli
// (HTTPClient/WiFiClientSecure otomatik encode etmez).
String urlEncode(const String& s) {
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

// Il/ilce adindan enlem/boylam cozer (Open-Meteo ucretsiz geocoding API).
// Basarili olursa lat/lon'u doldurur ve true doner.
bool geocodeIlIlce(const String& il, const String& ilce, double& lat, double& lon) {
  if (WiFi.status() != WL_CONNECTED) return false;
  String query = ilce.length() > 0 ? (ilce + ", " + il + ", Turkiye") : (il + ", Turkiye");
  String url = String(RAIN_GEOCODE_API) + "?name=" + urlEncode(query) + "&count=1&language=tr&format=json";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  bool ok = false;
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray results = doc["results"].as<JsonArray>();
      if (!results.isNull() && results.size() > 0) {
        lat = results[0]["latitude"].as<double>();
        lon = results[0]["longitude"].as<double>();
        ok = true;
      }
    }
  } else {
    DEBUG_PRINTLN(String("[GEOCODE] HTTP hata: ") + String(code));
  }
  http.end();
  return ok;
}

// Il/ilce kaydedildi ama internet olmadigi icin koordinat cozulemediyse,
// internet geldiginde (en fazla 60sn'de bir) otomatik tekrar dener.
void konumCozumKontrolEt() {
  if (rainLocationValid) return;
  if (savedIl.length() == 0) return;
  if (WiFi.status() != WL_CONNECTED) return;
  static unsigned long sonDenemeMs = 0;
  if (sonDenemeMs != 0 && millis() - sonDenemeMs < 60000UL) return;
  sonDenemeMs = millis();
  double lat, lon;
  if (geocodeIlIlce(savedIl, savedIlce, lat, lon)) {
    savedLat = lat; savedLon = lon; rainLocationValid = true;
    locPrefsKaydet();
    DEBUG_PRINTLN("[RAIN] Konum internet gelince cozuldu");
  }
}

// Yarinki yagmur tahminini sorgular ve degistiyse/gecerliyse esp8266_slave'e
// RS485 ile SET_RAIN_SKIP bildirir. Internet yoksa veya konum cozulmemisse
// sessizce cikar - bir sonraki loop'ta tekrar denenir.
void yagmurTahminiKontrolEt(bool zorla = false) {
  if (!rainSkipEnabled) return;
  if (!rainLocationValid) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (!zorla && lastRainCheckMs != 0 && millis() - lastRainCheckMs < RAIN_CHECK_INTERVAL_MS) return;

  String url = String(RAIN_FORECAST_API) + "?latitude=" + String(savedLat, 4) + "&longitude=" + String(savedLon, 4) +
               "&daily=precipitation_sum&forecast_days=" + String(RAIN_FORECAST_DAYS) + "&timezone=auto";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(3072);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray dates = doc["daily"]["time"].as<JsonArray>();
      JsonArray precip = doc["daily"]["precipitation_sum"].as<JsonArray>();
      rainForecastCount = min((int)precip.size(), RAIN_FORECAST_DAYS);
      for (int i = 0; i < rainForecastCount; i++) {
        rainForecastDates[i] = dates[i].as<String>();
        rainForecastMm[i] = precip[i].as<float>();
      }
      if (rainForecastCount >= 2) {
        float yarinMm = rainForecastMm[1];
        rainForecastTomorrow = (yarinMm >= RAIN_THRESHOLD_MM);
        lastRainCheckMs = millis();
        lastRainCheckStr = String(yarinMm, 1) + "mm";
        String reply;
        rs485_send_wait_ack(rainForecastTomorrow ? "MASTER:SET_RAIN_SKIP=1\n" : "MASTER:SET_RAIN_SKIP=0\n", reply, 1000, 3);
        DEBUG_PRINTLN(String("[RAIN] Yarin: ") + yarinMm + "mm -> skip=" + (rainForecastTomorrow ? "1" : "0"));
      }
    }
  } else {
    DEBUG_PRINTLN(String("[RAIN] HTTP hata: ") + String(code));
  }
  http.end();
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
// RS485 UART1 AYARLARI
// ============================================================

void rs485_init() {
  DEBUG_PRINTLN("[RS485] Initializing UART1...");
  
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
        // KRITIK: ESP8266 kendi periyodik durumunu da bagimsiz bir zamanlayicida
        // (RS485_SEND_INTERVAL) istem disi gonderiyor. Bu eski/bekleyen mesaj
        // burada temizlenmezse, az sonra gonderecegimiz GET_STATUS'un YANITI
        // yerine bu BAYAT veriyi okuyabiliyorduk - ekranin "gec guncellenmesi"
        // hissinin buyuk kismi buradan geliyordu.
        while (Serial1.available()) Serial1.read();
        rs485_send("GET_STATUS\n");
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
        String msg = rs485_read_line();
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
        <div><small>Röle:</small> <b id="rl">-</b></div>
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
        <button class="btn btn-warn" id="role-test-btn" onclick="roleTest()">Röle Test (2sn)</button>
      </div>
      <div id="lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Test 1 - Nano D13 LED</h3>
      <div class="row">
        <button class="btn btn-primary" id="test1-btn" onclick="test1Toggle()">Aç</button>
      </div>
      <div id="test1-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
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
      <p style="font-size:12px;color:var(--muted)">Bahcede internet olmadigindan, ESP8266'nin "URL'den Guncelle" kutusuna GitHub yerine buradaki adresi yaz - ikisi ayni WiFi agindayken calisir.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="fw-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <input type="file" id="fwDosya" accept=".bin">
        <button class="btn btn-primary" onclick="firmwareYukle()">Yukle</button>
      </div>
      <div id="fw-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <p style="font-size:12px;color:var(--muted);margin-top:14px">Dosya sistemi (LittleFS - index.html/app.js, "uploadfs"): PlatformIO'da <code>pio run -t buildfs</code> ile <code>.pio/build/esp8266-12e/littlefs.bin</code> uret, buraya yukle, sonra ESP8266'nin OTA kutusuna asagidaki URL'yi yaz.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="fwfs-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <input type="file" id="fwfsDosya" accept=".bin">
        <button class="btn btn-primary" onclick="firmwareFSYukle()">Yukle</button>
      </div>
      <div id="fwfs-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Kayit Yedekleme</h3>
      <p style="font-size:12px;color:var(--muted)">ESP8266'da "uploadfs" yapilinca kayitlar.csv silinir. Buradan yedek alip geri yukleyebilirsin.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="yedek-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <button class="btn btn-primary" onclick="kayitYedekle()">Simdi Yedekle</button>
        <button class="btn btn-warn" onclick="kayitGeriYukle()">ESP8266'ya Geri Yukle</button>
      </div>
      <div id="yedek-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Hava Durumu / Yağmur Tahmini</h3>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="rain-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <select class="input" id="rainIl">
          <option value="">İl seç...</option>
          <option>Adana</option><option>Adıyaman</option><option>Afyonkarahisar</option><option>Ağrı</option>
          <option>Aksaray</option><option>Amasya</option><option>Ankara</option><option>Antalya</option>
          <option>Ardahan</option><option>Artvin</option><option>Aydın</option><option>Balıkesir</option>
          <option>Bartın</option><option>Batman</option><option>Bayburt</option><option>Bilecik</option>
          <option>Bingöl</option><option>Bitlis</option><option>Bolu</option><option>Burdur</option>
          <option>Bursa</option><option>Çanakkale</option><option>Çankırı</option><option>Çorum</option>
          <option>Denizli</option><option>Diyarbakır</option><option>Düzce</option><option>Edirne</option>
          <option>Elazığ</option><option>Erzincan</option><option>Erzurum</option><option>Eskişehir</option>
          <option>Gaziantep</option><option>Giresun</option><option>Gümüşhane</option><option>Hakkari</option>
          <option>Hatay</option><option>Iğdır</option><option>Isparta</option><option>İstanbul</option>
          <option>İzmir</option><option>Kahramanmaraş</option><option>Karabük</option><option>Karaman</option>
          <option>Kars</option><option>Kastamonu</option><option>Kayseri</option><option>Kırıkkale</option>
          <option>Kırklareli</option><option>Kırşehir</option><option>Kilis</option><option>Kocaeli</option>
          <option>Konya</option><option>Kütahya</option><option>Malatya</option><option>Manisa</option>
          <option>Mardin</option><option>Mersin</option><option>Muğla</option><option>Muş</option>
          <option>Nevşehir</option><option>Niğde</option><option>Ordu</option><option>Osmaniye</option>
          <option>Rize</option><option>Sakarya</option><option>Samsun</option><option>Siirt</option>
          <option>Sinop</option><option>Sivas</option><option>Şanlıurfa</option><option>Şırnak</option>
          <option>Tekirdağ</option><option>Tokat</option><option>Trabzon</option><option>Tunceli</option>
          <option>Uşak</option><option>Van</option><option>Yalova</option><option>Yozgat</option>
          <option>Zonguldak</option>
        </select>
        <input class="input" id="rainIlce" placeholder="İlçe (yazın)">
      </div>
      <div class="row" style="margin-top:8px">
        <button class="btn btn-primary" onclick="rainKonumKaydet()">Konumu Kaydet</button>
        <button class="btn btn-warn" onclick="rainKontrolEt()">Şimdi Kontrol Et</button>
      </div>
      <div class="row" style="margin-top:8px">
        <label><input type="checkbox" id="rainSkipToggle" onchange="rainToggle()"> Yarın yağmur bekleniyorsa otomatik sulamayı atla</label>
      </div>
      <div id="rain-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="rain-haftalik" style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap"></div>
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
      <h3>Kullanım Kılavuzu</h3>
      <div style="font-size:13px;line-height:1.6">
        <p><b>Alarm Modları</b> (Kontrol → Alarm): 1-Sesli (tetiklenince siren hemen çalışır), 2-Sessiz (siren çalışmaz, sadece bu sayfada/ESP8266'da bildirim), 3-Onaylı (tetiklenince onay bekler, "Tetiklenmeyi Onayla" ile sesli moda geçer).</p>
        <p><b>Sustur/Sireni Kapat:</b> Alarm koşulu sürse bile röleyi susturur; koşul temizlenince otomatik sıfırlanır.</p>
        <p><b>Panik:</b> Tetikleyicilerden bağımsız, elle aç/kapat anahtarı gibi çalışır - röleyi zorla açık tutar.</p>
        <p><b>Kapı/PIR/Kaçak/Düşük seviye</b> tetikleyicileri ve gündüz/gece + mod bazlı senaryolar ESP8266 panelinin "Alarm" sekmesinden ayarlanır (bu panel sadece görüntüler ve mod/susturma/onay/panik komutlarını iletir).</p>
        <p><b>WiFi:</b> Ayarlar sekmesinden ev ağını tarayıp kaydedebilirsiniz; AP (<code id="ag-ap-bilgi">-</code>) STA bağlantısından bağımsız her zaman açıktır, ağ ayarları NVS'de kalıcıdır.</p>
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
  const anyAlarm = d.alarm && d.alarm.enabled !== false && (alarmMask !== 0 || d.alarm.panic);
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
    if(anyAlarm || bekliyor){
      let msg = bekliyor ? ('ONAY BEKLIYOR - '+at) : at;
      if(d.alarm && d.alarm.muted) msg += ' (Susturuldu)';
      const tk = tetikleyenMetni((d.alarm&&d.alarm.trigger_mask)||0, d.alarm&&d.alarm.panic);
      msg += ' | Tetikleyen: '+tk;
      let html = '⚠ '+msg;
      if(bekliyor){
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
  // Hava durumu / yagmur - dropdown/input sadece ilk yuklemede doldurulur,
  // kullanici yazarken periyodik yenileme onu ezmesin diye.
  const rain=d.rain||{};
  const rdk=$('#rain-durum-kutu');
  if(rdk){
    if(!rain.gecerli && !rain.il){
      rdk.innerHTML='Konum kaydedilmedi';
    } else if(!rain.gecerli && rain.il){
      rdk.innerHTML='Konum kaydedildi: <b>'+(rain.ilce?rain.ilce+', ':'')+rain.il+'</b><br>Koordinat henuz cozulemedi - internet gelince otomatik denenecek.';
    } else {
      rdk.innerHTML='Konum: <b>'+(rain.ilce?rain.ilce+', ':'')+rain.il+'</b><br>Yarın yağmur: <b>'+(rain.tomorrow?'Evet, sulama atlanacak':'Hayır')+'</b> (son kontrol: '+(rain.lastCheck||'-')+')';
    }
  }
  if(!window.rainUIInitialized){
    window.rainUIInitialized=true;
    if(rain.il) $('#rainIl').value=rain.il;
    if(rain.ilce) $('#rainIlce').value=rain.ilce;
    $('#rainSkipToggle').checked=!!rain.skipEnabled;
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
  if(!busySet.has('#test1-btn')){ const t1=$('#test1-btn'); if(t1) t1.textContent = d.nano.lamp ? 'Kapat' : 'Aç'; }
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
function test1Toggle(){
  const acik = $('#test1-btn') && $('#test1-btn').textContent.trim() === 'Kapat';
  sendCommand('#test1-btn', '/api/test1?durum='+(acik?0:1), '#test1-sonuc');
}
function roleTest(){
  sendCommand('#role-test-btn', '/api/role-test', '#lamba-sonuc');
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
function rainKonumKaydet(){
  const il=$('#rainIl').value, ilce=$('#rainIlce').value;
  if(!il){$('#rain-sonuc').textContent='Il secin';return;}
  $('#rain-sonuc').textContent='Konum cozuluyor...';
  fetch('/api/location',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'il='+encodeURIComponent(il)+'&ilce='+encodeURIComponent(ilce)})
    .then(r=>r.json()).then(d=>{$('#rain-sonuc').textContent=d.mesaj||'';rainHaftalikYukle();guncelle();})
    .catch(()=>{$('#rain-sonuc').textContent='Hata!';});
}
function rainKontrolEt(){
  $('#rain-sonuc').textContent='Kontrol ediliyor...';
  api('/api/rain/check').then(d=>{$('#rain-sonuc').textContent=d.mesaj||'';rainHaftalikYukle();});
}
function rainToggle(){
  const aktif=$('#rainSkipToggle').checked?1:0;
  api('/api/rain/toggle?aktif='+aktif).then(d=>{$('#rain-sonuc').textContent=d.mesaj||'';rainHaftalikYukle();});
}
function rainHaftalikYukle(){
  const kutu=$('#rain-haftalik'); if(!kutu) return;
  fetch('/api/location').then(r=>r.json()).then(d=>{
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
function firmwareDurumYukle(){
  fetch('/api/firmware/durum').then(r=>r.json()).then(d=>{
    $('#fw-durum-kutu').innerHTML = d.varMi
      ? ('Yuklu: <b>'+(d.boyut/1024).toFixed(0)+' KB</b> (yukleme: '+d.yuklemeZamani+')<br>URL: <code>'+d.url+'</code>')
      : 'Henuz firmware yuklenmedi';
    $('#fwfs-durum-kutu').innerHTML = d.varMiFS
      ? ('Yuklu: <b>'+(d.boyutFS/1024).toFixed(0)+' KB</b> (yukleme: '+d.yuklemeZamaniFS+')<br>URL: <code>'+d.urlFS+'</code>')
      : 'Henuz dosya sistemi imaji yuklenmedi';
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
function firmwareFSYukle(){
  const f=$('#fwfsDosya').files[0];
  if(!f){$('#fwfs-sonuc').textContent='Dosya secin';return;}
  $('#fwfs-sonuc').textContent='Yukleniyor...';
  const fd=new FormData(); fd.append('firmware',f);
  fetch('/firmware/upload_fs',{method:'POST',body:fd})
    .then(r=>r.json()).then(d=>{$('#fwfs-sonuc').textContent='Yuklendi'; firmwareDurumYukle();})
    .catch(()=>{$('#fwfs-sonuc').textContent='Hata!';});
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
rainHaftalikYukle();
yedekDurumYukle();
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

  doc["rain"]["il"] = savedIl;
  doc["rain"]["ilce"] = savedIlce;
  doc["rain"]["gecerli"] = rainLocationValid;
  doc["rain"]["skipEnabled"] = rainSkipEnabled;
  doc["rain"]["tomorrow"] = rainForecastTomorrow;
  doc["rain"]["lastCheck"] = lastRainCheckStr;
  
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
  String url = GITHUB_FIRMWARE_URL;
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"Guncelleniyor\"}");
  delay(100);

  WiFiClientSecure client;
  client.setInsecure();  // raw.githubusercontent.com sertifika zincirini dogrulamadan kabul et
  HTTPClient http;
  http.begin(client, url);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);
      if (canBegin) {
        WiFiClient* stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);
        if (written == contentLength && Update.end()) {
          DEBUG_PRINTLN("[OTA] Update OK. Rebooting...");
          ESP.restart();
        } else {
          DEBUG_PRINTLN("[OTA] Write failed");
          Update.abort();
        }
      } else {
        DEBUG_PRINTLN("[OTA] Not enough space");
      }
    }
  } else {
    DEBUG_PRINT("[OTA] HTTP failed: ");
    DEBUG_PRINTLN(httpCode);
    DEBUG_PRINTLN(http.errorToString(httpCode).c_str());
  }
  http.end();
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
// LittleFS (data/ - index.html/app.js) imaji icin ayri depo slotu - bkz
// asagida "ESP8266 DOSYA SISTEMI (LittleFS) DEPOSU" bolumu.
#define ESP8266_FS_DOSYASI "/esp8266_littlefs.bin"
String esp8266FSYuklemeZamani = "-";

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
  bool varMiFS = SPIFFS.exists(ESP8266_FS_DOSYASI);
  size_t boyutFS = 0;
  if (varMiFS) { File f = SPIFFS.open(ESP8266_FS_DOSYASI, "r"); boyutFS = f.size(); f.close(); }
  server.send(200, "application/json", "{\"varMi\":" + String(varMi ? "true" : "false") + ",\"boyut\":" + String(boyut) +
              ",\"yuklemeZamani\":\"" + esp8266FirmwareYuklemeZamani + "\",\"url\":\"http://" + String(MDNS_NAME) + ".local/firmware/esp8266.bin\"" +
              ",\"varMiFS\":" + String(varMiFS ? "true" : "false") + ",\"boyutFS\":" + String(boyutFS) +
              ",\"yuklemeZamaniFS\":\"" + esp8266FSYuklemeZamani + "\",\"urlFS\":\"http://" + String(MDNS_NAME) + ".local/firmware/esp8266fs.bin\"}");
}

// ============ ESP8266 DOSYA SISTEMI (LittleFS) DEPOSU ============
// AMAC: OTA sadece programı (sketch/.bin) gunceller, LittleFS icindeki
// index.html/app.js DEGISMEZ - "uploadfs" ayri bir islemdir ve normalde
// USB gerektirir. Bahcede internet/USB yokken data/ klasoru degisince
// (ornegin bu oturumda alarm banner/cikis ayarlari eklendiginde) ESP8266'yi
// guncellemenin tek yolu buydu. Program deposuyla ayni mantik: dosya
// buraya (ESP32 SPIFFS) yuklenir, ESP8266 buradan ceker.
void handleFirmwareFSUpload() {
  server.send(200, "application/json", "{\"basarili\":true}");
}

File esp8266FSYazFile;
void handleFirmwareFSUploadProgress() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    DEBUG_PRINTLN(String("[FS-DEPO] Basliyor: ") + upload.filename);
    esp8266FSYazFile = SPIFFS.open(ESP8266_FS_DOSYASI, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (esp8266FSYazFile) esp8266FSYazFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (esp8266FSYazFile) {
      esp8266FSYazFile.close();
      esp8266FSYuklemeZamani = String(millis() / 1000) + "sn (uptime)";
      DEBUG_PRINTLN(String("[FS-DEPO] Kaydedildi: ") + String(upload.totalSize) + " byte");
    }
  }
}

void handleFirmwareFSServe() {
  File f = SPIFFS.open(ESP8266_FS_DOSYASI, "r");
  if (!f) { server.send(404, "text/plain", "Henuz dosya sistemi imaji yuklenmedi"); return; }
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

// ============ HAVA DURUMU / YAGMUR TAHMINI API'LERI ============
void handleAPI_LocationGet() {
  String j = "{";
  j += "\"il\":\"" + savedIl + "\",";
  j += "\"ilce\":\"" + savedIlce + "\",";
  j += "\"lat\":" + String(savedLat, 4) + ",";
  j += "\"lon\":" + String(savedLon, 4) + ",";
  j += "\"gecerli\":" + String(rainLocationValid ? "true" : "false") + ",";
  j += "\"rainSkipEnabled\":" + String(rainSkipEnabled ? "true" : "false") + ",";
  j += "\"rainForecastTomorrow\":" + String(rainForecastTomorrow ? "true" : "false") + ",";
  j += "\"lastCheck\":\"" + lastRainCheckStr + "\",";
  j += "\"haftalik\":[";
  for (int i = 0; i < rainForecastCount; i++) {
    if (i > 0) j += ",";
    j += "{\"tarih\":\"" + rainForecastDates[i] + "\",\"mm\":" + String(rainForecastMm[i], 1) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleAPI_LocationSet() {
  if (!server.hasArg("il")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"il eksik\"}"); return; }
  String il = server.arg("il");
  String ilce = server.hasArg("ilce") ? server.arg("ilce") : "";
  // FIX: Il/ilce secimini kaydetmek ile koordinat cozmek (internet gerektirir,
  // HTTPS/TLS istegi asilabilir/uzun surebilir) ayni islemdeymis gibi
  // yapiliyordu - geocode adimi tamamlanmadan/basarisiz olursa kaydetme hic
  // gerceklesmiyordu, sayfa yenilenince secim sifirlaniyordu. Simdi il/ilce
  // ONCE, geocode denemeden HEMEN kaydedilir; boylece o adim ne olursa olsun
  // secim guvende. Koordinat cozulemezse konumCozumKontrolEt() internet
  // gelince tekrar dener.
  savedIl = il; savedIlce = ilce;
  locPrefsKaydet();
  double lat = 0, lon = 0;
  bool geocodeOk = geocodeIlIlce(il, ilce, lat, lon);
  rainLocationValid = geocodeOk;
  if (geocodeOk) { savedLat = lat; savedLon = lon; locPrefsKaydet(); }
  if (geocodeOk) {
    lastRainCheckMs = 0;  // konum degisti, hemen yeniden kontrol edilsin
    yagmurTahminiKontrolEt(true);
  }
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" +
              String(geocodeOk ? "Konum kaydedildi ve bulundu" : "Il/ilce kaydedildi - internet gelince koordinat otomatik cozulecek") +
              "\",\"lat\":" + String(lat, 4) + ",\"lon\":" + String(lon, 4) + "}");
}

void handleAPI_RainToggle() {
  if (!server.hasArg("aktif")) { server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"aktif eksik\"}"); return; }
  rainSkipEnabled = server.arg("aktif").toInt() != 0;
  locPrefsKaydet();
  if (rainSkipEnabled) { lastRainCheckMs = 0; yagmurTahminiKontrolEt(true); }
  else {
    String reply;
    rs485_send_wait_ack("MASTER:SET_RAIN_SKIP=0\n", reply, 1000, 3);
  }
  server.send(200, "application/json", "{\"basarili\":true,\"mesaj\":\"" + String(rainSkipEnabled ? "Etkin" : "Kapali") + "\"}");
}

void handleAPI_RainCheckNow() {
  yagmurTahminiKontrolEt(true);
  server.send(200, "application/json", "{\"basarili\":" + String(rainLocationValid ? "true" : "false") + ",\"mesaj\":\"" +
              String(rainLocationValid ? "Kontrol edildi" : "Once konum kaydet") + "\",\"rainForecastTomorrow\":" + String(rainForecastTomorrow ? "true" : "false") + "}");
}

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

// ============ RS485 KOMUT API'LERI ============
// FIX: Nano'nun anladığı komut formatına uygun:
//   LAMBA_ON / LAMBA_OFF / RELAY_ON / RELAY_OFF / GET_STATUS
void handleAPI_Lamba() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  rs485_api_busy = true; // poll'u durdur
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_LAMBA=1\n" : "MASTER:SET_LAMBA=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) {
    nanoStatus.lamp_on = (d == 1);
    last_rs485_update_ms = millis(); // poll timer'ı sıfırla - hemen tekrar GET_STATUS göndermesin
  }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Lamba Acik" : "Lamba Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_Alarm() {
  if (!server.hasArg("aktif")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"aktif eksik\"}");
    return;
  }
  int d = server.arg("aktif").toInt();
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_ALARM=1\n" : "MASTER:SET_ALARM=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.enabled = (d == 1); last_rs485_update_ms = millis(); }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Alarm Aktif" : "Alarm Pasif") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
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
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack((String("MASTER:SET_ALARM_MOD=") + m + "\n").c_str(), reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.mode = (uint8_t)m; alarmStatus.muted = false; alarmStatus.pending = false; last_rs485_update_ms = millis(); }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? ("Mod " + String(m)) : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_AlarmMute() {
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_MUTE\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.muted = !alarmStatus.muted; last_rs485_update_ms = millis(); }
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"muted\":" + String(alarmStatus.muted ? "true" : "false") + ",\"reply\":\"" + reply + "\"}");
}

void handleAPI_AlarmOnayla() {
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ALARM_ONAYLA\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { alarmStatus.pending = false; last_rs485_update_ms = millis(); }
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

void handleAPI_RoleTest() {
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:ROLE_TEST\n", reply, 2000);
  rs485_api_busy = false;
  if (ok) last_rs485_update_ms = millis();
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Role testi gonderildi" : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

void handleAPI_Kapi() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_KAPI=1\n" : "MASTER:SET_KAPI=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) last_rs485_update_ms = millis();
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Kapi Acik" : "Kapi Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

// ============ PANIK BUTONU (ESP8266 ile senkronize) ============
// ESP8266'daki /role/panic ile aynı toggle davranışı.
// RS485 üzerinden MASTER:PANIC gönderir, ESP8266 toggle yapar ve
// ACK:PANIC=1 veya ACK:PANIC=0 ile yeni durumu döndürür.
void handleAPI_Panic() {
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack("MASTER:PANIC\n", reply, 1000, 3);
  rs485_api_busy = false;

  // ACK yanıtından panik durumunu çöz: "ACK:PANIC=1" veya "ACK:PANIC=0"
  bool panicActive = false;
  if (ok) {
    int eqIdx = reply.indexOf("PANIC=");
    if (eqIdx >= 0) {
      panicActive = (reply.substring(eqIdx + 6).startsWith("1"));
    }
    alarmStatus.panic_mode = panicActive;
    last_rs485_update_ms = millis();
  }

  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") +
    ",\"mesaj\":\"" + String(ok ? (panicActive ? "Panik Acik" : "Panik Kapali") : "Komut hatasi") +
    "\",\"panic\":" + String(panicActive ? "true" : "false") +
    ",\"reply\":\"" + reply + "\"}");
}

// Test 1: Nano D13 LED kontrolü
// ESP32 -> RS485 -> ESP8266 (MASTER:SET_LAMBA) -> Nano (LAMBA_ON) -> D13 HIGH
void handleAPI_Test1() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  int d = server.arg("durum").toInt();
  Serial.printf("[TEST1] RS485 gonderiliyor: MASTER:SET_LAMBA=%d\n", d);
  String reply;
  rs485_api_busy = true;
  bool ok = rs485_send_wait_ack(d ? "MASTER:SET_LAMBA=1\n" : "MASTER:SET_LAMBA=0\n", reply, 1000, 3);
  rs485_api_busy = false;
  if (ok) { nanoStatus.lamp_on = (d == 1); last_rs485_update_ms = millis(); }
  Serial.println("[TEST1] RS485 gonderildi");
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? (d ? "Test1 Acik" : "Test1 Kapali") : "Komut hatasi") + "\",\"reply\":\"" + reply + "\"}");
}

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
  server.on("/api/location", HTTP_GET, handleAPI_LocationGet);
  server.on("/api/location", HTTP_POST, handleAPI_LocationSet);
  server.on("/api/rain/toggle", handleAPI_RainToggle);
  server.on("/api/rain/check", handleAPI_RainCheckNow);
  server.on("/api/kayit/yedekle", handleAPI_KayitYedekle);
  server.on("/api/kayit/geri_yukle", handleAPI_KayitGeriYukle);
  server.on("/api/kayit/yedek_durum", handleAPI_KayitYedekDurum);
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUpload, handleFirmwareUploadProgress);
  server.on("/firmware/esp8266.bin", HTTP_GET, handleFirmwareServe);
  server.on("/firmware/upload_fs", HTTP_POST, handleFirmwareFSUpload, handleFirmwareFSUploadProgress);
  server.on("/firmware/esp8266fs.bin", HTTP_GET, handleFirmwareFSServe);
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
  server.on("/api/role-test", handleAPI_RoleTest);
  server.on("/api/kapi", handleAPI_Kapi);
  server.on("/api/panic", handleAPI_Panic);
  server.on("/api/test1", handleAPI_Test1);
  server.on("/api/wifi", handleAPI_Wifi);
  server.on("/api/wifi/scan", handleAPI_WifiScan);
  server.on("/api/restart", handleAPI_Restart);
  
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

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(9600);
  delay(1000);
  
  DEBUG_PRINTLN("\n========================================");
  DEBUG_PRINTLN("🟢 SuDepoSis v2 - ESP32-S3 Master");
  DEBUG_PRINTLN("========================================");
  
  // RS485 Initialize
  rs485_init();

  // SPIFFS - kayit yedekleme icin (bkz esp8266KayitYedekle/GeriYukle)
  if (!SPIFFS.begin(true)) {
    DEBUG_PRINTLN("[SPIFFS] Baslatilamadi");
  }

  // WiFi Connect
  wifi_connect();
  setup_ota();

  // Hava durumu / yagmur tahmini - kayitli konumu yukle
  locPrefsYukle();
  
  // MQTT Setup
  if (ENABLE_MQTT) {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    DEBUG_PRINT("[MQTT] Broker: ");
    DEBUG_PRINTLN(MQTT_BROKER);
  }
  
  // Web Server
  setupWebServer();
  
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
  // Web Server handle
  server.handleClient();
  ArduinoOTA.handle();
  
  // RS485 Polling
  rs485_poll();

  // Il/ilce secilmis ama internet olmadigi icin koordinat cozulememisse tekrar dene
  konumCozumKontrolEt();

  // Hava durumu / yagmur tahmini - internet varken periyodik kontrol
  yagmurTahminiKontrolEt();
  
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