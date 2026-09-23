// SPI NOR Flash (SOIC-8) saglamlik testi - ESP32 VSPI uzerinden JEDEC sorgusu.
//
// Cip takili degilken veya oluyken MISO hatti bosta kalir ve 0xFF okunur;
// kisa devreli cipte 0x00 gelir. Bu yuzden iki uc deger de "cevap yok"
// olarak degerlendirilir - gercek bir cip her zaman anlamli bir uretici
// kodu doner.

#include <Arduino.h>
#include <SPI.h>

#define FLASH_CS    5      // VSPI SS
#define SPI_HZ      1000000UL   // 1 MHz - uzun jumper kablolarda guvenli

// SPI NOR standart komutlari
#define CMD_JEDEC_ID    0x9F
#define CMD_READ_STATUS 0x05
#define CMD_READ_DATA   0x03

static SPISettings ayar(SPI_HZ, MSBFIRST, SPI_MODE0);

static const char* ureticiAdi(uint8_t m) {
  switch (m) {
    case 0xEF: return "Winbond";
    case 0xC8: return "GigaDevice";
    case 0x20: return "XMC / Micron";
    case 0x5E: return "Zbit";
    case 0x1C: return "EON";
    case 0xBF: return "SST";
    case 0x01: return "Spansion / Cypress";
    case 0xC2: return "Macronix";
    case 0x9D: return "ISSI";
    case 0x0B: return "XTX";
    default:   return "BILINMIYOR";
  }
}

// JEDEC kapasite bayti 2^n bayt anlamina gelir (0x18 = 2^24 = 16 MB)
static uint32_t kapasiteBayt(uint8_t c) {
  return (c >= 0x10 && c <= 0x1B) ? (1UL << c) : 0;
}

static void ikiHane(uint8_t v) {
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(FLASH_CS, OUTPUT);
  digitalWrite(FLASH_CS, HIGH);
  SPI.begin();   // SCK=18, MISO=19, MOSI=23

  Serial.println();
  Serial.println(F("=== SPI NOR Flash Test Cihazi ==="));
  Serial.println(F("Baglanti: CS=GPIO5  MISO=GPIO19  MOSI=GPIO23  CLK=GPIO18"));
  Serial.println(F("Flash pin3 (WP#) ve pin7 (HOLD#) 3.3V'a baglanmali!"));
  Serial.println();
}

void loop() {
  uint8_t id[3];

  SPI.beginTransaction(ayar);
  digitalWrite(FLASH_CS, LOW);
  SPI.transfer(CMD_JEDEC_ID);
  id[0] = SPI.transfer(0x00);
  id[1] = SPI.transfer(0x00);
  id[2] = SPI.transfer(0x00);
  digitalWrite(FLASH_CS, HIGH);
  SPI.endTransaction();

  Serial.print(F("JEDEC ID : "));
  ikiHane(id[0]); Serial.print(' ');
  ikiHane(id[1]); Serial.print(' ');
  ikiHane(id[2]);
  Serial.println();

  bool cevapYok = (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) ||
                  (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00);

  if (cevapYok) {
    Serial.println(F("SONUC    : CEVAP YOK"));
    Serial.println(F("           Cip olu, takili degil, ters takilmis"));
    Serial.println(F("           veya WP#/HOLD# 3.3V'a baglanmamis olabilir."));
  } else {
    uint32_t bayt = kapasiteBayt(id[2]);

    Serial.print(F("Uretici  : "));
    Serial.println(ureticiAdi(id[0]));

    Serial.print(F("Kapasite : "));
    if (bayt) {
      Serial.print(bayt / (1024UL * 1024UL));
      Serial.print(F(" MB ("));
      Serial.print(bayt / (128UL * 1024UL));
      Serial.println(F(" Mbit)"));
    } else {
      Serial.println(F("cozulemedi"));
    }

    // Status register - ikinci bagimsiz dogrulama
    SPI.beginTransaction(ayar);
    digitalWrite(FLASH_CS, LOW);
    SPI.transfer(CMD_READ_STATUS);
    uint8_t sr = SPI.transfer(0x00);
    digitalWrite(FLASH_CS, HIGH);
    SPI.endTransaction();

    Serial.print(F("Status   : 0x"));
    ikiHane(sr);
    Serial.println();

    // Adres 0'dan 16 bayt - veri yolunun gercekten calistigini gosterir
    Serial.print(F("Ilk 16 B : "));
    SPI.beginTransaction(ayar);
    digitalWrite(FLASH_CS, LOW);
    SPI.transfer(CMD_READ_DATA);
    SPI.transfer(0x00); SPI.transfer(0x00); SPI.transfer(0x00);
    for (uint8_t i = 0; i < 16; i++) {
      ikiHane(SPI.transfer(0x00));
      Serial.print(' ');
    }
    digitalWrite(FLASH_CS, HIGH);
    SPI.endTransaction();
    Serial.println();

    Serial.println(F("SONUC    : SAGLAM"));
  }

  Serial.println(F("---"));
  delay(2000);
}
