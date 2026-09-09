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
};
extern BahceKapisi bahceKapi[2];

void kapiTumRoleleriKapat();
void kapiAcKomut(int i);
void kapiKapatKomut(int i);
void kapiDurdurKomut(int i);
void kapiPoll();
void zilButonPoll();

#endif
