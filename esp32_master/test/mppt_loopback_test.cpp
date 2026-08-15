// MAX3232 loopback (jumper) testi - GERCEK MPPT/PI30 PROTOKOLUNU TEST ETMEZ.
// DB9 konnektorde RS232 pin2 (RXD) ile pin3 (TXD) arasina jumper takin, boylece
// ESP32 TX(GPIO40) -> MAX3232 T1IN -> T1OUT -> [jumper] -> R1IN -> R1OUT -> ESP32 RX(GPIO41)
// (2026-08-15: bu testte R1OUT/T1IN'in fiziksel kablolamada ters oldugu
// gorulup RX/TX GPIO atamasi config.h'da degistirildi - asagidaki kod
// hep MPPT_UART_RX_PIN/TX_PIN makrolarini kullandigi icin otomatik dogru.)
// yoluyla gonderdigimiz veri kendimize geri donuyor mu diye bakiyoruz - bu
// MAX3232'nin ve TTL taraf kablolamasinin elektriksel olarak calistigini dogrular.
#include <Arduino.h>
#include "config.h"

static HardwareSerial LoopSerial(MPPT_UART_NUM);
static const char TEST_MSG[] = "TEST1234\r\n";
static const size_t TEST_MSG_LEN = sizeof(TEST_MSG) - 1;

static uint32_t lastSendMs = 0;
static uint32_t sendPendingSinceMs = 0;
static size_t matchedCount = 0;
static bool waitingEcho = false;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== MAX3232 loopback testi ===");
    Serial.printf("UART%d, RX=GPIO%d, TX=GPIO%d, %d baud\n",
                   MPPT_UART_NUM, MPPT_UART_RX_PIN, MPPT_UART_TX_PIN, MPPT_BAUDRATE);
    Serial.println("DB9 pin2-pin3 (RXD-TXD) arasina jumper takili oldugundan emin olun.");
    LoopSerial.begin(MPPT_BAUDRATE, SERIAL_8N1, MPPT_UART_RX_PIN, MPPT_UART_TX_PIN);
}

void loop() {
    uint32_t now = millis();

    if (!waitingEcho && now - lastSendMs >= 2000) {
        while (LoopSerial.available()) LoopSerial.read(); // eski/gecikmis baytlari at
        LoopSerial.write((const uint8_t*)TEST_MSG, TEST_MSG_LEN);
        Serial.printf("[gonderildi] \"%s\"\n", "TEST1234");
        lastSendMs = now;
        sendPendingSinceMs = now;
        matchedCount = 0;
        waitingEcho = true;
    }

    while (waitingEcho && LoopSerial.available()) {
        uint8_t b = LoopSerial.read();
        Serial.printf("  [alindi] 0x%02X ('%c')\n", b, (b >= 32 && b < 127) ? (char)b : '.');
        if (b == (uint8_t)TEST_MSG[matchedCount]) {
            matchedCount++;
            if (matchedCount == TEST_MSG_LEN) {
                Serial.println("*** LOOPBACK OK - gonderilen veri aynen geri geldi ***");
                waitingEcho = false;
            }
        } else {
            matchedCount = 0; // esleme bozuldu, bastan ara
        }
    }

    if (waitingEcho && now - sendPendingSinceMs > 500) {
        Serial.println("*** CEVAP YOK (veya eksik/bozuk) - VCC=3.3V mi, R1OUT/T1IN<->GPIO40/41 dogru mu, jumper 2-3 pininde mi kontrol edin ***");
        waitingEcho = false;
    }
}
