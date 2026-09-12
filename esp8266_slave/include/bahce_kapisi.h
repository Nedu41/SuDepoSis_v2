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
  float akimAmper = 0.0;  // son olculen deger (kapiPoll'da sadece hareket halindeyken guncellenir), RS485/web'e tasinir
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

// Iki kanadi SIRALI baslatir - bindirmeli kanatlar ayni anda hareket edemez
// (ticari kapi otomasyon kartlarindaki "leaf delay / phase shift" kurali).
// Acilista ust kanat once, kapanista en son; gecikme config.h'de.
void kapiCiftKanatAc();
void kapiCiftKanatKapat();
void kapiGecikmeliKomutIptal();
void kapiPoll();
// Nano'nun tum dijital girislerini TEK PIN_READ_ALL turunda okur: bahce
// kapisi "tam acik" limit switch'leri + zil butonu (zil basilinca Nano
// buzzer'inda ding-dong calar). Eskiden sadece zil icin ayri sorgu yapiliyordu.
void bahceNanoPoll();

// bahceNanoPoll'un doldurdugu anlik giris durumlari + kilit KOMUT durumu.
// Limit switch'ler INPUT_PULLUP (tetiklenince LOW), burada true = tetiklenmis.
extern bool bahceKapi1TamAcik, bahceKapi2TamAcik;
extern bool bahceZilBasili;
extern unsigned long bahceZilSonCalmaMs;   // son ding-dong ani (0 = hic calmadi)
extern unsigned long bahceSwSonBasariliMs; // son basarili PIN_READ_ALL (0 = hic)
extern bool bahceKilitAktif;               // solenoid kilit enerjili mi (komut, geri besleme YOK)

// GECICI TEST: R413D08'e Modbus fonksiyon 0x03 (durum oku, kanal 1-8) gonderir
// ve RS485 hattindan yanit gelip gelmedigini bekler - normal r413RoleYaz()'in
// aksine fire-and-forget DEGIL, gercekten cevap var mi kontrol eder. R413D08
// bagli/adresi dogruysa yanit doner, yoksa bos doner (2026-09-10 tezgah testi).
String r413DurumSorgula();

// main.cpp'deki struct Ayarlar icin kopru - web'den ayarlanabilir, kapi
// basina AYRI ACS712 kalibrasyonu (kapiIndex: 0=Kapi1, 1=Kapi2).
uint16_t bahceAkimSifirRawGetir(int kapiIndex);
float bahceAkimEsikAGetir(int kapiIndex);

#endif
