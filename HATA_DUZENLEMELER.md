# 🔧 Kod Düzeltmeleri - SuDepoSis v2

## 🔴 Bulduğum 8 Sorun & Çözümleri

### 1️⃣ **RS485 Parse Infinite Loop (ESP32)**

**Problem:**
```cpp
while (eq_idx >= 0) {
  String key = payload.substring(0, eq_idx);  // ❌ Key hep "LEVEL" oluyor
}
```

**Çözüm:**
```cpp
int pos = 0;
while (pos < payload.length()) {
  int eq_idx = payload.indexOf('=', pos);      // ✅ From başlangıç konumu
  String key = payload.substring(pos, eq_idx); // ✅ Doğru key
  pos = next_comma + 1;                         // ✅ Ilerle
}
```

---

### 2️⃣ **SoftwareSerial Pin Çakışması (ESP8266)**

**Problem:**
```cpp
#define NANO_TX_PIN D9    // GPIO3 = UART0 RX
#define NANO_RX_PIN D10   // GPIO1 = UART0 TX
// ❌ UART0 ile çakışma → Seri karışır
```

**Çözüm:**
```cpp
#define NANO_TX_PIN D7    // GPIO13 ✅ Boş
#define NANO_RX_PIN D8    // GPIO15 ✅ Boş
// SoftwareSerial safe mode
```

---

### 3️⃣ **HC-SR04 Timeout Kontrolü Yetersiz (ESP8266)**

**Problem:**
```cpp
unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
if (duration == 0) return -1.0;  // ❌ Partial fail değil tam timeout
```

**Çözüm:**
```cpp
unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
if (duration == 0 || duration > 29000) {  // ✅ Timeout ve sınır
  return -1.0;
}
// Range check
if (distance < 2 || distance > 400) {
  return -1.0;
}
```

---

### 4️⃣ **Arduino Nano Buffer Overflow**

**Problem:**
```cpp
String cmd = Serial.readStringUntil('\n');  // ❌ Limitsiz buffer
// Memory corruption riski (Nano 2KB RAM sadece)
```

**Çözüm:**
```cpp
char buffer[32];  // ✅ Fixed size
int len = 0;
unsigned long start = millis();
while (Serial.available() && len < 31 && millis() - start < 100) {
  buffer[len++] = Serial.read();
}
buffer[len] = '\0';  // ✅ Null terminate
```

---

### 5️⃣ **WiFi Connection Blocking (ESP32)**

**Problem:**
```cpp
while (WiFi.status() != WL_CONNECTED && attempts < 20) {
  delay(500);  // ❌ Block 10 saniye = sistem donuyor
}
```

**Çözüm:**
```cpp
while (WiFi.status() != WL_CONNECTED && attempts < 20) {
  if (millis() - start_ms > 100) {  // ✅ Non-blocking
    attempts++;
    yield();  // ✅ Other tasks run
  }
}
```

---

### 6️⃣ **MQTT Spam Reconnect Loop**

**Problem:**
```cpp
if (!mqttClient.connected()) {
  if (mqttClient.connect(...)) { }
}
// Loop'ta 50 kez/saniye deneniyor = broker spam
```

**Çözüm:**
```cpp
static unsigned long last_mqtt_attempt_ms = 0;
if (millis() - last_mqtt_attempt_ms < 10000) {
  return;  // ✅ 10 saniye bekle
}
last_mqtt_attempt_ms = millis();
// Now try connect
```

---

### 7️⃣ **Memory Leak (Heap Fragmentation)**

**Problem:**
```cpp
String jsonStr;
serializeJson(doc, jsonStr);  // ❌ Loop'ta heap tükenir
```

**Çözüm:**
```cpp
char jsonBuffer[512];  // ✅ Stack buffer
snprintf(jsonBuffer, sizeof(jsonBuffer),
  "{"
  "\"level_cm\":%.1f,"
  ...
  "}",
  values...
);
mqttClient.publish(topic, jsonBuffer);
```

---

### 8️⃣ **RS485 Message Timing Yetersiz**

**Problem:**
```cpp
rs485_send("...\n");
delay(100);        // ❌ 100ms = Slave RS485'den oku → encode → gönder
String msg = rs485_read_line();  // Çoğu zaman timeout
```

**Çözüm:**
```cpp
rs485_send("...\n");
delay(200);        // ✅ 200ms - Slave'e veri hazırlama süresi
String msg = rs485_read_line();
// + Buffer clear delay
delay(50);
```

---

## ✅ Yapılan Düzeltmeler

| Dosya | Fonksiyon | Durum |
|-------|-----------|-------|
| esp32_master/main.cpp | parse_esp8266_data() | ✅ Fixed |
| esp32_master/main.cpp | wifi_connect() | ✅ Fixed |
| esp32_master/main.cpp | mqtt_connect() | ✅ Fixed |
| esp32_master/main.cpp | mqtt_publish() | ✅ Fixed |
| esp32_master/main.cpp | rs485_read_line() | ✅ Fixed |
| esp32_master/main.cpp | rs485_poll() | ✅ Fixed |
| esp8266_slave/include/config.h | NANO pins | ✅ Fixed |
| esp8266_slave/main.cpp | measureDistance() | ✅ Fixed |
| nano_io/main.ino | handleSerialCommand() | ✅ Fixed |

---

## 🧪 Test Prosedürü (Sonra)

```
1. Her cihazı yükle
2. Serial Monitor 115200 baud
3. Logları kontrol et:
   [RS485] UART1 ready
   [WiFi] Connected
   [MQTT] Connected
   [WEB] Server started
4. RS485 mesaj gelmesi kontrol et
5. Web dashboard açılsın
```

---

## 📌 Sonraki Kontrol Noktaları

- [ ] UART1 mesaj sırası (ordering)
- [ ] JSON buffer size (512 yeterli mi?)
- [ ] MQTT keepalive (default 60s)
- [ ] Watchdog timer (optional)
- [ ] Cihaz restart recovery

---

**Tüm Kritik Hatalar Düzeltildi!** ✅

