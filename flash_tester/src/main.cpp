// SPI NOR Flash (SOIC-8) saglamlik testi - ESP32 VSPI uzerinden JEDEC sorgusu.
//
// Cip takili degilken veya oluyken MISO hatti bosta kalir ve 0xFF okunur;
// kisa devreli cipte 0x00 gelir. Iki uc deger de "cevap yok" sayilir -
// gercek bir cip her zaman anlamli bir uretici kodu doner.
//
// Sonuclar hem seri porttan hem de kartin kendi WiFi agindaki web
// sayfasindan izlenebilir (AP modu, sifre gerektiren kapali ag).

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "secrets.h"
#include "web_ui.h"

#define FLASH_CS    5           // VSPI SS
#define SPI_HZ      1000000UL   // 1 MHz - uzun jumper kablolarda guvenli
#define OKUMA_MS    1000        // testin tekrarlanma araligi

// Normalde ev agina (STA) baglanir ve http://flashtest.local adresinden
// acilir. Ag bulunamazsa kendi AP'sini acar - cihaz sahada/baska yerde de
// calissin diye. WPA2 en az 8 karakter ister; kisa sifre AP'yi sessizce
// baslatmiyor (bu tuzaga daha once Kalburum'da yakalanildi).
const char* MDNS_ADI = "flashtest";
const char* AP_SSID  = "FlashTester";
const char* AP_SIFRE = "flash1234";
static bool apModunda = false;

// SPI NOR standart komutlari
#define CMD_JEDEC_ID    0x9F
#define CMD_READ_STATUS 0x05
#define CMD_READ_DATA   0x03
#define CMD_UNIQUE_ID   0x4B   // fabrikada yazilmis 64-bit seri no

static SPISettings ayar(SPI_HZ, MSBFIRST, SPI_MODE0);
static WebServer server(80);

struct Sonuc {
  bool     saglam;
  uint8_t  id[3];
  uint8_t  sr;
  uint8_t  veri[16];
  uint8_t  uid[8];
  bool     uidVar;
  uint32_t bayt;
};
static Sonuc son;
static uint32_t sonOkuma = 0;

static const char* ureticiAdi(uint8_t m) {
  switch (m) {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0x20: return "XMC / Micron";
    case 0x5E: return "Zbit";
    case 0x1C: return "EON";
    case 0xBF: return "SST";
    case 0x01: return "Spansion / Cypress";
    case 0xC2: return "Macronix";
    case 0x9D: return "ISSI";
    case 0x0B: return "XTX";
    case 0x68: return "Boya (BY25Q)";
    case 0x85: return "Puya";
    case 0xA1: return "Fudan";
    default:   return "BILINMIYOR";
  }
}

// JEDEC kapasite bayti 2^n bayt anlamina gelir (0x18 = 2^24 = 16 MB)
static uint32_t kapasiteBayt(uint8_t c) {
  return (c >= 0x10 && c <= 0x1B) ? (1UL << c) : 0;
}

static String hex2(uint8_t v) {
  char b[3];
  snprintf(b, sizeof(b), "%02X", v);
  return String(b);
}

static void flashOku() {
  SPI.beginTransaction(ayar);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(CMD_JEDEC_ID);
  son.id[0] = SPI.transfer(0x00);
  son.id[1] = SPI.transfer(0x00);
  son.id[2] = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();

  bool cevapYok = (son.id[0] == 0xFF && son.id[1] == 0xFF && son.id[2] == 0xFF) ||
                  (son.id[0] == 0x00 && son.id[1] == 0x00 && son.id[2] == 0x00);
  son.saglam = !cevapYok;
  son.bayt   = kapasiteBayt(son.id[2]);

  if (!son.saglam) return;

  SPI.beginTransaction(ayar);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(CMD_READ_STATUS);
  son.sr = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();

  // Benzersiz seri no - AYNI MODEL iki cipi birbirinden ayiran tek sey.
  // Sokulmus cipler karistiginda hangisinin hangisi oldugunu bu soyler.
  SPI.beginTransaction(ayar);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(CMD_UNIQUE_ID);
  for (uint8_t i = 0; i < 4; i++) SPI.transfer(0x00);   // 4 dummy bayt
  son.uidVar = false;
  for (uint8_t i = 0; i < 8; i++) {
    son.uid[i] = SPI.transfer(0x00);
    if (son.uid[i] != 0xFF && son.uid[i] != 0x00) son.uidVar = true;
  }
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();

  // Adres 0'dan 16 bayt - veri yolunun gercekten calistigini gosterir
  SPI.beginTransaction(ayar);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(CMD_READ_DATA);
  SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00);
  for (uint8_t i = 0; i < 16; i++) son.veri[i] = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();
}

// 0xE9 = ESP firmware image magic byte - cipin icinde eski bir firmware
// duruyorsa bunu bilmek kurtarma isinde ise yarar
static const char* icerikAdi() {
  if (son.veri[0] == 0xE9) return "ESP firmware image (0xE9)";
  if (son.veri[0] == 0xFF) return "bos / silinmis";
  return "bilinmeyen veri";
}

static String kapasiteMetni() {
  if (!son.bayt) return "cozulemedi";
  return String(son.bayt / (1024UL * 1024UL)) + " MB (" +
         String(son.bayt / (128UL * 1024UL)) + " Mbit)";
}

static void seriYaz() {
  Serial.print(F("JEDEC ID : "));
  Serial.println(hex2(son.id[0]) + " " + hex2(son.id[1]) + " " + hex2(son.id[2]));

  if (!son.saglam) {
    Serial.println(F("SONUC    : CEVAP YOK"));
    Serial.println(F("           Cip olu, takili degil, ters takilmis"));
    Serial.println(F("           veya WP#/HOLD# 3.3V'a baglanmamis olabilir."));
  } else {
    Serial.print(F("Uretici  : ")); Serial.println(ureticiAdi(son.id[0]));
    Serial.print(F("Kapasite : ")); Serial.println(kapasiteMetni());
    Serial.print(F("Status   : 0x")); Serial.println(hex2(son.sr));
    Serial.print(F("SERI NO  : "));
    if (son.uidVar) { for (uint8_t i = 0; i < 8; i++) Serial.print(hex2(son.uid[i])); }
    else Serial.print(F("cip desteklemiyor"));
    Serial.println();
    Serial.print(F("Ilk 16 B : "));
    for (uint8_t i = 0; i < 16; i++) { Serial.print(hex2(son.veri[i])); Serial.print(' '); }
    Serial.println();
    Serial.print(F("ICERIK   : ")); Serial.println(icerikAdi());
    Serial.println(F("SONUC    : SAGLAM"));
  }
  Serial.println(F("---"));
}

static void apiGonder() {
  String hexStr;
  for (uint8_t i = 0; i < 16; i++) { hexStr += hex2(son.veri[i]); hexStr += ' '; }

  String j = "{\"saglam\":";
  j += son.saglam ? "true" : "false";
  j += ",\"jedec\":\"" + hex2(son.id[0]) + " " + hex2(son.id[1]) + " " + hex2(son.id[2]) + "\"";
  j += ",\"uretici\":\"" + String(ureticiAdi(son.id[0])) + "\"";
  j += ",\"kapasite\":\"" + kapasiteMetni() + "\"";
  j += ",\"status\":\"0x" + hex2(son.sr) + "\"";
  String uidStr;
  if (son.uidVar) { for (uint8_t i = 0; i < 8; i++) uidStr += hex2(son.uid[i]); }
  else uidStr = "cip desteklemiyor";
  j += ",\"seri\":\"" + uidStr + "\"";
  j += ",\"icerik\":\"" + String(icerikAdi()) + "\"";
  j += ",\"hex\":\"" + hexStr + "\"}";

  server.send(200, "application/json", j);
}

// Iki ag da denenir (evde/atolyede farkli aglar var). Hicbiri yoksa AP'ye duser.
static void wifiBaglan() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  const char* ssid[2]  = { WIFI_SSID, WIFI_SSID2 };
  const char* sifre[2] = { WIFI_PASSWORD, WIFI_PASSWORD2 };

  for (uint8_t a = 0; a < 2; a++) {
    if (!ssid[a] || !ssid[a][0]) continue;
    Serial.print(F("WiFi denemesi: ")); Serial.println(ssid[a]);
    WiFi.begin(ssid[a], sifre[a]);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 4000) delay(200);
    if (WiFi.status() == WL_CONNECTED) { apModunda = false; return; }
    WiFi.disconnect();
  }

  Serial.println(F("Ag bulunamadi - AP moduna geciliyor"));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_SIFRE);
  apModunda = true;
}

// STA'da baglanti koparsa kendiliginden geri gelsin - reconnect poll'u
// olmayan kartlar "calisiyor ama sayfa acilmiyor" sikayetine yol aciyor.
static void wifiPoll() {
  static uint32_t sonDeneme = 0;
  if (apModunda || WiFi.status() == WL_CONNECTED) return;
  if (millis() - sonDeneme < 15000) return;
  sonDeneme = millis();
  WiFi.reconnect();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(FLASH_CS, OUTPUT);
  digitalWrite(FLASH_CS, HIGH);
  SPI.begin();   // SCK=18, MISO=19, MOSI=23

  wifiBaglan();

  // MDNS.begin() unutulursa .local adresi hic cozulmez - daha once
  // Kalburum'da tam bu yuzden saatler kaybedilmisti.
  if (MDNS.begin(MDNS_ADI)) MDNS.addService("http", "tcp", 80);

  server.on("/", []() { server.send_P(200, "text/html", SAYFA_HTML); });
  server.on("/api", apiGonder);
  server.begin();

  Serial.println();
  Serial.println(F("=== SPI NOR Flash Test Cihazi ==="));
  Serial.println(F("Baglanti: CS=GPIO5  MISO=GPIO19  MOSI=GPIO23  CLK=GPIO18"));
  Serial.println(F("Flash pin3 (WP#) ve pin7 (HOLD#) 3.3V'a baglanmali!"));
  if (apModunda) {
    Serial.print(F("WiFi AP  : ")); Serial.print(AP_SSID);
    Serial.print(F("  sifre: ")); Serial.println(AP_SIFRE);
    Serial.print(F("Web      : http://")); Serial.println(WiFi.softAPIP());
  } else {
    Serial.print(F("WiFi agi : ")); Serial.println(WiFi.SSID());
    Serial.print(F("Web      : http://")); Serial.print(WiFi.localIP());
    Serial.print(F("  veya  http://")); Serial.print(MDNS_ADI); Serial.println(F(".local"));
  }
  Serial.println();
}

void loop() {
  server.handleClient();
  wifiPoll();

  if (millis() - sonOkuma >= OKUMA_MS) {
    sonOkuma = millis();
    flashOku();
    seriYaz();
  }
}
