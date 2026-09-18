// ============ BAHÇE KAPISI (R413D08 Modbus RTU) + ZİL BUTONU ============
// R413D08, mevcut Sudepo<->Konteyner RS485 hattina (main.cpp'deki swSerial) 3.
// node olarak eklenir. Custom text protokolden farkli olarak burasi binary
// Modbus RTU cercevesi gonderir - yanit BEKLENMEZ (fire-and-forget), gercek
// sonuc limit switch/akim sensoruyle (Nano uzerinden) dogrulanir. Bkz
// config.h BAHCE_* tanimlari, proje hafizasi project_bahce_kapisi_motor_gelecek_ozellik.
#include <SoftwareSerial.h>
#include <math.h>
#include "config.h"
#include "bahce_kapisi.h"

extern SoftwareSerial swSerial;   // main.cpp - RS485 hatti (custom protokol + Modbus paylasimli)
extern bool bahceKapi1TamKapali, bahceKapi2TamKapali; // main.cpp - Nano D2/D3 tam-kapali limit switch'leri
extern bool nanoBaglantiVar;      // main.cpp - Nano ile seri haberlesme canli mi
void masterGonder();              // main.cpp - RS485 durum satirini Kalburum'a gonderir

const char* kapiDurumAdi(KapiDurum d) {
  switch (d) {
    case KAPI_KAPALI: return "kapali";
    case KAPI_ACIK: return "acik";
    case KAPI_KILIT_ACILIYOR: return "kilit_aciliyor";
    case KAPI_HAREKET_AC: return "aciliyor";
    case KAPI_HAREKET_KAPA: return "kapaniyor";
    case KAPI_NANO_BEKLENIYOR: return "nano_bekleniyor";
    default: return "hata";
  }
}

// Nano'dan PIN_READ_ALL ile toplu okunan bahce kapisi girisleri (bkz
// bahceNanoPoll). Limit switch'ler INPUT_PULLUP: basili/tetiklenmis = LOW.
bool bahceKapi1TamAcik = false, bahceKapi2TamAcik = false;
bool bahceZilBasili = false;
unsigned long bahceZilSonCalmaMs = 0;
unsigned long bahceSwSonBasariliMs = 0;  // 0 = Nano'dan hic gecerli okuma alinmadi
bool bahceKilitAktif = false;            // solenoid kilit KOMUT durumu (geri besleme sensoru yok)
bool bahceSwGetStatustan = false;         // true = acik sw/zil GET_STATUS'tan geliyor, PIN_READ_ALL gereksiz

// Zil basisinin RS485/web'e tasinan MANDALLI hali - ham "su an basili"
// degeri kisa basislarda 600ms'lik RS485 turuna denk gelmeyebiliyor.
bool bahceZilMandalliMi() {
  return bahceZilSonCalmaMs != 0 && (millis() - bahceZilSonCalmaMs < BAHCE_ZIL_MANDAL_MS);
}

// releKilit iki kapida da AYNI kanali (BAHCE_KILIT_RELE) gosterir - tek
// ortak solenoid kilit, kapi basina ayri kilit YOK (bkz config.h).
BahceKapisi bahceKapi[2] = {
  { KAPI_KAPALI, 0, 0, 0, BAHCE_KAPI1_RELE_A, BAHCE_KAPI1_RELE_B, BAHCE_KILIT_RELE, BAHCE_KAPI1_ACIK_PIN, BAHCE_KAPI1_AKIM_PIN, false },
  { KAPI_KAPALI, 0, 0, 0, BAHCE_KAPI2_RELE_A, BAHCE_KAPI2_RELE_B, BAHCE_KILIT_RELE, BAHCE_KAPI2_ACIK_PIN, BAHCE_KAPI2_AKIM_PIN, false }
};

uint16_t modbusCRC16(const uint8_t* buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
      else crc >>= 1;
    }
  }
  return crc;
}

// R413D08'e OZGU "Control command" cercevesi (fonksiyon 0x06) - standart
// Modbus "Write Single Coil" (0x05) DEGIL, R413D08 onu desteklemiyor.
// koilNo 0-tabanli kanal (0-7) alir, cerceve icinde 1-tabanli register'a
// (0x0001-0x0008) cevrilir. Komut kodu 0x01=Ac, 0x02=Kapat (0xFF/0x00
// standart Modbus coil kodlamasi DEGIL). 2026-09-10'da resmi komut
// dokumanindan (github.com/microrobotics/R413D08) dogrulandi - eskiden
// yanlislikla standart Write Single Coil formati kullaniliyordu, R413D08
// bu yuzden hicbir komuta tepki vermiyordu.
// 2026-09-15: fire-and-forget oldugu icin (yanit beklenmiyor) cerceve hatta
// (Kalburum'un ayni swSerial hattina yazdigi metin protokolüyle) çakışıp
// kaybolabiliyor - sahada DUR sonrasi bir kanadin dakikalarca enerjili
// kaldigi, sadece tekrar tekrar basinca "kendiliginden" duzeldigi gozlendi.
// Ayni cerceve kisa aralikla 3 kez gonderilerek tek seferlik kayip riski
// azaltilir - toplam maliyet birkaç ms, guvenlik-kritik (motor durdurma
// dahil) tum role yazimlarinda tek noktadan gecerli.
void r413RoleYaz(uint8_t koilNo, bool acik) {
  uint8_t frame[8];
  frame[0] = R413D08_MODBUS_ADRES;
  frame[1] = 0x06;
  frame[2] = 0x00; frame[3] = koilNo + 1;
  frame[4] = acik ? 0x01 : 0x02; frame[5] = 0x00;
  uint16_t crc = modbusCRC16(frame, 6);
  frame[6] = crc & 0xFF; frame[7] = (crc >> 8) & 0xFF;
  for (uint8_t tekrar = 0; tekrar < 3; tekrar++) {
    digitalWrite(RS485_DE_PIN, HIGH);
    delayMicroseconds(100);
    swSerial.write(frame, 8);
    delay(2);
    digitalWrite(RS485_DE_PIN, LOW);
    if (tekrar < 2) delay(3);
  }
}

// GECICI TEST (bkz bahce_kapisi.h): fire-and-forget DEGIL, gercekten yanit
// bekler. R413D08'in RS485/adres seviyesinde canli olup olmadigini rol
// modulunden BAGIMSIZ dogrulamak icin.
String r413DurumSorgula() {
  uint8_t frame[8];
  frame[0] = R413D08_MODBUS_ADRES;
  frame[1] = 0x03;
  frame[2] = 0x00; frame[3] = 0x01;  // baslangic register 0x0001 (kanal 1)
  frame[4] = 0x00; frame[5] = 0x08;  // 8 kanal oku
  uint16_t crc = modbusCRC16(frame, 6);
  frame[6] = crc & 0xFF; frame[7] = (crc >> 8) & 0xFF;

  while (swSerial.available()) swSerial.read();  // eski cop varsa temizle
  digitalWrite(RS485_DE_PIN, HIGH);
  delayMicroseconds(100);
  swSerial.write(frame, 8);
  swSerial.flush();
  delayMicroseconds(100);
  digitalWrite(RS485_DE_PIN, LOW);

  String hex = "";
  unsigned long t = millis();
  int n = 0;
  while (millis() - t < 300) {
    if (swSerial.available()) {
      int b = swSerial.read();
      if (b < 0x10) hex += "0";
      hex += String(b, HEX);
      hex += " ";
      n++;
      t = millis();  // her byte'ta zaman asimini yenile, hizli okusun
    }
    yield();
  }
  return hex;
}

// Nano'nun genel ANALOG_READ komutuna senkron (bloklayan) sarmalayici.
// Dijital karsiligi (PIN_READ) artik kullanilmiyor - tum dijital girisler
// tek seferde PIN_READ_ALL ile okunuyor (bkz bahceNanoPoll).
// 2026-09-15: 300ms -> 80ms. Nano donanim UART0 uzerinde normalde birkac ms
// icinde cevap veriyor; 300ms sadece "hic cevap yok" durumu icin asiri
// cömert bir sinirdi. Kapi hareket halindeyken bu fonksiyon her kapiPoll()
// turunda (250ms) cagrildigindan, uzun timeout ESP8266'nin loop()'unu
// bloke edip swSerial (Kalburum RS485) trafigini kacirmasina - Kalburum'un
// ACK bekleyip "kopma" gibi gorunmesine - yol aciyordu.
static int nanoAnalogOku(int pin) {
  while (Serial.available()) Serial.read();
  Serial.print("ANALOG_READ:"); Serial.println(pin);
  unsigned long t = millis(); String r = ""; bool ok = false;
  while (millis() - t < 80) {
    if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("ANALOG:") >= 0) { ok = true; break; } }
    yield();
  }
  if (!ok) return -1;
  int eq = r.indexOf('=');
  return eq >= 0 ? r.substring(eq + 1).toInt() : -1;
}

// "Tam kapali" limit switch'leri (Nano D2/D3) icin ayri bir PIN_READ sorgusu
// YOK - Nano bunlari zaten her GET_STATUS yanitinda bildiriyor, main.cpp
// orada bahceKapi1/2TamKapali'ya yaziyor. Ekstra Nano trafigi olmadan taze.
static bool kapiTamKapaliMi(int i) { return i == 0 ? bahceKapi1TamKapali : bahceKapi2TamKapali; }

static void kapiMotorDurdur(BahceKapisi& k) {
  r413RoleYaz(k.releA, false);
  r413RoleYaz(k.releB, false);
  k.akimAmper = 0.0;  // motor duruyor, gosterge "0A" gostersin - eski deger yaniltici olmasin
}

static void kilitYaz(BahceKapisi& k, bool aktif) {
  r413RoleYaz(k.releKilit, aktif);
  bahceKilitAktif = aktif;
}

void kapiTumRoleleriKapat() {
  for (int i = 0; i < 2; i++) {
    kapiMotorDurdur(bahceKapi[i]);
    kilitYaz(bahceKapi[i], false);
  }
}

static unsigned long gecikmeliKomutMs = 0;
static int gecikmeliKomutKapi = -1;

void kapiGecikmeliKomutIptal() { gecikmeliKomutMs = 0; gecikmeliKomutKapi = -1; }

// ============ IKI KANAT LADDER-MANTIK SEKANSI ============
// 2026-09-13'te sahada dogrulanmis, sonra birkac "tam geri alma" sirasinda
// kaybolmus tasarimin GERI PORTU (2026-09-17, kullanici: "çift kapıda da
// çalışan son sürüme geri al" - bkz proje hafizasi
// project_bahce_kapisi_ladder_mantik_cozuldu, commit 5152f17). 2026-09-17
// sabah yapilan ilk deneme (CiftAcilisSekansi) tek kapinin KILIT_ACILIYOR/
// kilitPulseBaslangicMs alanlarini PAYLASIYORDU (yaris durumu riski) VE
// - en onemlisi - sekans SURERKEN bir kanat HATA'ya (asiri akim/zaman
// asimi) duserse bunu HIC fark etmiyordu, digerini baslatmaya kor kor
// devam ediyordu. Asagidaki tasarim KENDI AYRI durum degiskenini kullanir,
// hicbir alani tek kapi (kapiAcKomut) ile PAYLASMAZ, ve her adimda HATA
// kontrolu yapip gerekirse sekansi hemen iptal eder.
enum IkiliAdim { IKILI_YOK, AC_ADIM_KILIT, AC_ADIM_KAPI2, AC_ADIM_KAPI1 };
static IkiliAdim ikiliAdim = IKILI_YOK;
static unsigned long ikiliAdimMs = 0;

static void ikiliSekansIptalEt() {
  if (ikiliAdim == IKILI_YOK) return;
  // KOSULSUZ birak - bahceKilitAktif bayragina GUVENME. R413D08 yazimlari
  // fire-and-forget (yanit beklenmez, bkz r413RoleYaz yorumu); bayrak "off"
  // dese bile gercek role KAYBOLAN BIR CERCEVE yuzunden hala enerjili
  // kalmis olabilir. Durdur+hemen-tekrar-baslat testinde gorulen "saçmalıyor"
  // sikayetinin olasi kaynaklarindan biri buydu (2026-09-17).
  kilitYaz(bahceKapi[0], false);
  ikiliAdim = IKILI_YOK;
}

static void ikiliSekansPoll() {
  if (ikiliAdim == IKILI_YOK) return;
  // Sekans surerken bir kanat HATA'ya dusmusse (asiri akim/zaman asimi)
  // hemen iptal et - ladder, hatali kanadin yaninda henuz baslamamis
  // digerini baslatmaya devam ETMEMELI.
  if (bahceKapi[0].durum == KAPI_HATA || bahceKapi[1].durum == KAPI_HATA) {
    ikiliSekansIptalEt();
    return;
  }
  unsigned long simdi = millis();
  if (simdi - ikiliAdimMs < BAHCE_KAPI_LADDER_ADIM_MS) return;
  switch (ikiliAdim) {
    case AC_ADIM_KILIT: {
      BahceKapisi& k2 = bahceKapi[1];
      r413RoleYaz(k2.releA, true);  // Role3 HIGH (Kapi2/SAG acma)
      k2.durum = KAPI_HAREKET_AC;
      k2.hataAsiriAkim = false;
      k2.akimTepeAmper = 0.0;
      k2.hareketBaslangicMs = simdi;
      ikiliAdim = AC_ADIM_KAPI2;
      ikiliAdimMs = simdi;
      break;
    }
    case AC_ADIM_KAPI2: {
      BahceKapisi& k1 = bahceKapi[0];
      r413RoleYaz(k1.releA, true);  // Role1 HIGH (Kapi1/SOL acma)
      k1.durum = KAPI_HAREKET_AC;
      k1.hataAsiriAkim = false;
      k1.akimTepeAmper = 0.0;
      k1.hareketBaslangicMs = simdi;
      ikiliAdim = AC_ADIM_KAPI1;
      ikiliAdimMs = simdi;
      break;
    }
    case AC_ADIM_KAPI1:
      kilitYaz(bahceKapi[0], false);  // Role5 LOW
      ikiliAdim = IKILI_YOK;
      break;
    default: break;
  }
}

// ACILIS: Role5(kilit) HIGH -> 1sn -> Role3(Kapi2/SAG acma) HIGH -> 1sn ->
// Role1(Kapi1/SOL acma) HIGH -> 1sn -> Role5(kilit) LOW.
void kapiCiftKanatAc() {
  if (ikiliAdim != IKILI_YOK) return;
  BahceKapisi& k1 = bahceKapi[0];
  BahceKapisi& k2 = bahceKapi[1];
  if (k1.durum == KAPI_HAREKET_AC || k2.durum == KAPI_HAREKET_AC) return;
  // KRITIK: bekleyen bir "kapi2'yi gecikmeli kapat" zamanlayicisi varsa
  // (kapat_cift'ten hemen sonra ac_cift'e basilmis olabilir) iptal et -
  // yoksa bu eski komut birkac saniye sonra, tamamen alakasiz bir anda,
  // acilmakta olan kapi2'yi sessizce kapatmaya calisirdi (2026-09-17).
  kapiGecikmeliKomutIptal();
  kapiMotorDurdur(k1);
  kapiMotorDurdur(k2);
  // birlikte=true: bir kanat HATA verirse (bkz kapiPoll) diger kanat da
  // (henuz hareket etmemis olsa bile) ayni ters-yon/durdurma kapsamina girer.
  k1.birlikte = true;
  k2.birlikte = true;
  // Durdur+hemen-tekrar-baslat testinde sekans bazen saçmalıyordu (2026-09-17
  // sahada goruldu). R413D08 yazimlari fire-and-forget - bir onceki DUR
  // komutunun kilit-birakma cercevesi kaybolmus olabilir, bayrak "off" dese
  // bile gercek role hala enerjili kalmis olabilir. Yeni sekans baslamadan
  // once kilidi ACIKCA once KAPAT sonra AC - boylece kayip bir onceki
  // cerceveye guvenmek yerine bilinen-temiz bir baslangic garanti edilir.
  kilitYaz(k1, false);
  kilitYaz(k1, true);  // Role5 HIGH (ortak kilit)
  ikiliAdim = AC_ADIM_KILIT;
  ikiliAdimMs = millis();
}

// KAPANIS: Role2(Kapi1/SOL kapama) HIGH -> 2sn -> Role4(Kapi2/SAG kapama)
// HIGH. "Kapanista ters sira" (bindirmeli kapi kurali) burada GECERLI
// DEGIL - Kapi1(SOL) her iki yonde de HEMEN baslar, Kapi2(SAG) her iki
// yonde de gecikmeli kanattir (2026-09-13 sahada dogrulandi).
// KAPANIS: Role2(Kapi1/SOL kapama) HIGH -> 2sn -> Role4(Kapi2/SAG kapama)
// HIGH. "Kapanista ters sira" (bindirmeli kapi kurali) burada GECERLI
// DEGIL - Kapi1(SOL) her iki yonde de HEMEN baslar, Kapi2(SAG) her iki
// yonde de gecikmeli kanattir (2026-09-13 sahada dogrulandi).
void kapiCiftKanatKapat() {
  kapiKapatKomut(0, true);
  gecikmeliKomutKapi = 1;
  gecikmeliKomutMs = millis() + BAHCE_KANAT_GECIKME_KAPA_MS;
}

// Nano'dan gelen limit switch verisi taze mi (BAHCE_SW_TAZELIK_MS icinde)?
// "Zaten hedef konumdaysa dokunma" kontrolu icin - bkz kapiAcKomut/kapiKapatKomut.
static bool bahceSwTazeMi() {
  return (bahceSwSonBasariliMs != 0) && (millis() - bahceSwSonBasariliMs < BAHCE_SW_TAZELIK_MS);
}

// 2026-09-17 kullanici tarifi (kesinlesmis hali - "once r5, 1sn sonra r1"):
// kilit (R5) ONCE cekilir, motor 1sn SONRA (kapiPoll'daki KAPI_KILIT_ACILIYOR
// bekleme adiminda) baslar. R5, motordan tamamen bagimsiz kendi 3sn'lik
// sayacinin sonunda (ayni kilitPulseBaslangicMs referansiyla, ayri bir
// kontrolde) birakilir - motor durumundan etkilenmez.
void kapiAcKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_AC || k.durum == KAPI_KILIT_ACILIYOR) return;
  // Kullanici bulgusu (2026-09-17): kapi zaten HATA'dan (ör. zaman asimi)
  // gelmis olsa bile gercekte hedef konumdaysa, "Ac" tekrar basildiginda
  // hicbir seye dokunmadan sadece durumu senkronize et - motoru/kilidi
  // tekrar sursun, kullanici home pozisyondayken HATA'da takili kalmasin.
  bool acikLimit = (i == 0) ? bahceKapi1TamAcik : bahceKapi2TamAcik;
  if (bahceSwTazeMi() && acikLimit) { k.durum = KAPI_ACIK; return; }
  ikiliSekansIptalEt();  // cift sekans surerken tek kapi komutu gelirse once onu iptal et - R5 kontrolu ayni anda iki yerden yapilmasin
  kapiMotorDurdur(k);  // ters yonden (kapaniyor) gelinmis olabilir - once motoru kes
  kilitYaz(k, true);
  k.kilitPulseBaslangicMs = millis();  // R5 bu andan 3sn sonra birakilir, motor 1sn sonra baslar (kapiPoll)
  k.hataAsiriAkim = false;
  k.akimTepeAmper = 0.0;  // yeni hareket - onceki tepe deger sifirlanir
  k.birlikte = birlikte;
  k.durum = KAPI_KILIT_ACILIYOR;  // motor HENUZ baslamadi - kapiPoll 1sn sonra baslatacak
}

void kapiKapatKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_KAPA) return;
  // bkz kapiAcKomut ayni gerekce - zaten kapaliysa (HATA'dan gelinmis olsa
  // bile) tekrar suruculemesin, sadece durum senkronize edilsin.
  if (nanoBaglantiVar && kapiTamKapaliMi(i)) { k.durum = KAPI_KAPALI; return; }
  ikiliSekansIptalEt();  // cift acilis sekansi surerken tek kapi kapatma gelirse once onu iptal et
  kapiMotorDurdur(k);
  r413RoleYaz(k.releA, false);
  r413RoleYaz(k.releB, true);
  k.hareketBaslangicMs = millis();
  k.hataAsiriAkim = false;
  k.akimTepeAmper = 0.0;  // yeni hareket - onceki tepe deger sifirlanir
  k.birlikte = birlikte;
  k.durum = KAPI_HAREKET_KAPA;
}

// Motoru dogrudan verilen yone alir (kilit-darbe sekansi YOK) - sadece
// zaten hareket halindeki bir kanadi aninda ters yone almak icin (bkz
// kapiPoll asiri akim guvenligi).
static void kapiYoneAyarla(BahceKapisi& k, KapiDurum yon, unsigned long now) {
  if (yon == KAPI_HAREKET_AC) { r413RoleYaz(k.releB, false); r413RoleYaz(k.releA, true); }
  else { r413RoleYaz(k.releA, false); r413RoleYaz(k.releB, true); }
  k.hareketBaslangicMs = now;
  k.durum = yon;
}

void kapiDurdurKomut(int i) {
  // Henuz baslamamis gecikmeli kanat komutu varsa onu da iptal et - yoksa
  // "Dur" dedikten saniyeler sonra diger kanat kendi kendine hareket ederdi.
  kapiGecikmeliKomutIptal();
  ikiliSekansIptalEt();
  BahceKapisi& k = bahceKapi[i];
  // Sadece gercekten hareket/bekleme halindeyse dokun - BAHCE_KAPI_DUR komutu
  // iki kanada birden gider, hareketsiz (zaten kapali/acik) kanadin durumunu
  // yanlislikla HATA'ya cekmesin.
  if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA && k.durum != KAPI_KILIT_ACILIYOR && k.durum != KAPI_NANO_BEKLENIYOR) return;
  kapiMotorDurdur(k);
  kilitYaz(k, false);
  k.kilitPulseBaslangicMs = 0;
  k.durum = KAPI_HATA;
}

void kapiPoll() {
  unsigned long now = millis();

  ikiliSekansPoll();
  // AYNI TASMA RISKI (bkz asagidaki gecikmeli-komut bloguyla ilgili yorum):
  // ikiliSekansPoll() adim atarken k.hareketBaslangicMs'i KENDI taze
  // millis()'iyle set edebilir - bu, yukaridaki "now"dan (mikrosaniyeler de
  // olsa) daha GEC bir zaman olabilir. Asagidaki for donguso "now"u hemen
  // kullanacagindan, tazelemeden devam etmek ayni unsigned wraparound riskini
  // tasir. Ucuz oldugu icin her zaman tazeleniyor.
  now = millis();

  // Bekleyen ikinci kanat KAPANIS komutu zamani geldiyse baslat (kanat gecikmesi).
  if (gecikmeliKomutMs != 0 && (long)(now - gecikmeliKomutMs) >= 0) {
    int k = gecikmeliKomutKapi;
    kapiGecikmeliKomutIptal();
    if (k >= 0) kapiKapatKomut(k, true);
    // KRITIK BUG (2026-09-17, sahada uzun sure arandi): kapiKapatKomut()
    // icinde k.hareketBaslangicMs YENI bir millis() cagrisiyla set ediliyor -
    // bu deger, fonksiyonun en basinda okunan "now"dan birkac mikrosaniye
    // SONRAKI bir zamandir. Asagidaki for donguson AYNI turda, hala ESKI
    // "now" ile zamanAsimi hesapliyordu: (now - hareketBaslangicMs) negatif
    // cikip unsigned long tasmasindan (wraparound) DEV bir sayiya donusuyor,
    // "zaman asimi" ANINDA true oluyor, motor o an baslar baslamaz kesiliyor.
    // Bu SADECE gecikmeli tetiklenen kapida oluyordu (dogrudan HTTP'den
    // gelen anlik komutlarda hareketBaslangicMs, bu "now" okumasindan cok
    // ONCE, ayri bir kapiPoll() turunda set ediliyordu - tasma riski yoktu).
    // "R4 anlik on-off" sikayetinin GERCEK kok nedeni buydu. now'u taze
    // okuyarak duzeltiliyor.
    now = millis();
  }

  for (int i = 0; i < 2; i++) {
    BahceKapisi& k = bahceKapi[i];
    // Kilit (R5) motordan BAGIMSIZ kendi 3sn sayaciyla birakilir - ayni
    // kilitPulseBaslangicMs referansi asagidaki motor-baslatma kontrolunde
    // de kullanilir, ikisi de R5'in ilk tetiklendigi ana gore hesaplanir.
    if (k.kilitPulseBaslangicMs != 0 && now - k.kilitPulseBaslangicMs >= BAHCE_KILIT_PULSE_MS) {
      kilitYaz(k, false);
      k.kilitPulseBaslangicMs = 0;
    }
    // Motor, R5'ten 1sn SONRA baslar (bkz kapiAcKomut yorumu) - ayni
    // kilitPulseBaslangicMs referansi (R5'in tetiklendigi an) kullanilir.
    if (k.durum == KAPI_KILIT_ACILIYOR) {
      if (now - k.kilitPulseBaslangicMs >= BAHCE_ACILIS_MOTOR_GECIKME_MS) {
        r413RoleYaz(k.releB, false);
        r413RoleYaz(k.releA, true);
        k.hareketBaslangicMs = now;  // GERCEK motor baslangici - asiri akim baslangic payi buradan sayilir
        k.durum = KAPI_HAREKET_AC;
      }
      continue;
    }
    // Nano kopma guvenligi (2026-09-17 kullanici talebi): motor hareket
    // halindeyken Nano ile iletisim koparsa (limit switch/akim verisi artik
    // guvenilmez) 20sn'lik zaman asimini BEKLEMEDEN motoru hemen durdur.
    // Kisa surede (BAHCE_NANO_KOPMA_TOLERANS_MS) baglanti donerse kaldigi
    // yonde devam eder, donmezse KAPI_HATA'ya duser.
    if ((k.durum == KAPI_HAREKET_AC || k.durum == KAPI_HAREKET_KAPA) && !nanoBaglantiVar) {
      k.nanoKopmaYonu = k.durum;
      k.nanoKopmaBaslangicMs = now;
      kapiMotorDurdur(k);
      k.durum = KAPI_NANO_BEKLENIYOR;
      DEBUG_PRINTF("[KAPI%d] Nano baglantisi koptu, motor durduruldu\n", i + 1);
      continue;
    }
    if (k.durum == KAPI_NANO_BEKLENIYOR) {
      if (nanoBaglantiVar) {
        if (k.nanoKopmaYonu == KAPI_HAREKET_AC) { r413RoleYaz(k.releB, false); r413RoleYaz(k.releA, true); }
        else { r413RoleYaz(k.releA, false); r413RoleYaz(k.releB, true); }
        k.hareketBaslangicMs = now;  // guvenlik icin taze zaman asimi penceresi
        k.durum = k.nanoKopmaYonu;
        DEBUG_PRINTF("[KAPI%d] Nano baglantisi geri geldi, %s yonunde devam\n", i + 1, kapiDurumAdi(k.durum));
      } else if (now - k.nanoKopmaBaslangicMs >= BAHCE_NANO_KOPMA_TOLERANS_MS) {
        k.durum = KAPI_HATA;
        DEBUG_PRINTF("[KAPI%d] HATA: Nano baglantisi zamaninda donmedi\n", i + 1);
      }
      continue;
    }
    if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA && k.durum != KAPI_HATA) {
      // Boot/reboot sonrasi (veya hic komut verilmemisken) durum hep
      // varsayilan KAPI_KAPALI ile baslar - gercek switch konumuyla HIC
      // senkronize edilmiyordu ("kapi fiziksel acik ama ekranda kapali
      // yaziyor" sikayeti, 2026-09-17, OTA flas sonrasi reboot ile
      // fark edildi). Hareket halinde DEGILKEN taze switch verisi varsa
      // durumu gercek konuma gore duzelt - relay'e dokunmaz, salt takip.
      // KAPI_HATA'ya BILEREK dokunulmuyor - kullanici sorunu gorup mudahale
      // edene kadar ekranda kalmali (2026-09-17: "sorun cozulunceye kadar
      // ekranda kalsin" - eskiden bu blok HATA'yi da "duzeltip" gizliyordu).
      bool acikOkIdle = (bahceSwSonBasariliMs != 0) && (now - bahceSwSonBasariliMs < BAHCE_SW_TAZELIK_MS);
      bool acikLimitIdle = (i == 0) ? bahceKapi1TamAcik : bahceKapi2TamAcik;
      bool kapaliLimitIdle = nanoBaglantiVar && kapiTamKapaliMi(i);
      if (acikOkIdle && acikLimitIdle) k.durum = KAPI_ACIK;
      else if (kapaliLimitIdle) k.durum = KAPI_KAPALI;
      continue;
    }
    // KAPI_HATA: motor zaten durduruldu, asagidaki aktif-hareket bloguna
    // (asiri akim/zaman asimi kontrolu) HIC girmesin - stray bir akim
    // okumasi durumu yanlislikla HAREKET_AC/KAPA'ya geri dondurmesin.
    // Kullanicinin yeni bir Ac/Kapat komutu vermesi gerekir.
    if (k.durum == KAPI_HATA) continue;
    if (now - k.sonPollMs < BAHCE_POLL_ARALIK_MS) continue;
    k.sonPollMs = now;

    // Limit switch'ler icin burada AYRI sorgu YOK - "tam acik" bahceNanoPoll'un
    // PIN_READ_ALL'undan, "tam kapali" GET_STATUS'tan taze geliyor. Veri
    // bayatsa (Nano yanit vermiyorsa) limit'e gore karar VERILMEZ, hareket
    // yalnizca zaman asimi/asiri akim ile biter.
    bool acikOk = (bahceSwSonBasariliMs != 0) && (now - bahceSwSonBasariliMs < BAHCE_SW_TAZELIK_MS);
    bool acikLimit = (i == 0) ? bahceKapi1TamAcik : bahceKapi2TamAcik;
    bool kapaliLimit = nanoBaglantiVar && kapiTamKapaliMi(i);
    int akimRaw = nanoAnalogOku(k.akimPin);
    float akimAmper = (akimRaw >= 0) ? ((akimRaw - bahceAkimSifirRawGetir(i)) * (5000.0 / 1024.0)) / ACS712_MV_PER_AMP : 0.0;
    if (akimRaw >= 0) {
      k.akimAmper = fabs(akimAmper);  // web/RS485'e tasinan canli deger
      if (k.akimAmper > k.akimTepeAmper) k.akimTepeAmper = k.akimAmper;  // bu hareketteki en yuksek deger
    }

    bool zamanAsimi = (now - k.hareketBaslangicMs) > bahceMaxHareketMsGetir();
    bool baslangicPayindaMi = (now - k.hareketBaslangicMs) < BAHCE_ASIRI_AKIM_BASLANGIC_PAYI_MS;
    bool asiriAkim = !baslangicPayindaMi && akimRaw >= 0 && fabs(akimAmper) > bahceAkimEsikAGetir(i);

    if (k.durum == KAPI_HAREKET_AC && acikOk && acikLimit) {
      kapiMotorDurdur(k);
      k.hataAsiriAkim = false;  // basariyla acildi - eski asiri akim bayragi kalici kalmasin
      k.durum = KAPI_ACIK;
    } else if (k.durum == KAPI_HAREKET_KAPA && kapaliLimit) {
      kapiMotorDurdur(k);
      k.hataAsiriAkim = false;  // basariyla kapandi - eski asiri akim bayragi kalici kalmasin
      k.durum = KAPI_KAPALI;
    } else if (asiriAkim) {
      kapiMotorDurdur(k);
      if (k.hataAsiriAkim) {
        // Ters yone alindiktan SONRA da asiri akim geldi - gercek bir
        // tikanma/mekanik ariza (guvenlik icin ters-ileri ping-pong YAPMA),
        // motoru durdurup HATA'ya gec, kullanicinin mudahalesini bekle.
        k.durum = KAPI_HATA;
        DEBUG_PRINTF("[KAPI%d] HATA: ters yon sonrasi da asiri akim\n", i + 1);
      } else {
        // Guvenlik: arac/insan sikismasi ihtimaline karsi motoru DURDURMAK
        // yetmez, ters yone hareket ettirilir (UL 325 engel-geri-donus
        // mantigi). kapiAcKomut/kapiKapatKomut'daki kilit-darbe gecikmesi
        // BILEREK atlanir - kilit zaten hareket sirasinda acik, gecikme
        // sikismis nesneyi ters yone almayi geciktirir.
        k.hataAsiriAkim = true;
        KapiDurum tersYon = (k.durum == KAPI_HAREKET_KAPA) ? KAPI_HAREKET_AC : KAPI_HAREKET_KAPA;
        kapiYoneAyarla(k, tersYon, now);
        DEBUG_PRINTF("[KAPI%d] ASIRI AKIM - ters yone aliniyor (%s)\n", i + 1, kapiDurumAdi(tersYon));

        // Kullanici talebi: iki kanat BIRLIKTE komutuyla hareket ediyorsa
        // (cift basis/BAHCE_KAPI_AC/KAPAT), biri sikisinca SADECE o kanat
        // degil DIGER kanat da ayni ters yone alinir - orn. kapanirken
        // birini elle tutup durdurursan ikisi de geri acilir. Tek kanat
        // komutunda (birlikte=false) sadece bu kanat etkilenir.
        if (k.birlikte) {
          BahceKapisi& diger = bahceKapi[1 - i];
          if ((diger.durum == KAPI_HAREKET_AC || diger.durum == KAPI_HAREKET_KAPA) && !diger.hataAsiriAkim) {
            diger.hataAsiriAkim = true;
            kapiYoneAyarla(diger, tersYon, now);
            DEBUG_PRINTF("[KAPI%d] eslesmis kanat da ters yone aliniyor (%s)\n", 2 - i, kapiDurumAdi(tersYon));
          }
        }
      }
    } else if (zamanAsimi) {
      kapiMotorDurdur(k);
      k.durum = KAPI_HATA;
      DEBUG_PRINTF("[KAPI%d] HATA: zaman asimi\n", i + 1);
    }
  }
}

// ============ BAHCE KAPISI GIRIS POLL'U (limit switch'ler + zil) ============
// Nano'nun PIN_READ_ALL komutu D2-D13'u TEK turda dondurur - eskiden burada
// sadece zil butonu icin ayri bir PIN_READ yapiliyordu, ayni tur sayisiyla
// artik "tam acik" limit switch'leri de okunuyor (ek Nano trafigi YOK).
// Ayni gerekce GET_STATUS'a PIR eklenirken de gecerliydi: tek istekte tum
// durum, art arda komutlarin yol actigi zamanlama sorunlari olmadan.
// Kapi hareket halindeyken daha sik okunur (limit switch'i gec gorup motoru
// fazla surmemek icin).
static int pinDegerAyikla(const String& r, int pin) {
  int idx = r.indexOf("," + String(pin) + "=");
  if (idx < 0) return -1;
  int esit = r.indexOf('=', idx);
  return (esit >= 0) ? r.substring(esit + 1, esit + 2).toInt() : -1;
}

void bahceNanoPoll() {
  static unsigned long sonPollMs = 0;
  static bool oncekiBasili = false;
  unsigned long now = millis();
  bool hareketVar = false;
  for (int i = 0; i < 2; i++) {
    KapiDurum d = bahceKapi[i].durum;
    if (d == KAPI_HAREKET_AC || d == KAPI_HAREKET_KAPA || d == KAPI_KILIT_ACILIYOR) hareketVar = true;
  }
  // Guncel Nano firmware'i bu verileri GET_STATUS icinde gonderiyor - o zaman
  // bu ek istek HIC yapilmaz (hattin ~%22'si serbest kalir, veri de 400ms
  // yerine 300ms tazelikte olur). Eski firmware'de asagisi calismaya devam eder.
  if (bahceSwGetStatustan) return;
  if (now - sonPollMs < (hareketVar ? BAHCE_POLL_ARALIK_MS : BAHCE_ZIL_POLL_ARALIK_MS)) return;
  sonPollMs = now;

  while (Serial.available()) Serial.read();
  Serial.println("PIN_READ_ALL");
  unsigned long t = millis(); String r = ""; bool okundu = false;
  while (millis() - t < 300) {
    if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("PIN:") >= 0) { okundu = true; break; } }
    yield();
  }
  if (!okundu) return;  // Nano yanit vermediyse eski degerleri KORU, oncekiBasili DEGISTIRME

  int a1 = pinDegerAyikla(r, BAHCE_KAPI1_ACIK_PIN);
  int a2 = pinDegerAyikla(r, BAHCE_KAPI2_ACIK_PIN);
  int z  = pinDegerAyikla(r, BAHCE_ZIL_BUTON_PIN);
  if (a1 < 0 || a2 < 0 || z < 0) return;  // yanit bozuk/eksik - yine eski degerler gecerli
  bahceKapi1TamAcik = (a1 == 0);  // INPUT_PULLUP: tetiklenince LOW
  bahceKapi2TamAcik = (a2 == 0);
  bahceSwSonBasariliMs = now;

  bahceZilGuncelle(z == 0);
}

// Zil butonunun yukselen kenarini isler - hem GET_STATUS hem (eski firmware'de)
// PIN_READ_ALL yolundan cagrilir, mantik tek yerde kalsin diye.
void bahceZilGuncelle(bool basili) {
  static bool oncekiBasili = false;
  unsigned long now = millis();
  bahceZilBasili = basili;
  if (basili && !oncekiBasili) {
    bahceZilSonCalmaMs = now;
    // Kalburum'un zili poll sirasini beklemesin - durumu ANINDA gonder.
    // Zil nadir bir olay oldugu icin bu ek gonderim hatti yormaz.
    masterGonder();
    DEBUG_PRINTLN("[ZIL] basildi, ding-dong calinacak");
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON1_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    unsigned long tz = millis();
    while (millis() - tz < 300) { if (Serial.available()) { String ra = Serial.readStringUntil('\n'); if (ra.indexOf("ACK") >= 0) break; } yield(); }
    delay(BAHCE_ZIL_TON_SURE_MS + 30);
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON2_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    tz = millis();
    while (millis() - tz < 300) { if (Serial.available()) { String ra = Serial.readStringUntil('\n'); if (ra.indexOf("ACK") >= 0) break; } yield(); }
  }
  oncekiBasili = basili;
}
