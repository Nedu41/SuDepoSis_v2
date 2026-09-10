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
extern bool kapi1Acik, kapi2Acik; // main.cpp - depo alarm kapi sensorleri, "kapali" tespiti icin

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

// Nano'nun genel PIN_READ/ANALOG_READ komutlarina senkron (bloklayan) sarmalayici.
static bool nanoDijitalOku(int pin, bool* okundu = nullptr) {
  while (Serial.available()) Serial.read();
  Serial.print("PIN_READ:"); Serial.println(pin);
  unsigned long t = millis(); String r = ""; bool ok = false;
  while (millis() - t < 300) {
    if (Serial.available()) { r = Serial.readStringUntil('\n'); r.trim(); if (r.indexOf("PIN:") >= 0) { ok = true; break; } }
    yield();
  }
  if (okundu) *okundu = ok;
  if (!ok) return false;
  int eq = r.indexOf('=');
  return eq >= 0 ? (r.substring(eq + 1).toInt() != 0) : false;
}

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

// "Kapalı" konumu icin ayri Nano sorgusu YOK - mevcut alarm kapi sensoru
// (kapi1Acik/kapi2Acik, GET_STATUS ile zaten surekli taze) dogrudan kullanilir.
// Bu sensor "kapi acik" algiladiginda true oldugundan, bahce kapisi
// "kapali" durumu = !kapi1Acik/!kapi2Acik.
static bool kapiMevcutAlarmSensoruKapali(int i) { return i == 0 ? !kapi1Acik : !kapi2Acik; }

static void kapiMotorDurdur(BahceKapisi& k) {
  r413RoleYaz(k.releA, false);
  r413RoleYaz(k.releB, false);
}

void kapiTumRoleleriKapat() {
  for (int i = 0; i < 2; i++) {
    kapiMotorDurdur(bahceKapi[i]);
    r413RoleYaz(bahceKapi[i].releKilit, false);
  }
}

// Acilis komutu: once kilidi darbeyle acar, pulse suresi dolunca kapiPoll()
// motoru baslatir (bkz asagisi) - delay() ile bloklamadan sekans yurutulur.
void kapiAcKomut(int i, bool birlikte) {
  BahceKapisi& k = bahceKapi[i];
  if (k.durum == KAPI_HAREKET_AC || k.durum == KAPI_KILIT_ACILIYOR) return;
  kapiMotorDurdur(k);  // ters yonden (kapaniyor) gelinmis olabilir - once motoru kes
  r413RoleYaz(k.releKilit, true);
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
  BahceKapisi& k = bahceKapi[i];
  // Sadece gercekten hareket halindeyse dokun - BAHCE_KAPI_DUR komutu iki
  // kanada birden gider, hareketsiz (zaten kapali/acik) kanadin durumunu
  // yanlislikla HATA'ya cekmesin.
  if (k.durum != KAPI_HAREKET_AC && k.durum != KAPI_HAREKET_KAPA && k.durum != KAPI_KILIT_ACILIYOR) return;
  kapiMotorDurdur(k);
  r413RoleYaz(k.releKilit, false);
  k.durum = KAPI_HATA;
}

void kapiPoll() {
  unsigned long now = millis();
  for (int i = 0; i < 2; i++) {
    BahceKapisi& k = bahceKapi[i];
    if (k.durum == KAPI_KILIT_ACILIYOR) {
      if (now - k.kilitPulseBaslangicMs >= BAHCE_KILIT_PULSE_MS) {
        r413RoleYaz(k.releKilit, false);  // kilit darbesi bitti, motoru baslat
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

    bool acikOk;
    bool acikLimit = nanoDijitalOku(k.acikPin, &acikOk) == LOW;
    bool kapaliLimit = kapiMevcutAlarmSensoruKapali(i);  // mevcut alarm kapi sensorunden, ekstra Nano sorgusu yok
    int akimRaw = nanoAnalogOku(k.akimPin);
    float akimAmper = (akimRaw >= 0) ? ((akimRaw - BAHCE_AKIM_SIFIR_RAW) * (5000.0 / 1024.0)) / ACS712_MV_PER_AMP : 0.0;

    bool zamanAsimi = (now - k.hareketBaslangicMs) > BAHCE_MAX_HAREKET_MS;
    bool asiriAkim = akimRaw >= 0 && fabs(akimAmper) > BAHCE_AKIM_ESIK_A;

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

// ============ ZIL BUTONU ============
// Disarida buton, iceride (Nano/Sudepo) buzzer "ding-dong" calar - klasik
// kapi zili. Motor hareket halindeyken kapiPoll() zaten sik Nano sorgusu
// yaptigindan, cakismayi/asiri trafigi onlemek icin bu poll de ayni interval
// mantigini kullanir ama BAGIMSIZ calisir (kapi hareketinden etkilenmez).
void zilButonPoll() {
  static unsigned long sonPollMs = 0;
  static bool oncekiBasili = false;
  unsigned long now = millis();
  if (now - sonPollMs < BAHCE_ZIL_POLL_ARALIK_MS) return;
  sonPollMs = now;
  bool okundu = false;
  bool basili = nanoDijitalOku(BAHCE_ZIL_BUTON_PIN, &okundu) == LOW;  // INPUT_PULLUP, basilinca LOW
  if (!okundu) return;  // Nano yanit vermediyse bu turu atla, oncekiBasili DEGISTIRME
  if (basili && !oncekiBasili) {
    DEBUG_PRINTLN("[ZIL] basildi, ding-dong calinacak");
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON1_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    unsigned long t = millis();
    while (millis() - t < 300) { if (Serial.available()) { String r = Serial.readStringUntil('\n'); if (r.indexOf("ACK") >= 0) break; } yield(); }
    delay(BAHCE_ZIL_TON_SURE_MS + 30);
    while (Serial.available()) Serial.read();
    Serial.print("TONE_PLAY:"); Serial.print(NANO_BUZZER_PIN); Serial.print(","); Serial.print(BAHCE_ZIL_TON2_HZ); Serial.print(","); Serial.println(BAHCE_ZIL_TON_SURE_MS);
    t = millis();
    while (millis() - t < 300) { if (Serial.available()) { String r = Serial.readStringUntil('\n'); if (r.indexOf("ACK") >= 0) break; } yield(); }
  }
  oncekiBasili = basili;
}
