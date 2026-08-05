// ============================================================
// SuDepoSis v2 - Arduino Nano I/O Configuration
// ============================================================
#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// PIN TANIMLAMALARI
// ============================================================

// ============================================================
// PIN HARİTASI (v2 - 2026 planı)
// D0/D1  → ESP8266 seri haberleşme
// D2/D3  → Kapı sensörleri (INPUT_PULLUP)
// D4     → Alarm rölesi (OUTPUT, NC - LOW=aktif)
// D5     → Nem rölesi (OUTPUT)
// D8     → Yedek (eski durum LED)
// D13    → Depo iç lamba rölesi (OUTPUT, sadece lamba)
// D6,D7,D9,D10,D11,D12 → Yedek GPIO (ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik)
// A0-A5  → Yedek GPIO (analog + digital I/O)
// A6-A7  → Yedek (sadece analog input)
// ============================================================

// Kapı Sensörleri (Input, PULLUP)
#define DOOR1_PIN       2       // D2 - Sol Kapı Sensörü
#define DOOR2_PIN       3       // D3 - Sağ Kapı Sensörü

// Alarm Rölesi (Output) - NC (Normally Closed), LOW = aktif
#define RELAY_PIN       4       // D4 - Alarm Rölesi

// Yedek GPIO pinleri (ESP'den dinamik yapılandırılır)
// D6=6 (PIR sensörü burada - ESP8266 PIN_READ:6 ile okur, Nano kodu degismez)
// D7=7, D8=8, D9=9, D10=10, D11=11, D12=12

// Depo İç Lamba Rölesi (Output)
#define LAMBA_PIN       13      // D13 - Depo içi lamba rölesi

// Nem kontrolü için Nano çıkışı
#define MOISTURE_PIN    5       // D5 - Toprak nem kontrol rölesi

// Toprak nem sensörü (analog input)
#define MOISTURE_ADC_PIN A0     // A0 - Toprak nem sensörü (analog)

// ============================================================
// ZAMAN SABİTLERİ
// ============================================================

#define DEBOUNCE_MS     50      // Kapı debounce süresi (ms)
#define STATUS_INTERVAL 2000    // Periyodik status gönderme aralığı (ms)
#define SERIAL_BAUD     9600    // Seri haberleşme hızı
#define CMD_TIMEOUT_MS  100     // Seri komut timeout (ms)
#define CMD_BUFFER_SIZE 32      // Seri buffer boyutu

// ============================================================
// I/O Mantığı
// ============================================================

// Kapı sensörü: HIGH = Açık, LOW = Kapalı (PULLUP)
#define DOOR_OPEN   HIGH
#define DOOR_CLOSED LOW

// NOT: Bu degerler artik sadece ILK ACILIS varsayilanidir (EEPROM bos ise
// kullanilir). Rolenin gercek polaritesi calisma zamaninda SET_RELAY_POLARITY
// komutuyla degistirilebilir ve EEPROM'da kalici kalir - reflash GEREKMEZ.
// Bkz main.cpp relayPolariteYukle()/relayPolariteKaydet().
#define RELAY_ACTIVE    LOW
#define RELAY_INACTIVE  HIGH

// Lamba: HIGH = Açık, LOW = Kapalı
#define LAMBA_ON_STATE  HIGH
#define LAMBA_OFF_STATE LOW

// Nem kontrol rölesi: HIGH = Açık, LOW = Kapalı
#define MOISTURE_ON_STATE   HIGH
#define MOISTURE_OFF_STATE  LOW

#endif // CONFIG_H
