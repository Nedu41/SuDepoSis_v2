// MAX485 modulunun ESP32-S3 tarafinin (DE/TX) saglikli calisip calismadigini
// multimetre/osiloskopla net gormek icin SUREKLI TX testi. Normal RS485
// trafigi (GET_STATUS pollu vb.) kisa/duzensiz patlamalar halinde oldugu
// icin DE pininde neyin "sabit takili" neyin "gercek sinyal" oldugunu ayirt
// etmek zordu. Bu firmware DE'yi surekli HIGH (verici modu) tutup GPIO38'den
// (DI) durmadan 0x55 (01010101) baytini gonderir.
//
// KULLANIM:
//   1) MAX485 modulu SOKULUYKEN: GPIO39 (DE) multimetreyle olc - net ~3.3V
//      sabit gormelisin (kod DE'yi surekli HIGH tutuyor, dalgalanma degil).
//   2) MAX485 modulune TAKIP tekrar olc: hala net ~3.3V mi, yoksa yine
//      belirsiz orta bir degerde mi (ornegin ~1.5-2V) takili kaliyor mu bak.
//      Ikinci durumda modulun DE/RE girisi ESP32 ciktisini yukleyip
//      cekemiyor demektir - ariza modulde.
//   3) Osiloskop/logic analizor varsa GPIO38 (DI) ucunda temiz bir
//      01010101 kare dalgasi (9600 baud) gormelisin.
//   4) Loopback: MAX485 modulunun A ve B ucunu birbirine kisa devre
//      yaparsan (RS485 half-duplex loopback), swSerial/Serial1 RX
//      tarafinda ayni 0x55 baytini geri okumalisin - bu asagida
//      loglanir. Gelmezse modulun alici (RO) tarafi da supheli demektir.
#include <Arduino.h>
#include "config.h"

static HardwareSerial RS485Trace(1);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== RS485 DE/TX iz surme testi ===");
    Serial.printf("UART1, RX=GPIO%d, TX=GPIO%d, DE=GPIO%d, %d baud\n",
                   RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_BAUDRATE);
    Serial.println("DE SUREKLI HIGH tutuluyor, GPIO38'den SUREKLI 0x55 gonderiliyor.");
    Serial.println("Multimetre: DE pini boşta ~3.3V net gormeli, module takiliyken de ayni kalmali.");
    Serial.println("A/B kisa devre loopback yaparsan asagida [RX] satirlari gorunur.");

    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, HIGH);  // surekli verici modu - sabit olcum icin

    RS485Trace.begin(RS485_BAUDRATE, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
}

void loop() {
    RS485Trace.write((uint8_t)0x55);

    while (RS485Trace.available()) {
        uint8_t c = RS485Trace.read();
        Serial.printf("[RX] 0x%02X\n", c);
    }

    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 2000) {
        lastBeat = millis();
        Serial.println("[gonderiliyor] 0x55 (surekli tekrar), DE=HIGH sabit");
    }
}
