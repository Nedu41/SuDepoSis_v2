// Bahçe Kapısı (araç girişi) - Konteyner (ESP32) tarafı. Bkz bahce_kapisi.h.
#include <WebServer.h>
#include "config.h"
#include "bahce_kapisi.h"

extern WebServer server;
extern unsigned long last_rs485_update_ms;
extern bool rs485_send_wait_ack(const char* data, String& response, unsigned long timeout_ms, uint8_t max_attempts);
extern uint8_t bahceKapi1DurumAl();
extern uint8_t bahceKapi2DurumAl();

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

  const char* cmd;
  if (hareketVar) cmd = "MASTER:BAHCE_KAPI_DUR\n";
  else if (acikVar) cmd = "MASTER:BAHCE_KAPI_KAPAT\n";
  else cmd = ikiliBasis ? "MASTER:BAHCE_KAPI_AC\n" : "MASTER:BAHCE_KAPI1_AC\n";

  String reply;
  bool ok = rs485_send_wait_ack(cmd, reply, 1000, 3);
  Serial.printf("[BAHCE_KAPI_BUTON] %s gonderildi, sonuc=%d\n", cmd, ok);
}

// Bahce kapisi (arac girisi) - motor/rolelerin kendisi ESP8266/Sudepo
// tarafinda (R413D08 uzerinden), burada sadece komut iletilir. Gercek durum
// (acik/kapali/hareket halinde) bir sonraki GET_STATUS ile BAHCE1/BAHCE2
// alanindan gelir (bkz parse_esp8266_data) - burada varsayimsal atama YOK.
bool bahceKapiKomutGonder(const char* aksiyon, String& reply) {
  String cmd = "MASTER:BAHCE_KAPI_" + String(aksiyon) + "\n";
  bool ok = rs485_send_wait_ack(cmd.c_str(), reply, 1000, 3);
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
  String reply;
  bool ok = bahceKapiKomutGonder(aksiyon, reply);
  server.send(200, "application/json", "{\"basarili\":" + String(ok ? "true" : "false") + ",\"mesaj\":\"" + String(ok ? "Komut gonderildi" : "Sudepo yanit vermedi") + "\"}");
}
