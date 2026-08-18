// MAX3232 T1OUT hattinin RJ45/MPPT icinde nerede koptugunu bulmak icin
// SUREKLI TX testi. Normal QPIGS sorgusu 5sn'de bir ~40ms'lik kisa bir
// patlama gonderiyor, elle multimetreyle yakalamak zor. Bu firmware
// GPIO40'tan (T1IN) durmadan 0x55 (01010101) baytini gonderir - MAX3232
// T1OUT ucunda surekli tekrar eden bir kare dalgaya yakin sinyal olusur,
// boylece kabloyu CN1'den RJ45 soketine kadar multimetre/osiloskopla
// takip edip sinyalin nerede kayboldugu bulunabilir.
// NOT: Bu sadece GIDEN yonu (ESP32->MPPT) test eder, R1IN/RX tarafina
// dokunmaz - o taraf zaten calistigi onceki testlerde kanitlandi.
#include <Arduino.h>
#include "config.h"

static HardwareSerial TraceSerial(MPPT_UART_NUM);

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("=== MPPT TX iz surme testi ===");
    Serial.printf("UART%d, TX=GPIO%d, %d baud - SUREKLI 0x55 gonderiliyor\n",
                   MPPT_UART_NUM, MPPT_UART_TX_PIN, MPPT_BAUDRATE);
    Serial.println("Multimetre/osiloskopla T1OUT'tan RJ45 pin2'ye kadar kabloyu takip edin.");
    Serial.println("Sinyalin kayboldugu nokta kopuklugun yeri.");
    TraceSerial.begin(MPPT_BAUDRATE, SERIAL_8N1, MPPT_UART_RX_PIN, MPPT_UART_TX_PIN);
}

void loop() {
    TraceSerial.write((uint8_t)0x55);
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat > 3000) {
        lastBeat = millis();
        Serial.println("[gonderiliyor] 0x55 (surekli tekrar)");
    }
}
