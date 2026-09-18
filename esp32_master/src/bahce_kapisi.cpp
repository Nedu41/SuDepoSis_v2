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
extern bool bahceKapi1TamAcikAl();
extern bool bahceKapi1TamKapaliAl();
extern bool bahceKapi2TamAcikAl();
extern bool bahceKapi2TamKapaliAl();
bool bahceKapiKomutGonder(const char* aksiyon, String& reply);           // tanimi asagida
bool bahceKapiTekKomutGonder(int kapi, const char* aksiyon, String& reply); // tanimi asagida

// Fiziksel Bahce Kapisi Butonu (GPIO47, INPUT_PULLUP, aktif-LOW).
// 2026-09-18 kullanici talebi: eski "kisa/cift TIK sayma" semasindan basili-
// TUTMA suresine gecildi - daha sezgisel (bir kapi kontrolu = kisa tik, iki
// kapi kontrolu = uzun basis):
//   KISA basis (birakildiginda BAHCE_KAPI_UZUN_BASIS_MS'den once) -> SADECE
//   Kapi 2 (SAG): hareketteyse DUR, aciksa KAPAT, degilse AC. Kapi 1'in
//   durumu bu karari HIC etkilemez (kullanici talebi - iki kapi birbirinden
//   bagimsiz kisa-basis kapsaminda).
//   UZUN basis (BAHCE_KAPI_UZUN_BASIS_MS doldugu AN, hala basili - birakmayi
//   BEKLEMEZ) -> IKI KAPI birden: herhangi biri HATA'daysa Home (motor
//   surmeden her kanadi kendi gercek limit switch konumuna resenkronize eder,
//   bkz web arayuzundeki ayni-mantikli Home butonu), yoksa herhangi biri
//   hareketteyse DUR, aciksa KAPAT, degilse AC (ikisi birden).
// Motor/role mantigi ESP8266/Sudepo tarafinda, buradan sadece HTTP komutu
// gonderilir (bkz bahceKapiKomutGonder/bahceKapiTekKomutGonder).
void bahceKapiButonPoll() {
  static bool oncekiBasili = false;
  static unsigned long sonDegisimMs = 0;
  static unsigned long sonTetikMs = 0;
  static unsigned long basisBaslangicMs = 0;
  static bool uzunTetiklendi = false;

  unsigned long now = millis();
  bool basili = (digitalRead(BAHCE_KAPI_BUTON_PIN) == LOW);
  if (basili != oncekiBasili && now - sonDegisimMs > 50) {
    sonDegisimMs = now;
    oncekiBasili = basili;
    if (basili) {
      basisBaslangicMs = now;
      uzunTetiklendi = false;
    } else if (!uzunTetiklendi && now - sonTetikMs > BAHCE_KAPI_BUTON_COOLDOWN_MS) {
      // Birakildi VE uzun basis olarak zaten tetiklenmedi -> KISA basis.
      sonTetikMs = now;
      uint8_t bk2 = bahceKapi2DurumAl();
      bool hareket2 = (bk2 == 2 || bk2 == 3 || bk2 == 4);
      bool acik2 = (bk2 == 1);
      String reply;
      bool ok = hareket2 ? bahceKapiTekKomutGonder(2, "DUR", reply)
              : acik2    ? bahceKapiTekKomutGonder(2, "KAPAT", reply)
                         : bahceKapiTekKomutGonder(2, "AC", reply);
      Serial.printf("[BAHCE_KAPI_BUTON] kisa basis (Kapi2) sonuc=%d\n", ok);
    }
  }

  // Basili tutulurken UZUN basis esigini gectigi AN (birakmayi beklemeden) tetikle.
  if (basili && !uzunTetiklendi && now - basisBaslangicMs >= BAHCE_KAPI_UZUN_BASIS_MS) {
    uzunTetiklendi = true;
    if (now - sonTetikMs <= BAHCE_KAPI_BUTON_COOLDOWN_MS) return;
    sonTetikMs = now;

    uint8_t bk1 = bahceKapi1DurumAl(), bk2 = bahceKapi2DurumAl();
    bool hataVar = (bk1 == 5 || bk2 == 5);
    bool hareketVar = (bk1 == 2 || bk1 == 3 || bk1 == 4 || bk2 == 2 || bk2 == 3 || bk2 == 4);
    bool acikVar = (bk1 == 1 || bk2 == 1);
    String reply;
    bool ok;
    if (hataVar) {
      // Home: her kanadi KENDI gercek limit switch konumuna gore resenkronize
      // et (kapiAcKomut/kapiKapatKomut zaten hedef konumdaysa motoru
      // surmeden sadece durumu duzeltiyor, bkz esp8266_slave bahce_kapisi.cpp).
      ok = true;
      if (bahceKapi1TamKapaliAl()) ok &= bahceKapiTekKomutGonder(1, "KAPAT", reply);
      else if (bahceKapi1TamAcikAl()) ok &= bahceKapiTekKomutGonder(1, "AC", reply);
      if (bahceKapi2TamKapaliAl()) ok &= bahceKapiTekKomutGonder(2, "KAPAT", reply);
      else if (bahceKapi2TamAcikAl()) ok &= bahceKapiTekKomutGonder(2, "AC", reply);
    } else if (hareketVar) {
      ok = bahceKapiKomutGonder("DUR", reply);
    } else if (acikVar) {
      ok = bahceKapiKomutGonder("KAPAT", reply);
    } else {
      ok = bahceKapiKomutGonder("AC", reply);
    }
    Serial.printf("[BAHCE_KAPI_BUTON] uzun basis (iki kapi%s) sonuc=%d\n", hataVar ? "/Home" : "", ok);
  }
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
