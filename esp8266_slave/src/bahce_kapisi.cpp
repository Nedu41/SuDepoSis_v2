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
void rs485KomutDinle();           // main.cpp - Kalburum'dan gelen RS485 komutlarini dinler/yanitlar

const char* kapiDurumAdi(KapiDurum d) {
  switch (d) {
    case KAPI_KAPALI: return "kapali";
    case KAPI_ACIK: return "acik";
    case KAPI_KILIT_ACILIYOR: return "kilit_aciliyor";
    case KAPI_HAREKET_AC: return "aciliyor";
    case KAPI_HAREKET_KAPA: return "kapaniyor";
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
// KOK NEDEN (2026-09-12 sahada bulundu, elle yazma testiyle dogrulandi):
// kapiKapatKomut/kapiAcKomut gibi cagiranlar art arda BIRDEN FAZLA
// r413RoleYaz() cagirir (once motoru durdur = 2 cerceve, sonra yeni yonu ac
// = 1-2 cerceve daha). Eskiden cerceveler arasinda HICBIR bosluk yoktu -
// fonksiyon DE_PIN'i LOW yapar yapmaz doner, bir sonraki cagri aninda yeni
// cerceveyi baslatiyordu. Modbus RTU, bir cercevenin bittigini/yenisinin
// basladigini SADECE araya giren sessizlikten (>=3.5 karakter suresi,
// 9600 baud'da ~3.6ms) anlar - bu bosluk olmayinca R413D08 art arda gelen
// cerceveleri TEK BOZUK BLOK sanip SESSIZCE reddediyordu (fire-and-forget
// oldugundan hata da donmuyor). Tek basina gonderilen bir yazim (test
// endpoint'i /rs485/r413_write_test) bu yuzden HEP calisiyordu, ama gercek
// kapi komutlarindaki 3-4 ardisik cerceve cogunlukla kaybolabiliyordu -
// "komut basarili" diyor ama role hic tepki vermiyordu. Cozum: her
// cagridan SONRA (fonksiyon donmeden once) yeterli sessizlik birakiliyor,
// boylece TUM cagiranlar (tek tek degistirmeye gerek kalmadan) korunuyor.
void r413RoleYaz(uint8_t koilNo, bool acik) {
  uint8_t frame[8];
  frame[0] = R413D08_MODBUS_ADRES;
  frame[1] = 0x06;
  frame[2] = 0x00; frame[3] = koilNo + 1;
  frame[4] = acik ? 0x01 : 0x02; frame[5] = 0x00;
  uint16_t crc = modbusCRC16(frame, 6);
  frame[6] = crc & 0xFF; frame[7] = (crc >> 8) & 0xFF;
  digitalWrite(RS485_DE_PIN, HIGH);
  delayMicroseconds(100);
  swSerial.write(frame, 8);
  delay(2);
  digitalWrite(RS485_DE_PIN, LOW);
  delay(5);  // Modbus inter-frame sessizligi (>=3.5 karakter, ~3.6ms @9600) icin pay
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

// Tek bir kanalin GERCEK anlik durumunu R413D08'den okur (fire-and-forget
// DEGIL, function 0x03 READ) - -1 = yanit yok/hata, 0 = kapali, 1 = acik.
// Yon degistirmeden once "eski role gercekten birakti mi" diye SADECE
// zamanlamaya degil, donanimin kendi cevabina guvenmek icin (2026-09-12,
// ikinci sigorta atmasi sonrasi eklendi - salt sabit sureli bekleme
// yetersiz kaldi, gercek donanimsal onay gerekti).
static int r413KanalDurumuOku(uint8_t koilNo) {
  uint8_t frame[8];
  frame[0] = R413D08_MODBUS_ADRES;
  frame[1] = 0x03;
  frame[2] = 0x00; frame[3] = koilNo + 1;
  frame[4] = 0x00; frame[5] = 0x01;  // 1 kanal oku
  uint16_t crc = modbusCRC16(frame, 6);
  frame[6] = crc & 0xFF; frame[7] = (crc >> 8) & 0xFF;

  while (swSerial.available()) swSerial.read();
  digitalWrite(RS485_DE_PIN, HIGH);
  delayMicroseconds(100);
  swSerial.write(frame, 8);
  swSerial.flush();
  delayMicroseconds(100);
  digitalWrite(RS485_DE_PIN, LOW);

  uint8_t buf[16];
  int n = 0;
  unsigned long t = millis();
  while (millis() - t < 100 && n < (int)sizeof(buf)) {
    if (swSerial.available()) { buf[n++] = swSerial.read(); t = millis(); }
    yield();
  }
  delay(5);  // bkz r413RoleYaz - Modbus inter-frame sessizligi
  // Beklenen yanit: addr,0x03,byteCount(=2),hi,lo,crcLo,crcHi (7 byte)
  if (n < 5 || buf[0] != R413D08_MODBUS_ADRES || buf[1] != 0x03) return -1;
  return (buf[3] != 0 || buf[4] != 0) ? 1 : 0;
}

// Yeni yon rolesini acmadan once ESKI yonun rolesinin GERCEKTEN (R413D08'den
// okunarak) birakip birakmadigini dogrular - sabit sureli bekleme yerine
// donanimsal onay, ama R413D08 hic yanit vermezse (fire-and-forget fallback)
// BAHCE_YON_DEGISTIRME_BEKLEME_MS sonunda yine de devam eder, sonsuza kadar
// beklemez. Bekleme sirasinda RS485 istekleri (Kalburum GET_STATUS) de
// servis edilmeye devam eder.
// ONEMLI (2026-09-13 sahada bulundu - KOK NEDEN, coklu tutarsiz davranisin
// asil sebebi): burada rs485KomutDinle() cagirmak CIDDI bir reentrancy
// tehlikesi yaratiyordu - Kalburum'dan TAM bu bekleme sirasinda yeni bir
// BAHCE_KAPI_AC/KAPAT/DUR komutu gelirse, kapiPoll()'un SU AN islemekte
// oldugu AYNI kapi (BahceKapisi&) uzerinde kapiAcKomut/kapiKapatKomut/
// kapiDurdurKomut IC ICE (reentrant) tekrar calisiyordu - dis cagrinin
// zaten okumus oldugu "durum"/limit degiskenleri bayatlasip reentrant
// cagrinin yaptigi degisikligin ustune yaziliyordu. Bu, "bazen sadece sol
// calisiyor, bazen hicbiri, bazen anlik cekip birakiyor" gibi TUTARSIZ ve
// zamanlamaya bagli davranisin asil kaynagiydi - her testte RS485 trafiginin
// TAM o milisaniyede gelip gelmemesine gore sonuc degisiyordu. Cozum:
// burada RS485 komutlarini SERVIS ETME - bekleme kisa tutuluyor
// (BAHCE_YON_DEGISTIRME_BEKLEME_MS), Kalburum zaten 600ms'de bir tekrar
// soracagi icin tek bir kacan tur zararsiz/kendi kendini toparlar.
static void eskiYonBirakmasiniBekle(uint8_t eskiRoleKoilNo) {
  unsigned long baslangic = millis();
  while (millis() - baslangic < BAHCE_YON_DEGISTIRME_BEKLEME_MS) {
    int durum = r413KanalDurumuOku(eskiRoleKoilNo);
    if (durum == 0) return;  // R413D08 dogruladi: role gercekten kapali
    yield();
  }
  // Zaman asimi/yanit alinamadi - fire-and-forget fallback, elimizden gelen buydu
}

// Nano'nun genel ANALOG_READ komutuna senkron (bloklayan) sarmalayici.
// Dijital karsiligi (PIN_READ) artik kullanilmiyor - tum dijital girisler
// tek seferde PIN_READ_ALL ile okunuyor (bkz bahceNanoPoll).
// KOK NEDEN (2026-09-13 sahada bulundu): kapiPoll() kapi hareket halindeyken
// HER 250ms'de bir bu fonksiyonu cagiriyor, eskiden 300ms'e kadar
// bloklayabiliyordu - bu sure zarfinda ESP8266'nin Kalburum'a RS485 durum
// gonderimi gecikip/kesilip "Partial message"/"no response" ile
// sonuclaniyordu (kullanici bulgusu: kapi kapaninca role 3-4sn gec
// birakiyordu - aslinda role zamaninda birakiyordu ama Kalburum durumu GEC
// GORUYORDU). Nano'nun normal yanit suresi cok kisa oldugundan (birkac ms)
// zaman asimi 60ms'e dusuruldu. NOT: burada rs485KomutDinle() BILEREK
// cagrilmiyor - reentrancy riski icin bkz eskiYonBirakmasiniBekle notu,
// bu fonksiyon dogrudan kapiPoll()'un AYNI kapi uzerinde calistigi
// donguden cagrildigi icin risk en yuksek buradaydi.
static int nanoAnalogOku(int pin) {
  while (Serial.available()) Serial.read();
  Serial.print("ANALOG_READ:"); Serial.println(pin);
  unsigned long t = millis(); String r = ""; bool ok = false;
  while (millis() - t < 60) {
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

// Kilit rolesine her yazim buradan gecer - komut durumu (bahceKilitAktif)
// tek noktada guncel kalsin, web/RS485 gostergesi gercegi yansitsin.
static void kilitYaz(BahceKapisi& k, bool aktif) {
  r413RoleYaz(k.releKilit, aktif);
  bahceKilitAktif = aktif;
}

// KOK NEDEN (2026-09-13 sahada bulundu): r413RoleYaz() fire-and-forget'tir -
// yazim RS485 uzerinde kaybolursa (Kalburum'un GET_STATUS turuyla cakisma
// gibi) hicbir hata donmez, yazilim komut basarili sanip durum'u guncelliyor
// ama role FIILEN enerjili kalabiliyordu (kullanici bulgusu: switch tetiklendi,
// "durum" yazilimda kapali gorunuyordu, ama R413D08'in kendi Modbus okumasi
// kapama rolesinin hala ACIK oldugunu gosterdi). Motoru DURDURMAK guvenlik
// acisindan en kritik islem oldugundan (rolerin acik kalmasi surekli akim
// cekip motoru/PSU'yu zorlar), burada da yazim SONRASI donanimdan okunarak
// dogrulanir, basarisizsa kisa bir sure icinde tekrar denenir.
// Reentrancy riski hakkinda bkz eskiYonBirakmasiniBekle notu - burada da
// ayni sebeple rs485KomutDinle() cagrilmiyor.
// Donus degeri: true = R413D08 kapaliyi DOGRULADI, false = zaman asimi/yanit
// yok (fire-and-forget fallback - cagiran taraf watchdog'a devretmeli, bkz
// bahceRoleWatchdogPoll).
static bool r413RoleKapatDogrulayarak(uint8_t koilNo) {
  unsigned long baslangic = millis();
  r413RoleYaz(koilNo, false);
  while (millis() - baslangic < BAHCE_YON_DEGISTIRME_BEKLEME_MS) {
    int durum = r413KanalDurumuOku(koilNo);
    if (durum == 0) return true;      // dogrulandi: gercekten kapali
    if (durum == 1) r413RoleYaz(koilNo, false);  // hala acik okundu - tekrar dene
    yield();
  }
  return false;  // Zaman asimi/yanit yok - fire-and-forget fallback, elimizden gelen buydu
}

bool bahceRoleSorunu = false;

static void kapiMotorDurdur(BahceKapisi& k) {
  bool okA = r413RoleKapatDogrulayarak(k.releA);
  bool okB = r413RoleKapatDogrulayarak(k.releB);
  k.durdurmaOnaylanamadi = !(okA && okB);
  k.sonDurdurmaDenemeMs = millis();
  if (k.durdurmaOnaylanamadi) {
    DEBUG_PRINTF("[BAHCE] KAPAT DOGRULANAMADI (releA=%d releB=%d) - watchdog tekrar deneyecek\n", okA, okB);
  }
  k.akimAmper = 0.0;  // motor duruyor, gosterge "0A" gostersin - eski deger yaniltici olmasin
  // KOK NEDEN (2026-09-13 sahada bulundu): kilit solenoidi SADECE
  // KAPI_KILIT_ACILIYOR fazinin kendi zaman asimi kontrolunde (kapiPoll)
  // birakiliyordu. Eger durum bu fazdan (ornegin eski bir reentrancy
  // kalintisi veya yarida kesilen bir komut yuzunden) BASKA bir yola
  // gecerse, kilit rolesi ASLA birakilmiyor, sonsuza kadar enerjili
  // kaliyordu (kullanici bulgusu: "tekli testte selenoid surekli acik
  // kaliyor"). Motoru durduran HER yol (normal dur, hata, zaman asimi)
  // artik kilidi de garanti altina alir.
  if (bahceKilitAktif) kilitYaz(k, false);
}

// Bekleme sirasinda RS485 istekleri (Kalburum'un GET_STATUS'u) servis
// edilmeye devam etsin diye duz delay() yerine bu kullanilir - bkz zil
// blok sorunu notu asagida (bahceZilGuncelle).
static void rs485ServisliBekle(unsigned long sureMs) {
  unsigned long t = millis();
  while (millis() - t < sureMs) {
    rs485KomutDinle();
    yield();
  }
}

// KOK NEDEN - SIGORTA ATMASI (2026-09-12 sahada bulundu): yon degistirirken
// (motor HALEN kapaniyorken "Ac", ya da acikken/aciliyorken "Kapat" gibi)
// eski kapiKapatKomut() A rolesini KAPAT komutuyla AYNI ANDA (araya sadece
// ~2-4ms'lik Modbus cerceve suresi girerek) B rolesini ACIYORDU. 8CH role
// modulundeki MEKANIK roleler bu kadar hizli birakmayabiliyor (tipik roleler
// birkac ms ila birkac onlarca ms surer, yuklu/endiktif motor akiminda daha
// da uzayabilir) - A hala fiziksel kapaliyken B de kapanirsa motor besleme
// hatlari KISA DEVRE olur, tam da kullanicinin gozlemledigi "sigorta atti"
// sonucu. (Ac tarafinda bu risk zaten YOKTU - kapiAcKomut once kilit darbesi
// bekliyor, o darbe suresi zaten dogal bir "roleler tam birakti" payi
// sagliyor, motoru ancak ondan sonra baslatiyor.) Cozum: motor GERCEKTEN
// hareket halindeyken kapatilip yeni yon acilmadan once BAHCE_YON_DEGISTIRME_
// BEKLEME_MS kadar bekleniyor (RS485 istekleri bu sirada da servis edilir).
// Motor zaten duruyorsa (idle acik/kapali) bu bekleme gereksiz/atlanir.

void kapiTumRoleleriKapat() {
  for (int i = 0; i < 2; i++) {
    kapiMotorDurdur(bahceKapi[i]);
    kilitYaz(bahceKapi[i], false);
  }
}

// ============ IKI KANAT LADDER-MANTIK SEKANSI (2026-09-13) ============
// Kullanicinin TAM olarak tarif ettigi, sahada dogrulanan sekans - eski
// "gecikmeliKomut" + kapiCiftKanatAc/Kapat mekanizmasinin (reentrancy ve
// kilit-birakmama hatalarina yol acan) yerini alir. Adimlar arasi
// zamanlama disinda hicbir sey (guvenlik/dur/reentrancy riski) tasimaz -
// SW tetiklenince ilgili roleyi kapatma islemi zaten kapiPoll()'un mevcut
// per-kapi limit-switch kontrolunden (asagida) GELIR, burada tekrar
// yazilmaz.
enum IkiliAdim { IKILI_YOK, AC_ADIM_KILIT, AC_ADIM_KAPI2, AC_ADIM_KAPI1, KAPA_ADIM_KAPI1 };
static IkiliAdim ikiliAdim = IKILI_YOK;
static unsigned long ikiliAdimMs = 0;

// ACILIS: Role5(kilit) HIGH -> 1sn -> Role3(Kapi2/SAG acma) HIGH -> 1sn ->
// Role1(Kapi1/SOL acma) HIGH -> 1sn -> Role5(kilit) LOW.
void bahceIkisiniAc() {
  kapiMotorDurdur(bahceKapi[0]);
  kapiMotorDurdur(bahceKapi[1]);
  // birlikte=true: bir kanat asiri akim/zaman asimindan HATA verirse (bkz
  // kapiPoll) diger kanat da (henuz hareket etmemis olsa bile) durdurulur -
  // ladder sekansi "iki kanat birlikte" komutu oldugu icin BIREBIR ayni
  // guvenlik kapsamina girmeli (2026-09-15 kullanici bulgusu: biri hata
  // verince digeri calismaya devam ediyordu).
  bahceKapi[0].birlikte = true;
  bahceKapi[1].birlikte = true;
  kilitYaz(bahceKapi[0], true);  // Role5 HIGH (ortak kilit)
  ikiliAdim = AC_ADIM_KILIT;
  ikiliAdimMs = millis();
}

// KAPANIS: Role2(Kapi1/SOL kapama) HIGH -> 2sn -> Role4(Kapi2/SAG kapama) HIGH.
void bahceIkisiniKapat() {
  kapiMotorDurdur(bahceKapi[0]);
  kapiMotorDurdur(bahceKapi[1]);
  bahceKapi[0].birlikte = true;  // bkz bahceIkisiniAc() ayni gerekce
  bahceKapi[1].birlikte = true;
  BahceKapisi& k1 = bahceKapi[0];
  r413RoleYaz(k1.releB, true);  // Role2 HIGH
  k1.durum = KAPI_HAREKET_KAPA;
  k1.hataAsiriAkim = false;
  k1.akimPeakAmper = 0.0;
  k1.hareketBaslangicMs = millis();
  ikiliAdim = KAPA_ADIM_KAPI1;
  ikiliAdimMs = millis();
}

// kapiPoll()'un basinda her dongude cagrilir - zamani gelen adimi yurutur.
static void ikiliSekansPoll() {
  if (ikiliAdim == IKILI_YOK) return;
  // Guvenlik (2026-09-15 kullanici bulgusu): sekans surerken bir kanat HATA'ya
  // dusmusse (asiri akim/zaman asimi, bkz kapiPoll + birliktekiDigerKanadiDurdur)
  // sekans hemen iptal edilir - aksi halde ladder, HATA vermis/durmus kanadin
  // yaninda HENUZ BASLAMAMIS diger kanadi da baslatmaya devam ederdi. Ortak
  // kilit de burada garanti altina alinir (motoru durduran diger kod yollari
  // zaten kendi kilidini birakiyor, ama sekans AC_ADIM_KILIT/KAPI2 fazindaysa
  // kilit henuz hicbir "motor durdur" cagrisindan gecmemis olabilir).
  if (bahceKapi[0].durum == KAPI_HATA || bahceKapi[1].durum == KAPI_HATA) {
    if (bahceKilitAktif) kilitYaz(bahceKapi[0], false);
    ikiliAdim = IKILI_YOK;
    return;
  }
  unsigned long simdi = millis();
  switch (ikiliAdim) {
    case AC_ADIM_KILIT:
      if (simdi - ikiliAdimMs >= BAHCE_IKILI_ADIM_AC_MS) {
        BahceKapisi& k2 = bahceKapi[1];
        r413RoleYaz(k2.releA, true);  // Role3 HIGH (Kapi2/SAG acma)
        k2.durum = KAPI_HAREKET_AC;
        k2.hataAsiriAkim = false;
        k2.akimPeakAmper = 0.0;
        k2.hareketBaslangicMs = simdi;
        ikiliAdim = AC_ADIM_KAPI2;
        ikiliAdimMs = simdi;
      }
      break;
    case AC_ADIM_KAPI2:
      if (simdi - ikiliAdimMs >= BAHCE_IKILI_ADIM_AC_MS) {
        BahceKapisi& k1 = bahceKapi[0];
        r413RoleYaz(k1.releA, true);  // Role1 HIGH (Kapi1/SOL acma)
        k1.durum = KAPI_HAREKET_AC;
        k1.hataAsiriAkim = false;
        k1.akimPeakAmper = 0.0;
        k1.hareketBaslangicMs = simdi;
        ikiliAdim = AC_ADIM_KAPI1;
        ikiliAdimMs = simdi;
      }
      break;
    case AC_ADIM_KAPI1:
      if (simdi - ikiliAdimMs >= BAHCE_IKILI_ADIM_AC_MS) {
        kilitYaz(bahceKapi[0], false);  // Role5 LOW
        ikiliAdim = IKILI_YOK;
      }
      break;
    case KAPA_ADIM_KAPI1:
      if (simdi - ikiliAdimMs >= BAHCE_IKILI_ADIM_KAPA_MS) {
        BahceKapisi& k2 = bahceKapi[1];
        r413RoleYaz(k2.releB, true);  // Role4 HIGH (Kapi2/SAG kapama)
        k2.durum = KAPI_HAREKET_KAPA;
        k2.hataAsiriAkim = false;
        k2.akimPeakAmper = 0.0;
        k2.hareketBaslangicMs = simdi;
        ikiliAdim = IKILI_YOK;
      }
      break;
    default: break;
  }
}

// Acilis komutu: once kilidi darbeyle acar, pulse suresi dolunca kapiPoll()
// motoru baslatir (bkz asagisi) - delay() ile bloklamadan sekans yurutulur.
// Kullanici bulgusu (2026-09-12): kapi zaten tam aciksken tekrar "Ac"
// butonuna basilinca eski kod hicbir sey kontrol etmeden kilit-darbe +
// acma sekansini yeniden baslatiyordu - kanat zaten acik son switch'e
// dayanmisken motoru tekrar o yone suruyordu (gereksiz asinma/asiri akim
// riski). Simdi komut verilmeden once GERCEK limit switch durumuna (taze
// okumaysa) bakilir; zaten o konumdaysa sadece durum senkronize edilir,
// role/motor/kilit'e hic dokunulmaz.
static bool bahceSwTazeMi() {
  return (bahceSwSonBasariliMs != 0) && (millis() - bahceSwSonBasariliMs < BAHCE_SW_TAZELIK_MS);
}

// Tek kapi (Sudepo'nun kendi Kapi1/Kapi2 butonlari veya fiziksel
// BAHCE_KAPI1_AC butonu) icin dogrudan komut - iki kapiyi BIRLIKTE/sirali
// yonetmek icin bkz bahceIkisiniAc()/bahceIkisiniKapat() yukarida.
KapiKomutSonuc kapiAcKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_AC || k.durum == KAPI_KILIT_ACILIYOR) return KAPI_KOMUT_ZATEN_HAREKETTE;
  bool acikLimit = (i == 0) ? bahceKapi1TamAcik : bahceKapi2TamAcik;
  if (bahceSwTazeMi() && acikLimit) { k.durum = KAPI_ACIK; return KAPI_KOMUT_ZATEN_ORADA; }  // zaten tam acik - tekrar surme
  kapiMotorDurdur(k);  // ters yonden (kapaniyor) gelinmis olabilir - once motoru kes
  kilitYaz(k, true);
  k.kilitPulseBaslangicMs = millis();
  k.hataAsiriAkim = false;
  k.akimPeakAmper = 0.0;  // yeni hareket - onceki tepe deger artik gecersiz
  k.birlikte = birlikte;
  k.durum = KAPI_KILIT_ACILIYOR;
  return KAPI_KOMUT_BASLADI;
}

KapiKomutSonuc kapiKapatKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_KAPA) return KAPI_KOMUT_ZATEN_HAREKETTE;
  // NOT: kapiTamKapaliMi() (D0/D1) her basarili GET_STATUS yanitinda tazelenir,
  // bahceSwSonBasariliMs (=bahceSwTazeMi()) ise D7/D9 ACIK switch/zil verisinin
  // tazeligini takip eder - IKISI FARKLI ALANLAR, o yuzden burada sadece Nano
  // baglantisinin canli olup olmadigina (nanoBaglantiVar) bakiliyor.
  if (nanoBaglantiVar && kapiTamKapaliMi(i)) { k.durum = KAPI_KAPALI; return KAPI_KOMUT_ZATEN_ORADA; }  // zaten tam kapali - tekrar surme
  kapiMotorDurdur(k);
  // Bkz yukaridaki "SIGORTA ATMASI" notu (ikinci kez atti, 2026-09-12) -
  // artik SADECE zamanlamaya guvenilmiyor, R413D08'den eski (Ac) rolenin
  // GERCEKTEN birakip birakmadigi donanimdan okunarak dogrulaniyor. Motor
  // hareketsiz olsa bile (idle) bu kontrol ucretsiz/hizli (role zaten kapali
  // okunur, aninda devam eder) - o yuzden kosulsuz her zaman calisir.
  eskiYonBirakmasiniBekle(k.releA);
  r413RoleYaz(k.releB, true);  // releaseA zaten kapiMotorDurdur() ile kapatildi, tekrar yazmaya gerek yok
  k.hareketBaslangicMs = millis();
  k.hataAsiriAkim = false;
  k.akimPeakAmper = 0.0;  // yeni hareket - onceki tepe deger artik gecersiz
  k.birlikte = birlikte;
  k.durum = KAPI_HAREKET_KAPA;
  return KAPI_KOMUT_BASLADI;
}

// Motoru dogrudan verilen yone alir (kilit-darbe sekansi YOK) - sadece
// zaten hareket halindeki bir kanadi aninda ters yone almak icin (bkz
// kapiPoll asiri akim guvenligi). Buraya HER ZAMAN motor fiilen calisirken
// (asiri akim/ters yon guvenligi) girilir, o yuzden settle-bekleme HER
// ZAMAN uygulanir (yukaridaki kapiKapatKomut'taki kosullu halinden farkli).
static void kapiYoneAyarla(BahceKapisi& k, KapiDurum yon, unsigned long now) {
  if (yon == KAPI_HAREKET_AC) {
    r413RoleYaz(k.releB, false);
    eskiYonBirakmasiniBekle(k.releB);
    r413RoleYaz(k.releA, true);
  } else {
    r413RoleYaz(k.releA, false);
    eskiYonBirakmasiniBekle(k.releA);
    r413RoleYaz(k.releB, true);
  }
  k.hareketBaslangicMs = now;
  k.durum = yon;
}

void kapiDurdurKomut(int i) {
  // Henuz baslamamis ikili sekans adimi varsa onu da iptal et - yoksa "Dur"
  // dedikten saniyeler sonra diger kanat/kilit kendi kendine devam ederdi.
  ikiliAdim = IKILI_YOK;
  BahceKapisi& k = bahceKapi[i];
  // Sadece gercekten hareket halindeyse dokun - BAHCE_KAPI_DUR komutu iki
  // kanada birden gider, hareketsiz (zaten kapali/acik) kanadin durumunu
  // yanlislikla HATA'ya cekmesin.
  if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA && k.durum != KAPI_KILIT_ACILIYOR) return;
  kapiMotorDurdur(k);
  kilitYaz(k, false);
  k.durum = KAPI_HATA;
}

// Bir kapinin "kapat" komutu ilk denemede dogrulanamadiysa (R413D08 gecici
// kilitlenme/RS485 cakismasi), buradan pes etmeden BAHCE_ROLE_WATCHDOG_
// ARALIK_MS'de bir tekrar denenir - motorun kapali durum/HATA/zaman asimina
// ragmen fiilen enerjili kalmasina karsi son savunma hatti (bkz bahce_kapisi.h
// notu, 2026-09-15 sahada 5+ dakika enerjili kalma vakasi).
//
// KOK NEDEN - REGRESYON (2026-09-15, ayni gun ikinci bulgu): ilk surumde bu
// fonksiyon k.durum'a HIC bakmadan calisiyordu. kapiAcKomut/kapiKapatKomut
// YENI bir harekete baslarken de ONCE kapiMotorDurdur() cagirir (ters yonden
// kalmis olabilecek roleyi kapatmak icin) - R413D08'in okuma yaniti ROLE ZATEN
// KAPALIYKEN BILE arada bir gecici olarak basarisiz/gecikmeli donebiliyor
// (bilinen bir R413D08 protokol-uyum zayifligi, bkz github.com/microrobotics/
// R413D08 issue #1 ve genel Modbus RTU role modulu forumlari). Bu durumda
// durdurmaOnaylanamadi YANLISLIKLA true oluyordu - kapi hemen ardindan kilit/
// motor rolesini ACARKEN, watchdog'un 1sn'lik tekrar-deneme araligi TAM
// kilit-darbe suresiyle (BAHCE_KILIT_PULSE_MS=1000ms) cakisip motoru
// ACAR ACMAZ tekrar KAPATIYORDU ("role cekiyor ama hemen birakiyor").
// COZUM: watchdog SADECE kapinin durumu "durmus/durmali" ise (KAPI_KAPALI/
// KAPI_ACIK/KAPI_HATA) mudahale eder - kapi BILEREK hareket halindeyken
// (KAPI_KILIT_ACILIYOR/KAPI_HAREKET_AC/KAPI_HAREKET_KAPA) o an surmekte olan
// GERCEK harekete asla karismaz; o hareketi durdurmak kapiPoll()'un kendi
// limit switch/zaman asimi/asiri akim mantigina aittir.
void bahceRoleWatchdogPoll() {
  unsigned long now = millis();
  bool sorunVar = false;
  // KOK NEDEN - 2. REGRESYON (2026-09-15, ayni gun UCUNCU bulgu): yukaridaki
  // per-kapi durum kontrolu TEK KAPI komutlarini (kapiAcKomut/kapiKapatKomut,
  // durum'u ANINDA KILIT_ACILIYOR/HAREKET_AC/KAPA yapar) korurken, CIFT KAPI
  // ladder-mantik sekansini (bahceIkisiniAc/Kapat + ikiliSekansPoll) KORUMUYORDU
  // - o sekans Kapi1'in (bahceKapi[0]) durum'unu kilit-tutma fazinda (Role5
  // HIGH, ~1sn) HIC degistirmiyor, eskisi (orn. KAPI_KAPALI) gibi kaliyor.
  // Watchdog bunu "durmus olmali" saniyor, 1sn sonra (TAM kilit-tutma suresiyle
  // CAKISARAK) bahceKilitAktif'i erken kesiyordu - kilit kapi hareket etmeye
  // baslamadan/motor tam calisirken birakilinca kanat hala mekanik kilitliyken
  // motor zorlanip ASIRI AKIM veriyordu (kullanici bulgusu: "cift kapi ac"
  // asiri akim, "tek kapi ac" sorunsuz - tam bu farktan kaynaklaniyordu).
  // COZUM: ladder sekansi surerken (ikiliAdim != IKILI_YOK) watchdog HICBIR
  // kapiya dokunmaz - sekansin kendi zamanlamasi bitene kadar butun koruma
  // bu bayraga devredilir.
  bool ladderSekansiSuruyor = (ikiliAdim != IKILI_YOK);
  for (int i = 0; i < 2; i++) {
    BahceKapisi& k = bahceKapi[i];
    bool hareketIstiyor = ladderSekansiSuruyor || (k.durum == KAPI_KILIT_ACILIYOR || k.durum == KAPI_HAREKET_AC || k.durum == KAPI_HAREKET_KAPA);
    if (hareketIstiyor) continue;  // bilerek hareket ediyor - watchdog karismaz
    if (!k.durdurmaOnaylanamadi) continue;
    if (now - k.sonDurdurmaDenemeMs >= BAHCE_ROLE_WATCHDOG_ARALIK_MS) {
      DEBUG_PRINTF("[BAHCE] WATCHDOG: KAPI%d kapatma tekrar deneniyor\n", i + 1);
      kapiMotorDurdur(k);  // kendi ici k.durdurmaOnaylanamadi/sonDurdurmaDenemeMs'i gunceller
    }
    if (k.durdurmaOnaylanamadi) sorunVar = true;
  }
  bahceRoleSorunu = sorunVar;
}

bool r413ModulSagliksiz = false;

// "Durtme" sinyali: hicbir roleyi degistirmeden sadece kanal 1'i okur (0x03
// READ) - R413D08 cevap veriyorsa modul canli/komut isliyor demektir. Kapi
// hareket halindeyken zaten baska okumalar oluyor, bu yuzden burada sadece
// IKI kapi da tam hareketsizken calisir - gereksiz RS485 trafigi olmasin.
void r413SaglikPoll() {
  static unsigned long sonKontrolMs = 0;
  bool hareketVar = (ikiliAdim != IKILI_YOK);  // ladder sekansinin kilit-tutma fazinda durum henuz degismemis olabilir
  for (int i = 0; i < 2 && !hareketVar; i++) {
    KapiDurum d = bahceKapi[i].durum;
    if (d == KAPI_HAREKET_AC || d == KAPI_HAREKET_KAPA || d == KAPI_KILIT_ACILIYOR) hareketVar = true;
  }
  if (hareketVar) return;
  unsigned long now = millis();
  if (now - sonKontrolMs < R413_SAGLIK_KONTROL_ARALIK_MS) return;
  sonKontrolMs = now;
  int durum = r413KanalDurumuOku(bahceKapi[0].releA);  // sadece OKUR, hicbir role degismez
  r413ModulSagliksiz = (durum < 0);
  if (r413ModulSagliksiz) DEBUG_PRINTLN("[BAHCE] R413D08 saglik kontrolu: yanit yok");
}

// GUVENLIK (2026-09-15 kullanici bulgusu): "birlikte" (iki kanat ayni komutla
// baslatilmis) bir hareket sirasinda bir kanat HATA'ya duserse (asiri akim
// -sonrasi da asiri akim- veya zaman asimi), DIGER kanat eskiden hareketine
// DEVAM EDIYORDU - kullanici bunu acik bir guvenlik hatasi olarak bildirdi.
// Bu fonksiyon eslesmis diger kanadi (henuz baslamamis KAPI_KILIT_ACILIYOR
// fazinda bile olsa) hemen durdurur ve HATA'ya ceker - kapiMotorDurdur zaten
// ortak kilidi de (bahceKilitAktif ise) birakir.
static void birliktekiDigerKanadiDurdur(int i, const char* sebep) {
  BahceKapisi& k = bahceKapi[i];
  if (!k.birlikte) return;
  BahceKapisi& diger = bahceKapi[1 - i];
  if (diger.durum == KAPI_KILIT_ACILIYOR || diger.durum == KAPI_HAREKET_AC || diger.durum == KAPI_HAREKET_KAPA) {
    kapiMotorDurdur(diger);
    diger.durum = KAPI_HATA;
    DEBUG_PRINTF("[KAPI%d] eslesmis kanat durduruldu (diger kanat hata verdi: %s)\n", 2 - i, sebep);
  }
}

void kapiPoll() {
  unsigned long now = millis();

  bahceRoleWatchdogPoll();  // bkz yukarida - dogrulanamayan "kapat" komutlarini pes etmeden tekrar dener
  r413SaglikPoll();  // bkz yukarida - kapi hareketsizken bile modulun canli oldugunu periyodik dogrular
  ikiliSekansPoll();  // bkz yukarida - iki kapi ladder-mantik sekansinin zamanlanmis adimlari

  for (int i = 0; i < 2; i++) {
    BahceKapisi& k = bahceKapi[i];
    if (k.durum == KAPI_KILIT_ACILIYOR) {
      if (now - k.kilitPulseBaslangicMs >= BAHCE_KILIT_PULSE_MS) {
        kilitYaz(k, false);  // kilit darbesi bitti, motoru baslat
        r413RoleYaz(k.releB, false);
        eskiYonBirakmasiniBekle(k.releB);  // bkz "SIGORTA ATMASI" notu - donanimsal onay
        r413RoleYaz(k.releA, true);
        k.hareketBaslangicMs = now;
        k.akimPeakAmper = 0.0;
        k.durum = KAPI_HAREKET_AC;
      }
      continue;
    }
    if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA) continue;
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
      if (k.akimAmper > k.akimPeakAmper) k.akimPeakAmper = k.akimAmper;  // bu hareketin en yuksegi (2026-09-15 kullanici talebi - esik ayari icin referans)
    }

    bool zamanAsimi = (now - k.hareketBaslangicMs) > BAHCE_MAX_HAREKET_MS;
#if BAHCE_ASIRI_AKIM_KONTROL_AKTIF
    bool asiriAkim = akimRaw >= 0 && fabs(akimAmper) > bahceAkimEsikAGetir(i);
#else
    bool asiriAkim = false;  // bkz config.h - motor/ACS712 henuz takili degilken gecici kapali
#endif

    if (k.durum == KAPI_HAREKET_AC && acikOk && acikLimit) {
      kapiMotorDurdur(k);
      k.durum = KAPI_ACIK;
    } else if (k.durum == KAPI_HAREKET_KAPA && kapaliLimit) {
      kapiMotorDurdur(k);
      k.durum = KAPI_KAPALI;
    } else if (asiriAkim) {
      kapiMotorDurdur(k);
      if (k.hataAsiriAkim) {
        // Ters yone alindiktan SONRA da asiri akim geldi - gercek bir
        // tikanma/mekanik ariza (guvenlik icin ters-ileri ping-pong YAPMA),
        // motoru durdurup HATA'ya gec, kullanicinin mudahalesini bekle.
        k.durum = KAPI_HATA;
        DEBUG_PRINTF("[KAPI%d] HATA: ters yon sonrasi da asiri akim\n", i + 1);
        birliktekiDigerKanadiDurdur(i, "asiri akim");
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
      birliktekiDigerKanadiDurdur(i, "zaman asimi");
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
// KOK NEDEN (2026-09-12 sahada bulundu): asagidaki iki TONE_PLAY komutu
// eskiden delay()/bloklayan while-ACK dongusuyle yaziliyordu (~1080ms toplam).
// Bu sure boyunca ESP8266'nin ana loop()'u tamamen bloke oluyor, Kalburum'dan
// gelen RS485 "GET_STATUS" istegine YANIT VEREMIYORDU. Kalburum 400ms'de
// zaman asimina ugrayip "no response" diyor, gecikmis yanit da bir sonraki
// 600ms'lik dongunun basindaki tampon temizlemesiyle SESSIZCE atiliyordu -
// zil mandal penceresi (1500ms) bu yuzden sistematik olarak kaciriliyordu
// (rastgele degil, her seferinde ayni zamanlamayla). Sudepo'nun KENDI
// buzzer'i (bu fonksiyonun cagirdigi Nano TONE_PLAY) hep calindigi icin
// sorun gizli kaldi - sadece Kalburum'a giden RS485 durumu etkileniyordu.
// Cozum: bekleme dongulerinde rs485KomutDinle() de cagrilir, boylece
// RS485 istekleri ton calarken bile servis edilmeye devam eder. Ayni
// yardimci fonksiyon asagida role yon degistirme settle-bekleme icin de
// kullaniliyor (bkz kapiYonDegistirRoleYaz).
void bahceZilGuncelle(bool basili) {
  static bool oncekiBasili = false;
  unsigned long now = millis();
  bahceZilBasili = basili;
  if (basili && !oncekiBasili) {
    bahceZilSonCalmaMs = now;
    // Kalburum'un zili poll sirasini beklemesin - durumu ANINDA gonder.
    masterGonder();
    DEBUG_PRINTLN("[ZIL] basildi, ding-dong calinacak");
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON1_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    unsigned long tz = millis();
    while (millis() - tz < 300) { if (Serial.available()) { String ra = Serial.readStringUntil('\n'); if (ra.indexOf("ACK") >= 0) break; } rs485KomutDinle(); yield(); }
    rs485ServisliBekle(BAHCE_ZIL_TON_SURE_MS + 30);
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON2_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    tz = millis();
    while (millis() - tz < 300) { if (Serial.available()) { String ra = Serial.readStringUntil('\n'); if (ra.indexOf("ACK") >= 0) break; } rs485KomutDinle(); yield(); }
  }
  oncekiBasili = basili;
}
