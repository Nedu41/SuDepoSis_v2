// ============================================================
// SuDepoSis v2 - Arduino Nano I/O Control
// Kapı Sensörleri (Input) + Röle (Output)
// ============================================================
#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================
void readInputs();
void handleSerialCommand();
void sendStatusPeriodic();

// ============================================================
// GLOBAL VERİLER
// ============================================================
bool door1_open = false;
bool door2_open = false;
bool relay_active = false;
bool lamp_on = false;
bool moisture_output = false;
unsigned long last_status_send_ms = 0;
unsigned long door1_last_change_ms = 0;
unsigned long door2_last_change_ms = 0;

// ============================================================
// RÖLE POLARİTESİ - calisma-zamaninda ayarlanabilir (EEPROM'da kalici)
// ============================================================
// AMAC: D4 rolesinin "aktif" seviyesini (HIGH/LOW) donanim/kablolamaya gore
// yeniden flash ATMADAN degistirebilmek. Bahce/depoya monte edildikten sonra
// Nano'ya fiziksel erisim zor oluyor - ESP8266/ESP32 OTA ile guncellenebilir
// ama Nano guncellenemez. Bu ayari calisma zamaninda (seri komutla, EEPROM'a
// yazarak) degistirilebilir hale getirerek bu sorunu bir daha yasamiyoruz.
#define EEPROM_ADDR_RELAY_POL   0
// NOT: RELAY_ACTIVE varsayilani HIGH'dan LOW'a duzeltildiginde (NC role
// kablolamasiyla eslessin diye) bu magic bump edildi (eski: 0xA5). Aksi
// halde daha once bu Nano'da calismis herhangi bir firmware EEPROM'a HIGH
// yazmis olabilir ve relayPolariteYukle() yeni varsayilani hic gormeden
// eski (yanlis) degeri okumaya devam ederdi.
#define EEPROM_MAGIC_RELAY_POL  0xA6
uint8_t relayAktifSeviye = RELAY_ACTIVE; // varsayilan: config.h'daki (LOW - NC role)

void relayPolariteYukle() {
  if (EEPROM.read(EEPROM_ADDR_RELAY_POL) == EEPROM_MAGIC_RELAY_POL) {
    relayAktifSeviye = EEPROM.read(EEPROM_ADDR_RELAY_POL + 1) ? HIGH : LOW;
  } else {
    // Ilk calisma - varsayilani EEPROM'a yaz
    relayAktifSeviye = RELAY_ACTIVE;
    EEPROM.write(EEPROM_ADDR_RELAY_POL, EEPROM_MAGIC_RELAY_POL);
    EEPROM.write(EEPROM_ADDR_RELAY_POL + 1, relayAktifSeviye == HIGH ? 1 : 0);
  }
}

void relayPolariteKaydet(bool aktifHigh) {
  relayAktifSeviye = aktifHigh ? HIGH : LOW;
  EEPROM.write(EEPROM_ADDR_RELAY_POL, EEPROM_MAGIC_RELAY_POL);
  EEPROM.write(EEPROM_ADDR_RELAY_POL + 1, aktifHigh ? 1 : 0);
}

int relayPasifSeviye() { return relayAktifSeviye == HIGH ? LOW : HIGH; }

// ============================================================
// SETUP
// ============================================================
void setup() {
  // Serial: 9600 baud (ESP8266 ile)
  Serial.begin(SERIAL_BAUD);

  // GPIO Setup
  pinMode(DOOR1_PIN, INPUT_PULLUP);    // Açıkken HIGH
  pinMode(DOOR2_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LAMBA_PIN, OUTPUT);
  pinMode(MOISTURE_PIN, OUTPUT);
  pinMode(MOISTURE_ADC_PIN, INPUT);    // A0 - nem sensörü

  relayPolariteYukle();
  digitalWrite(RELAY_PIN, relayPasifSeviye());  // Basta pasif
  digitalWrite(LAMBA_PIN, LAMBA_OFF_STATE); // Lamba başta kapalı
  digitalWrite(MOISTURE_PIN, LOW);         // Nem kontrolu kapali
  moisture_output = false;

  delay(1000);
}

// ============================================================
// I/O KONTROL
// ============================================================
void readInputs() {
  // Kapılar: HIGH = Açık, LOW = Kapalı
  bool d1 = digitalRead(DOOR1_PIN);
  bool d2 = digitalRead(DOOR2_PIN);

  // Debounce kontrol
  if (d1 != door1_open) {
    if (millis() - door1_last_change_ms > DEBOUNCE_MS) {
      door1_open = d1;
      door1_last_change_ms = millis();
    }
  } else {
    door1_last_change_ms = millis();
  }

  if (d2 != door2_open) {
    if (millis() - door2_last_change_ms > DEBOUNCE_MS) {
      door2_open = d2;
      door2_last_change_ms = millis();
    }
  } else {
    door2_last_change_ms = millis();
  }
}

// NOT: Role artik SADECE ESP8266'nin RELAY_ON/RELAY_OFF komutlarıyla
// kontrol edilir (bkz handleSerialCommand). Eskiden burada kapı durumuna
// gore rolenin kendi basina karar veren bir updateRelay() vardi - bu,
// ESP8266'nin alarm modu/susturma/PIR/kacak/panik mantigini tamamen
// bypass ediyordu (kapı acikken "sustur" komutu gonderilse bile role
// bir sonraki loop'ta tekrar acikiyordu). Karar tamamen ESP8266'da.

// ============================================================
// SERİ HABERLEŞME (Non-blocking - while döngüsü yok)
// ============================================================
// Her loop çağrısında tek karakter okunur ve biriktirilir.
// \n alındığında komut tamamlanmış sayılır ve işlenir.
// ESP8266'dan gelen her komutun sonuna \n eklenir.
String inputString = "";
void handleSerialCommand() {
  // Non-blocking: while döngüsü yok, for döngüsü ile mevcut karakterleri işle
  int avail = Serial.available();
  if (avail == 0) return;

  // Mevcut tüm karakterleri oku (yeni veri bekleme = non-blocking)
  for (int i = 0; i < avail; i++) {
    char inChar = (char)Serial.read();

    // Satır sonu \n: komut tamamlandı, işle
    if (inChar == '\n') {
      if (inputString.length() == 0) continue;  // Boş satır, yok say

      inputString.trim();

      // Komutları işle ve ACK gönder
      if (inputString == F("GET_STATUS")) {
        int moistureRawVal = analogRead(MOISTURE_ADC_PIN);
        float moisturePct = 100.0 - (moistureRawVal * 100.0 / 1023.0);
        if (moisturePct < 0) moisturePct = 0;
        if (moisturePct > 100) moisturePct = 100;
        String response = "D0=";
        response += door1_open ? '1' : '0';
        response += ",D1=";
        response += door2_open ? '1' : '0';
        response += ",RELE=";
        response += relay_active ? '1' : '0';
        response += ",LAMBA=";
        response += lamp_on ? '1' : '0';
        response += ",MOISTURE=";
        response += moisture_output ? '1' : '0';
        response += ",MOISTURE_RAW=";
        response += moistureRawVal;
        response += ",MOISTURE_PCT=";
        response += moisturePct;
        response += ",POLARITY=";
        response += (relayAktifSeviye == HIGH) ? '1' : '0';
        Serial.println(response);
      } else if (inputString == F("LAMBA_ON")) {
        digitalWrite(LAMBA_PIN, LAMBA_ON_STATE);
        lamp_on = true;
        Serial.println(F("ACK:LAMBA_ON"));
      } else if (inputString == F("LAMBA_OFF")) {
        digitalWrite(LAMBA_PIN, LAMBA_OFF_STATE);
        lamp_on = false;
        Serial.println(F("ACK:LAMBA_OFF"));
      } else if (inputString == F("RELAY_ON")) {
        digitalWrite(RELAY_PIN, relayAktifSeviye);
        relay_active = true;
        Serial.println(F("ACK:RELAY_ON"));
      } else if (inputString == F("RELAY_OFF")) {
        digitalWrite(RELAY_PIN, relayPasifSeviye());
        relay_active = false;
        Serial.println(F("ACK:RELAY_OFF"));
      } else if (inputString.startsWith(F("SET_RELAY_POLARITY:"))) {
        // Format: SET_RELAY_POLARITY:1 (HIGH=aktif) veya :0 (LOW=aktif)
        // EEPROM'a kalici yazilir - reflash gerekmeden donanim polaritesi
        // degistirilebilir (bkz relayPolariteKaydet aciklamasi).
        int val = inputString.substring(19).toInt();
        relayPolariteKaydet(val == 1);
        digitalWrite(RELAY_PIN, relay_active ? relayAktifSeviye : relayPasifSeviye());
        Serial.println(F("ACK:SET_RELAY_POLARITY"));
       } else if (inputString == F("MOISTURE_ON")) {
        digitalWrite(MOISTURE_PIN, MOISTURE_ON_STATE);
        moisture_output = true;
        Serial.println(F("ACK:MOISTURE_ON"));
      } else if (inputString == F("MOISTURE_OFF")) {
        digitalWrite(MOISTURE_PIN, MOISTURE_OFF_STATE);
        moisture_output = false;
        Serial.println(F("ACK:MOISTURE_OFF"));
      } else if (inputString.startsWith(F("PIN_MODE:"))) {
        // Genel GPIO: pin modu ayarla (OUTPUT, INPUT, INPUT_PULLUP)
        // Format: PIN_MODE:<pin>,<mod>  (örn: PIN_MODE:6,OUTPUT)
        String param = inputString.substring(9);
        int comma = param.indexOf(',');
        if (comma > 0) {
          int pin = param.substring(0, comma).toInt();
          String mod = param.substring(comma + 1);
          mod.toUpperCase();
          if (pin >= 2 && pin <= 19) {  // D2-D13 + A0-A5 (14-19) - A6/A7 hariç
            if (mod == F("OUTPUT")) {
              pinMode(pin, OUTPUT);
              Serial.println(F("ACK:PIN_MODE:OUTPUT"));
            } else if (mod == F("INPUT")) {
              pinMode(pin, INPUT);
              Serial.println(F("ACK:PIN_MODE:INPUT"));
            } else if (mod == F("INPUT_PULLUP")) {
              pinMode(pin, INPUT_PULLUP);
              Serial.println(F("ACK:PIN_MODE:INPUT_PULLUP"));
            } else {
              Serial.println(F("NACK:PIN_MODE:BAD_MOD"));
            }
          } else {
            Serial.println(F("NACK:PIN_MODE:BAD_PIN"));
          }
        } else {
          Serial.println(F("NACK:PIN_MODE:FORMAT"));
        }
      } else if (inputString.startsWith(F("PIN_WRITE:"))) {
        // Genel GPIO: output pin yaz
        // Format: PIN_WRITE:<pin>,<0/1>  (örn: PIN_WRITE:6,1)
        String param = inputString.substring(10);
        int comma = param.indexOf(',');
        if (comma > 0) {
          int pin = param.substring(0, comma).toInt();
          int val = param.substring(comma + 1).toInt();
          if (pin >= 2 && pin <= 19) {
            digitalWrite(pin, val ? HIGH : LOW);
            Serial.println(F("ACK:PIN_WRITE"));
          } else {
            Serial.println(F("NACK:PIN_WRITE:BAD_PIN"));
          }
        } else {
          Serial.println(F("NACK:PIN_WRITE:FORMAT"));
        }
      } else if (inputString.startsWith(F("PIN_READ:"))) {
        // Genel GPIO: input pin oku
        // Format: PIN_READ:<pin>  (örn: PIN_READ:6)
        int pin = inputString.substring(9).toInt();
        if (pin >= 2 && pin <= 19) {
          int val = digitalRead(pin);
          Serial.print(F("PIN:"));
          Serial.print(pin);
          Serial.print(F("="));
          Serial.println(val);
        } else {
          Serial.println(F("NACK:PIN_READ:BAD_PIN"));
        }
      } else if (inputString == F("PIN_READ_ALL")) {
        // Tüm yedek pinleri oku: D2-D13 + A0-A5
        Serial.print(F("PIN:2="));
        Serial.print(digitalRead(2));
        Serial.print(F(",3="));
        Serial.print(digitalRead(3));
        Serial.print(F(",4="));
        Serial.print(digitalRead(4));
        Serial.print(F(",5="));
        Serial.print(digitalRead(5));
        Serial.print(F(",6="));
        Serial.print(digitalRead(6));
        Serial.print(F(",7="));
        Serial.print(digitalRead(7));
        Serial.print(F(",8="));
        Serial.print(digitalRead(8));
        Serial.print(F(",9="));
        Serial.print(digitalRead(9));
        Serial.print(F(",10="));
        Serial.print(digitalRead(10));
        Serial.print(F(",11="));
        Serial.print(digitalRead(11));
        Serial.print(F(",12="));
        Serial.print(digitalRead(12));
        Serial.print(F(",13="));
        Serial.print(digitalRead(13));
        Serial.print(F(",A0="));
        Serial.print(digitalRead(A0));
        Serial.print(F(",A1="));
        Serial.print(digitalRead(A1));
        Serial.print(F(",A2="));
        Serial.print(digitalRead(A2));
        Serial.print(F(",A3="));
        Serial.print(digitalRead(A3));
        Serial.print(F(",A4="));
        Serial.print(digitalRead(A4));
        Serial.print(F(",A5="));
        Serial.println(digitalRead(A5));
      }

      // Buffer'ı temizle, yeni komuta hazırla
      inputString = "";
    } else if (inChar != '\r') {
      // Buffer overflow koruması: CMD_BUFFER_SIZE'ı aşma
      if (inputString.length() < CMD_BUFFER_SIZE) {
        inputString += inChar;
      }
    }
  }
}

void sendStatusPeriodic() {
  // Her 2 saniyede bir durum gönder (opsiyonel)
  if (millis() - last_status_send_ms >= STATUS_INTERVAL) {
    last_status_send_ms = millis();

    String status = "STATUS:D0=";
    status += door1_open ? '1' : '0';
    status += ",D1=";
    status += door2_open ? '1' : '0';
    status += ",RELE=";
    status += relay_active ? '1' : '0';

    Serial.println(status);
  }
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // Input oku
  readInputs();

  // Seri komutları dinle
  handleSerialCommand();

  // Periyodik status gönder (opsiyonel)
  // sendStatusPeriodic();

  delay(10);
}