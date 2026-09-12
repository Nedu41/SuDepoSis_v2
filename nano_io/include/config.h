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
// D2/D3  → Bahçe kapısı tam-kapalı limit switch (INPUT_PULLUP) - Kapı1=SOL, Kapı2=SAĞ
// D4     → Alarm sireni (IRF520 MOSFET SIG, OUTPUT, HIGH=aktif)
// D5     → Nem rölesi (OUTPUT)
// D7     → Bahçe Kapı1 (SOL) tam açık switch (genel /pin/* API, pinKorumali())
// D8     → Yedek (eski durum LED)
// D9     → Bahçe Kapı2 (SAĞ) tam açık switch (genel /pin/* API, pinKorumali())
// D10    → Bahçe Kapısı zil butonu (genel /pin/* API, pinKorumali())
// D12    → Pasif buzzer (TONE_PLAY/TONE_STOP) - açılış melodisi, PIR ön-uyarı, zil
// D13    → Depo iç lamba rölesi (OUTPUT, sadece lamba)
// D6,D11 → Yedek GPIO (ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik)
// A0-A5  → Yedek GPIO (analog + digital I/O) - A1/A2 Bahçe Kapı1(SOL)/Kapı2(SAĞ) akım sensörü
// A6-A7  → Yedek (sadece analog input)
// ============================================================

// Kapı Sensörleri (Input, PULLUP)
#define DOOR1_PIN       2       // D2 - Bahçe Kapı1 (SOL) tam kapalı limit switch
#define DOOR2_PIN       3       // D3 - Bahçe Kapı2 (SAĞ) tam kapalı limit switch
// Bahçe kapısı "tam açık" limit switch'leri + zil butonu. Eskiden ESP8266
// bunları ayrı bir PIN_READ_ALL isteğiyle okuyordu; GET_STATUS yanıtına
// eklenerek o istek tamamen kaldırıldı (PIR'de uygulanan çözümün aynısı) -
// 9600 baud'da her istek ~90ms hat meşgul ediyor.
#define BAHCE_ACIK1_PIN 7       // D7 - Bahçe Kapı1 (SOL) tam açık limit switch
#define BAHCE_ACIK2_PIN 9       // D9 - Bahçe Kapı2 (SAĞ) tam açık limit switch
#define BAHCE_ZIL_PIN   10      // D10 - Bahçe kapısı zil butonu

// Alarm Sireni (Output) - IRF520 MOSFET SIG, HIGH = aktif (low-side switch, kontak yok)
#define RELAY_PIN       4       // D4 - Alarm Sireni (IRF520 MOSFET)

// PIR Hareket Sensörü (Input) - GET_STATUS yanitina dogrudan dahil edilir
#define PIR_PIN         6       // D6 - PIR sensörü (HIGH = hareket)

// Yedek GPIO pinleri (ESP'den dinamik yapılandırılır)
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
#define SERIAL_BAUD     9600    // Seri haberleşme hızı
#define CMD_BUFFER_SIZE 32      // Seri buffer boyutu

// ============================================================
// I/O Mantığı
// ============================================================

// NOT: Bu degerler artik sadece ILK ACILIS varsayilanidir (EEPROM bos ise
// kullanilir). Cikisin gercek polaritesi calisma zamaninda SET_RELAY_POLARITY
// komutuyla degistirilebilir ve EEPROM'da kalici kalir - reflash GEREKMEZ.
// Bkz main.cpp relayPolariteYukle()/relayPolariteKaydet().
// 2026-09-08: Role (NC, LOW=aktif) yerine IRF520 MOSFET (SIG, HIGH=aktif) -
// low-side switch'te kontak/NC kavrami yok, varsayilan HIGH'a cevrildi.
#define RELAY_ACTIVE    HIGH
#define RELAY_INACTIVE  LOW

// Lamba: HIGH = Açık, LOW = Kapalı
#define LAMBA_ON_STATE  HIGH
#define LAMBA_OFF_STATE LOW

// Nem kontrol rölesi: sahada ters polarite doğrulandı (2026-09-06) - LOW = Açık, HIGH = Kapalı
#define MOISTURE_ON_STATE   LOW
#define MOISTURE_OFF_STATE  HIGH

#endif // CONFIG_H
