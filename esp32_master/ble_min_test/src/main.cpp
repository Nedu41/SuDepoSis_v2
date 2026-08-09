// ============================================================
// TANI TESTI - cok basit, izole NimBLE peripheral (server)
// Ana firmwaredeki WiFi/RS485/MQTT/SPIFFS hicbiri yok - sadece BLE.
// Ayni cihaz adi/UUID'ler kullanildigi icin telefondaki mevcut uygulama
// (BLEDProject) hicbir degisiklik gerekmeden bunu test edebilir.
// h2zero/NimBLE-Arduino'nun kendi ornek kodunun standart deseni izlendi.
// ============================================================
#include <Arduino.h>
#include <NimBLEDevice.h>

#define DEVICE_NAME        "ESP32S3_Yonetici"
#define SERVICE_UUID        "4faac001-82ab-4dc1-9106-97217895d03a"
#define CHARACTERISTIC_UUID "3a200001-526b-4e01-9fa6-07217895d03a"

NimBLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    deviceConnected = true;
    Serial.println("[BLE] >>> Telefon BAGLANDI <<<");
    Serial.printf("[BLE] conn_handle=%d\n", desc->conn_handle);
  }
  void onDisconnect(NimBLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Baglanti koptu, yeniden yayinlaniyor");
  }
};

class CharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    std::string val = pChar->getValue();
    Serial.print("[BLE] Yazma alindi: ");
    Serial.println(val.c_str());
    pChar->setValue("ACK:TEST");
    if (deviceConnected) pChar->notify();
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n[BLE] Test basliyor...");

  NimBLEDevice::init(DEVICE_NAME);

  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  pCharacteristic->setCallbacks(new CharCallbacks());
  pCharacteristic->setValue("Hazir");
  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println("[BLE] Yayinda, telefon baglantisi bekleniyor...");
}

void loop() {
  static unsigned long sonLog = 0;
  if (millis() - sonLog > 5000) {
    sonLog = millis();
    Serial.printf("[BLE] durum: %s, heap=%d\n", deviceConnected ? "BAGLI" : "bekleniyor", ESP.getFreeHeap());
  }
  delay(10);
}
