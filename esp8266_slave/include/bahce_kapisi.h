// Bahçe Kapısı (araç girişi, 2 kanat) + zil butonu - R413D08 Modbus röle
// üzerinden motor/kilit kontrolü, Nano üzerinden limit switch/akım okuması.
// Bkz proje hafızası project_bahce_kapisi_motor_gelecek_ozellik.
#ifndef BAHCE_KAPISI_H
#define BAHCE_KAPISI_H

#include <Arduino.h>

enum KapiDurum { KAPI_KAPALI, KAPI_ACIK, KAPI_KILIT_ACILIYOR, KAPI_HAREKET_AC, KAPI_HAREKET_KAPA, KAPI_HATA };
const char* kapiDurumAdi(KapiDurum d);

struct BahceKapisi {
  KapiDurum durum = KAPI_KAPALI;  // gercek konum SADECE limit switch/alarm sensoru ile dogrulanir - kalici degil, boot'ta bilinmiyor sayilir
  unsigned long hareketBaslangicMs = 0;
  unsigned long kilitPulseBaslangicMs = 0;
  unsigned long sonPollMs = 0;
  uint8_t releA, releB, releKilit;
  int acikPin, akimPin;
  bool hataAsiriAkim = false;
  bool birlikte = false;  // bu hareket iki kanadin BIRLIKTE komutuyla mi baslatildi (bkz kapiAcKomut/kapiKapatKomut)
};
extern BahceKapisi bahceKapi[2];

// Modbus fonksiyon 0x05 (Write Single Coil) - koilNo 0-tabanli kanal (0-7).
void r413RoleYaz(uint8_t koilNo, bool acik);

void kapiTumRoleleriKapat();
// birlikte=true: asiri akimda SADECE bu kanat degil, DIGER kanat da (hareket
// halindeyse) ayni ters yone alinir - iki kanat birlikte acilip/kapanirken
// biri sikisirsa ikisi de geri doner. Tek kanat komutunda (birlikte=false,
// varsayilan) sadece o kanat etkilenir.
void kapiAcKomut(int i, bool birlikte = false);
void kapiKapatKomut(int i, bool birlikte = false);
void kapiDurdurKomut(int i);
void kapiPoll();
void zilButonPoll();

// GECICI TEST: R413D08'e Modbus fonksiyon 0x03 (durum oku, kanal 1-8) gonderir
// ve RS485 hattindan yanit gelip gelmedigini bekler - normal r413RoleYaz()'in
// aksine fire-and-forget DEGIL, gercekten cevap var mi kontrol eder. R413D08
// bagli/adresi dogruysa yanit doner, yoksa bos doner (2026-09-10 tezgah testi).
String r413DurumSorgula();

#endif
