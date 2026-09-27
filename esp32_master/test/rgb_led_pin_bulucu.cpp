// Kartin uzerindeki RGB LED'in GERCEKTEN hangi GPIO'ya bagli oldugunu GOZLE
// gormek icin - web arastirmasi karta gore (Espressif resmi v1.0=GPIO48,
// v1.1=GPIO38; YD-ESP32-S3 klonu=GPIO48 diyor ama kesin degil) celisiyordu.
// Bu test neopixelWrite() ile her aday pine sirayla KIRMIZI/YESIL/MAVI
// gonderir - LED hangi pin sirasinda yanarsa GERCEK pin odur (WS2812 basit
// HIGH/LOW ile yanmaz, ozel protokol ister - neopixelWrite bunu dogru
// pinde dogru zamanlamayla gonderir, LED baglı degilse pin sinyali bosa
// gider, zararsizdir).
//
// KULLANIM: Bu ortami flaslayip Serial Monitor'u ac (115200), kartin
// uzerindeki kucuk RGB LED'i izle - hangi "GPIOxx deneniyor" mesaji
// sirasinda LED yanarsa (kirmizi/yesil/mavi sirayla) o GPIO numarasini
// bana soyle.
#include <Arduino.h>

int adaylar[] = {38, 48, 45, 46, 21};
int adaySayisi = 5;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== RGB LED pin bulucu ===");
  Serial.println("Her aday pin sirayla 3sn KIRMIZI, 3sn YESIL, 3sn MAVI yanacak.");
  Serial.println("LED hangi anda tepki verirse (yanarsa) o pin GERCEK RGB pinidir.");
}

void loop() {
  for (int i = 0; i < adaySayisi; i++) {
    int pin = adaylar[i];
    Serial.print("\n>>> GPIO"); Serial.print(pin); Serial.println(" deneniyor - KIRMIZI");
    neopixelWrite(pin, 255, 0, 0);
    delay(3000);
    Serial.print(">>> GPIO"); Serial.print(pin); Serial.println(" - YESIL");
    neopixelWrite(pin, 0, 255, 0);
    delay(3000);
    Serial.print(">>> GPIO"); Serial.print(pin); Serial.println(" - MAVI");
    neopixelWrite(pin, 0, 0, 255);
    delay(3000);
    neopixelWrite(pin, 0, 0, 0);
    delay(1000);
  }
  Serial.println("\n=== Tur bitti, tekrar basliyor ===");
}
