// Bahçe Kapısı (araç girişi) - Konteyner (ESP32) tarafı. Bkz bahce_kapisi.h.
#include <WebServer.h>
#include <cstring>
#include "config.h"
#include "bahce_kapisi.h"

extern WebServer server;
extern unsigned long last_rs485_update_ms;
// 2026-09-15: RS485 komut kanali (SoftwareSerial + R413D08 Modbus trafigiyle
// paylasilan yari-cift-yonlu hat) tekrar tekrar "kopma"ya yol acti - Sudepo'nun
// KENDI yerel arayuzunden kontrol her zaman sorunsuzdu. Kok neden RS485
// hattinin kendisi oldugundan, kapi komutlari artik WiFi/HTTP ile Sudepo'nun
// kendi /api/kapi/* uclarina gonderiliyor (bkz sudepoHttpGet, main.cpp) - RS485
// SADECE durum senkronu (periyodik GET_STATUS) icin kullanilmaya devam eder.
extern bool sudepoHttpGet(const String& path, String& reply, uint16_t timeout_ms);
extern uint8_t bahceKapi1DurumAl();
extern uint8_t bahceKapi2DurumAl();
bool bahceKapiKomutGonder(const char* aksiyon, String& reply);           // tanimi asagida
bool bahceKapiTekKomutGonder(int kapi, const char* aksiyon, String& reply); // tanimi asagida

// Fiziksel Bahce Kapisi Butonu (GPIO47, INPUT_PULLUP, aktif-LOW) - ACIL_BUTON
// ile AYNI kenar-debounce deseni, ustune CIFT BASIS ayrimi eklendi (2026-09-08
// kullanici talebi): TEK basis = sadece Kapi 1 (sol) acilir, CIFT basis (
// BAHCE_KAPI_CIFT_BASIS_PENCERE_MS icinde) = iki kanat da acilir. Herhangi bir
// kanat ACIK/HAREKET halindeyse basis sayisi onemsenmez - o an surmekte olan
// hareket varsa DURDURULUR, yoksa (ikisi de kapaliysa) KAPATILIR. Boylece ayri
// bir "kapat" veya "dur" butonu gerekmez, ayni buton uc islevi de karsilar.
// Motor/role mantigi ESP8266/Sudepo tarafinda, buradan sadece RS485 komutu
// gonderilir (bkz esp8266_slave rs485KomutDinle).
void bahceKapiButonPoll() {
  static bool oncekiBasili = false;
  static unsigned long sonDegisimMs = 0;
  static unsigned long sonTetikMs = 0;
  static unsigned long ilkBasisMs = 0;
  static uint8_t basisSayaci = 0;
  static bool kararBekliyor = false;

  unsigned long now = millis();
  bool basili = (digitalRead(BAHCE_KAPI_BUTON_PIN) == LOW);
  if (basili != oncekiBasili && now - sonDegisimMs > 50) {
    sonDegisimMs = now;
    oncekiBasili = basili;
    if (basili) {
      if (basisSayaci == 0) ilkBasisMs = now;
      basisSayaci++;
      kararBekliyor = true;
    }
  }

  if (!kararBekliyor || now - ilkBasisMs < BAHCE_KAPI_CIFT_BASIS_PENCERE_MS) return;
  bool ikiliBasis = basisSayaci >= 2;
  basisSayaci = 0;
  kararBekliyor = false;
  if (now - sonTetikMs <= BAHCE_KAPI_BUTON_COOLDOWN_MS) return;
  sonTetikMs = now;

  uint8_t bk1 = bahceKapi1DurumAl(), bk2 = bahceKapi2DurumAl();
  bool hareketVar = (bk1 == 2 || bk1 == 3 || bk1 == 4 || bk2 == 2 || bk2 == 3 || bk2 == 4);
  bool acikVar = (bk1 == 1 || bk2 == 1);

  bool ok;
  String reply;
  if (hareketVar) ok = bahceKapiKomutGonder("DUR", reply);
  else if (acikVar) ok = bahceKapiKomutGonder("KAPAT", reply);
  else if (ikiliBasis) ok = bahceKapiKomutGonder("AC", reply);
  else ok = bahceKapiTekKomutGonder(1, "AC", reply);
  Serial.printf("[BAHCE_KAPI_BUTON] sonuc=%d\n", ok);
}

// 2026-09-15 kullanici talebi: kendi karar/toggle mantigi UYDURMA - Kalburum'un
// butonu Sudepo'nun ucuna DOGRUDAN ilettigi TEK satirlik bir cagri olsun.
// 2026-09-17 DUZELTME: "ikisini de ac/kapat" icin BURADA iki ayri tek-kapi
// istegi (kapi=1 + kapi=2) atmak YANLISTI - Sudepo'daki kademeli R5/R3/R1
// sekansini (kapiCiftKanatAc) DEVRE DISI birakip iki bagimsiz kapinin ayni
// anda acilmasina yol aciyordu (sahada goruldu). Sudepo'nun KENDI "Ikisini
// de Ac/Kapat" butonu da ayni hataya dusup ayni sekilde duzeltildi - dogru
// karsilik Sudepo'nun /api/kapi/ac_cift|kapat_cift|dur_cift ucu.
bool bahceKapiKomutGonder(const char* aksiyon, String& reply) {
  const char* ek = (String(aksiyon) == "AC") ? "ac_cift" : (String(aksiyon) == "KAPAT") ? "kapat_cift" : "dur_cift";
  bool ok = sudepoHttpGet(String("/api/kapi/") + ek, reply, 2000);
  if (ok) last_rs485_update_ms = millis();
  return ok;
}

bool bahceKapiTekKomutGonder(int kapi, const char* aksiyon, String& reply) {
  const char* yon = (String(aksiyon) == "AC") ? "ac" : (String(aksiyon) == "KAPAT") ? "kapat" : "dur";
  bool ok = sudepoHttpGet(String("/api/kapi/") + yon + "?kapi=" + String(kapi), reply, 2000);
  if (ok) last_rs485_update_ms = millis();
  return ok;
}

void handleAPI_BahceKapi() {
  if (!server.hasArg("durum")) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum eksik\"}");
    return;
  }
  String durum = server.arg("durum");
  const char* aksiyon = durum == "ac" ? "AC" : (durum == "kapat" ? "KAPAT" : (durum == "dur" ? "DUR" : nullptr));
  if (!aksiyon) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"durum ac/kapat/dur olmali\"}");
    return;
  }
  int kapi = server.hasArg("kapi") ? server.arg("kapi").toInt() : 0;
  if (kapi != 0 && kapi != 1 && kapi != 2) {
    server.send(400, "application/json", "{\"basarili\":false,\"mesaj\":\"kapi 1 veya 2 olmali\"}");
    return;
  }
  String reply;
  bool ok = (kapi == 0) ? bahceKapiKomutGonder(aksiyon, reply) : bahceKapiTekKomutGonder(kapi, aksiyon, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Komut gonderildi" : "Sudepo yanit vermedi") + "\"}");
}
