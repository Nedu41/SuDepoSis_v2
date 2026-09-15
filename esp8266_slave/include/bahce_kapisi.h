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
  float akimPeakAmper = 0.0;  // bu hareketin en yuksek olculen amperi - esik ayari icin referans (2026-09-15 kullanici talebi)
  bool hataAsiriAkim = false;
  bool birlikte = false;  // bu hareket iki kanadin BIRLIKTE komutuyla mi baslatildi (bkz kapiAcKomut/kapiKapatKomut)
  bool durdurmaOnaylanamadi = false;  // R413D08 "kapat" komutunu dogrulamadi (modul kilitlenmis/yanit vermiyor olabilir) - bkz bahceRoleWatchdogPoll
  unsigned long sonDurdurmaDenemeMs = 0;
};
extern BahceKapisi bahceKapi[2];

// KOK NEDEN (2026-09-15 sahada bulundu - GUVENLIK ACIGI): R413D08 motor
// kalkis anindaki akim/gerilim darbesi veya RS485 hat cakismasiyla komut
// islemeyi KESIP kilitlenebiliyor - kapiMotorDurdur() "kapat" yazip 600ms
// icinde dogrulayamayinca (BAHCE_YON_DEGISTIRME_BEKLEME_MS) tek seferlik
// fire-and-forget fallback ile pes ediyordu, motor modul elle power-cycle
// edilene kadar (sahada 5+ dakika) enerjili kalabiliyordu. Artik dogrulama
// basarisiz olan her kanal icin bu watchdog loop()'ta surekli (pes etmeden)
// "kapat" komutunu tekrarlar - modul kendini toparlarsa (RS485 gecici
// tikanikligi) motor birkac saniye icinde kesilir; modul gercekten
// kilitliyse (power-cycle gerekiyorsa) bahceRoleSorunu bayragi web/RS485
// durumunda GORUNUR kalir, operator "basarili" sanip yanlis guvende
// hissetmez.
void bahceRoleWatchdogPoll();
extern bool bahceRoleSorunu;  // true = en az bir kapinin "kapat" komutu donanimdan dogrulanamadi (tekrar deneniyor)

// Kapi hareketsizken bile R413D08'in RS485/komut islemeye hala CEVAP VERIP
// VERMEDIGINI periyodik "durtme" (0x03 okuma, hicbir role degismez) ile
// kontrol eder - boylece modulun kilitlendigi ANCAK bir kapi komutu
// denendiginde degil, ONCEDEN fark edilir. Kapi hareket halindeyken zaten
// surekli okunuyor (bkz r413KanalDurumuOku cagrilari), o yuzden bu fonksiyon
// sadece IDLE durumda calisir - RS485/Modbus hattini gereksiz mesgul etmez.
void r413SaglikPoll();
extern bool r413ModulSagliksiz;  // true = R413D08 son saglik sorgusuna (idle iken) yanit vermedi

// Modbus fonksiyon 0x05 (Write Single Coil) - koilNo 0-tabanli kanal (0-7).
void r413RoleYaz(uint8_t koilNo, bool acik);

void kapiTumRoleleriKapat();
// kapiAcKomut/kapiKapatKomut'un GERCEK sonucu - eskiden ikisi de void'di ve
// /api/kapi/ac|kapat handler'lari icerikten bagimsiz hep "basarili" mesaji
// donuyordu. Kullanici bulgusu (2026-09-13): bazen "Kapat" basinca sayfa
// basarili dese de role hic tepki vermiyordu, 2. basista calisiyordu - kok
// neden, ic korumalardan biri (zaten o yonde hareket ediyor / zaten o
// pozisyonda) sessizce hicbir sey yapmadan donuyor ama handler yine de
// "Kapaniyor" diyordu. Artik cagiran taraf GERCEKTE ne oldugunu gorebiliyor.
enum KapiKomutSonuc { KAPI_KOMUT_BASLADI, KAPI_KOMUT_ZATEN_ORADA, KAPI_KOMUT_ZATEN_HAREKETTE };
// birlikte=true: asiri akimda SADECE bu kanat degil, DIGER kanat da (hareket
// halindeyse) ayni ters yone alinir - iki kanat birlikte acilip/kapanirken
// biri sikisirsa ikisi de geri doner. Tek kanat komutunda (birlikte=false,
// varsayilan) sadece o kanat etkilenir.
KapiKomutSonuc kapiAcKomut(int i, bool birlikte = false);
KapiKomutSonuc kapiKapatKomut(int i, bool birlikte = false);
void kapiDurdurKomut(int i);

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
extern bool bahceSwGetStatustan;           // true = acik sw/zil GET_STATUS'tan geliyor
bool bahceZilMandalliMi();                 // kisa basisin RS485 turuna yetismesi icin mandalli hal

// Zil butonunun yukselen kenarini isler (ding-dong + Kalburum'a aninda bildirim).
void bahceZilGuncelle(bool basili);

// GECICI TEST: R413D08'e Modbus fonksiyon 0x03 (durum oku, kanal 1-8) gonderir
// ve RS485 hattindan yanit gelip gelmedigini bekler - normal r413RoleYaz()'in
// aksine fire-and-forget DEGIL, gercekten cevap var mi kontrol eder. R413D08
// bagli/adresi dogruysa yanit doner, yoksa bos doner (2026-09-10 tezgah testi).
String r413DurumSorgula();

// main.cpp'deki struct Ayarlar icin kopru - web'den ayarlanabilir, kapi
// basina AYRI ACS712 kalibrasyonu (kapiIndex: 0=Kapi1, 1=Kapi2).
uint16_t bahceAkimSifirRawGetir(int kapiIndex);
float bahceAkimEsikAGetir(int kapiIndex);
unsigned long bahceMaxHareketMsGetir();  // web'den ayarlanabilir kapi hareket zaman asimi (2026-09-15 kullanici talebi)

#endif
