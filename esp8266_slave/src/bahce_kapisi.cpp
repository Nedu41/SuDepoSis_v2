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
static int nanoAnalogOku(int pin) {
  while (Serial.available()) Serial.read();
  Serial.print("ANALOG_READ:"); Serial.println(pin);
  unsigned long t = millis(); String r = ""; bool ok = false;
  while (millis() - t < 300) {
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

static void kapiMotorDurdur(BahceKapisi& k) {
  r413RoleYaz(k.releA, false);
  r413RoleYaz(k.releB, false);
  k.akimAmper = 0.0;  // motor duruyor, gosterge "0A" gostersin - eski deger yaniltici olmasin
}

void kapiTumRoleleriKapat() {
  for (int i = 0; i < 2; i++) {
    kapiMotorDurdur(bahceKapi[i]);
    kilitYaz(bahceKapi[i], false);
  }
}

// Gecikmeli (ikinci) kanat komutu - bkz kapiCiftKanatAc/Kapat. 0 = bekleyen yok.
static unsigned long gecikmeliKomutMs = 0;
static int gecikmeliKomutKapi = -1;
static bool gecikmeliKomutAc = false;

// 2 kanatli kapilarin temel kurali: kanatlar orta noktada bindirdigi icin
// ayni anda hareket EDEMEZ. Acilista ust kanat once, kapanista en son -
// aradaki gecikme config.h'de (ticari kartlardaki "leaf delay/phase shift").
void kapiCiftKanatAc() {
  int once = BAHCE_ONCE_ACILAN_KAPI, sonra = 1 - BAHCE_ONCE_ACILAN_KAPI;
  kapiAcKomut(once, true);
  gecikmeliKomutKapi = sonra;
  gecikmeliKomutAc = true;
  gecikmeliKomutMs = millis() + BAHCE_KANAT_GECIKME_AC_MS;
}

void kapiCiftKanatKapat() {
  // Kapanista sira TERS: ustteki kanat en son kapanmali ki digerinin ustune otursun.
  int once = 1 - BAHCE_ONCE_ACILAN_KAPI, sonra = BAHCE_ONCE_ACILAN_KAPI;
  kapiKapatKomut(once, true);
  gecikmeliKomutKapi = sonra;
  gecikmeliKomutAc = false;
  gecikmeliKomutMs = millis() + BAHCE_KANAT_GECIKME_KAPA_MS;
}

void kapiGecikmeliKomutIptal() { gecikmeliKomutMs = 0; gecikmeliKomutKapi = -1; }

// Acilis komutu: once kilidi darbeyle acar, pulse suresi dolunca kapiPoll()
// motoru baslatir (bkz asagisi) - delay() ile bloklamadan sekans yurutulur.
void kapiAcKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_AC || k.durum == KAPI_KILIT_ACILIYOR) return;
  kapiMotorDurdur(k);  // ters yonden (kapaniyor) gelinmis olabilir - once motoru kes
  kilitYaz(k, true);
  k.kilitPulseBaslangicMs = millis();
  k.hataAsiriAkim = false;
  k.birlikte = birlikte;
  k.durum = KAPI_KILIT_ACILIYOR;
}

void kapiKapatKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_KAPA) return;
  kapiMotorDurdur(k);
  r413RoleYaz(k.releA, false);
  r413RoleYaz(k.releB, true);
  k.hareketBaslangicMs = millis();
  k.hataAsiriAkim = false;
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
  BahceKapisi& k = bahceKapi[i];
  // Sadece gercekten hareket halindeyse dokun - BAHCE_KAPI_DUR komutu iki
  // kanada birden gider, hareketsiz (zaten kapali/acik) kanadin durumunu
  // yanlislikla HATA'ya cekmesin.
  if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA && k.durum != KAPI_KILIT_ACILIYOR) return;
  kapiMotorDurdur(k);
  kilitYaz(k, false);
  k.durum = KAPI_HATA;
}

void kapiPoll() {
  unsigned long now = millis();

  // Bekleyen ikinci kanat komutu zamani geldiyse baslat (kanat gecikmesi).
  if (gecikmeliKomutMs != 0 && (long)(now - gecikmeliKomutMs) >= 0) {
    int k = gecikmeliKomutKapi;
    bool ac = gecikmeliKomutAc;
    kapiGecikmeliKomutIptal();
    if (k >= 0) { if (ac) kapiAcKomut(k, true); else kapiKapatKomut(k, true); }
  }

  for (int i = 0; i < 2; i++) {
    BahceKapisi& k = bahceKapi[i];
    if (k.durum == KAPI_KILIT_ACILIYOR) {
      if (now - k.kilitPulseBaslangicMs >= BAHCE_KILIT_PULSE_MS) {
        kilitYaz(k, false);  // kilit darbesi bitti, motoru baslat
        r413RoleYaz(k.releB, false);
        r413RoleYaz(k.releA, true);
        k.hareketBaslangicMs = now;
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
    if (akimRaw >= 0) k.akimAmper = fabs(akimAmper);  // web/RS485'e tasinan canli deger

    bool zamanAsimi = (now - k.hareketBaslangicMs) > BAHCE_MAX_HAREKET_MS;
    bool asiriAkim = akimRaw >= 0 && fabs(akimAmper) > bahceAkimEsikAGetir(i);

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
