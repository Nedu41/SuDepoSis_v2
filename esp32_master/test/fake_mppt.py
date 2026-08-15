"""
Sahte MPPT - ESP32'nin RS232/PI30 (QPIGS) kodunu evde, gercek MPPT
olmadan test etmek icin. Bir USB-TTL adaptorunu ESP32'nin UART2
pinlerine (GPIO41=RX, GPIO40=TX, GND) DOGRUDAN baglayin - MAX3232'ye
GEREK YOK, cunku bu adaptorler zaten TTL (3.3V/5V) seviyesinde, ESP32
UART2 de TTL - aralarinda seviye donusumu gerekmiyor. MAX3232 sadece
GERCEK MPPT'nin RS232 (+-12V) seviyesini TTL'e cevirmek icin lazim.
(Eski MAX485 modulu sokuldukten sonra MPPT/MAX3232 GPIO40/41'e tasindi
- eskiden GPIO47/48 kullaniliyordu. 2026-08-15 saha loopback testinde
R1OUT/T1IN'in fiziksel kablolamada ters oldugu gorulup RX/TX GPIO
atamasi yazilimda degistirildi, bkz config.h.)

Baglanti: adaptor TX -> ESP32 GPIO41 (RX)
          adaptor RX -> ESP32 GPIO40 (TX)
          adaptor GND -> ESP32 GND

Kullanim: python fake_mppt.py COM<N>
"""
import sys
import serial

CRC_TABLE = [0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
             0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef]


def crc16(data: bytes) -> bytes:
    crc = 0
    for b in data:
        da = ((crc >> 8) & 0xff) >> 4
        crc = (crc << 4) & 0xffff
        crc ^= CRC_TABLE[da ^ (b >> 4)]
        da = ((crc >> 8) & 0xff) >> 4
        crc = (crc << 4) & 0xffff
        crc ^= CRC_TABLE[da ^ (b & 0x0f)]
    low = crc & 0xff
    high = (crc >> 8) & 0xff
    if low in (0x28, 0x0d, 0x0a):
        low += 1
    if high in (0x28, 0x0d, 0x0a):
        high += 1
    return bytes([high, low])


# Sahte QPIGS govdesi - alan sirasi gercek protokolle ayni (main.cpp
# mpptPoll() bu indekslere gore okuyor):
# 0 gridV 1 gridHz 2 acOutV(load_voltage) 3 acOutHz 4 apparentVA
# 5 activeW(load_power) 6 loadPct 7 busV 8 battV 9 chgA 10 SOC 11 tempC
# 12 pvA(pv_current) 13 pvV(pv_voltage) 14 sccBattV 15 dischgA
# 16 statusBits 17 fanOfs 18 eeprom 19 pvChargeW(pv_power)
# 20 status2 21 gridFeed 22 country 23 gridFeedW
FAKE_QPIGS_BODY = "230.0 50.0 230.0 50.0 0300 0280 010 380 26.40 015 077 0250 02.5 68.5 27.10 000 10100110 00 02 00350 000 0 00 0000"

# QMOD: tek karakter - "L" = Line (sebeke) modu
FAKE_QMOD_BODY = "L"

# QPIWS: 36 bit, hepsi "0" = uyari/ariza yok. Bir uyariyi test etmek icin
# istenen indeksi (0=a0..35=a35) '1' yapabilirsiniz, orn. a12 (aku dusuk alarmi).
FAKE_QPIWS_BODY = "0" * 36

# QPIRI: main.cpp mpptPoll() rf[7..17] indekslerini okuyor (bkz orada yorum).
FAKE_QPIRI_BODY = "230.0 21.7 230.0 50.0 21.7 5000 5000 24.0 23.0 21.0 28.8 27.0 0 030 060 0 0 2 6 01 1 00 00.0"

FAKE_RESPONSES = {
    b"QPIGS": FAKE_QPIGS_BODY,
    b"QMOD": FAKE_QMOD_BODY,
    b"QPIWS": FAKE_QPIWS_BODY,
    b"QPIRI": FAKE_QPIRI_BODY,
}


def build_response(body: str) -> bytes:
    payload = f"({body}".encode()
    crc = crc16(payload)
    return payload + crc + b"\r"


def main():
    if len(sys.argv) < 2:
        print("Kullanim: python fake_mppt.py COM<N>")
        sys.exit(1)
    port = sys.argv[1]
    ser = serial.Serial(port, 2400, bytesize=8, parity="N", stopbits=1, timeout=0.2)
    print(f"[fake-mppt] {port} 2400 8N1 dinleniyor... (Ctrl+C ile cik)")
    buf = bytearray()
    while True:
        chunk = ser.read(64)
        if chunk:
            buf.extend(chunk)
            if b"\r" in buf:
                line, _, rest = bytes(buf).partition(b"\r")
                buf = bytearray(rest)
                cmd_bytes = line[:-2]  # son 2 byte CRC
                recv_crc = line[-2:]
                calc_crc = crc16(cmd_bytes)
                ok = "OK" if calc_crc == recv_crc else f"CRC UYUSMUYOR (beklenen {calc_crc.hex()})"
                print(f"[fake-mppt] geldi: {cmd_bytes!r} CRC={recv_crc.hex()} [{ok}]")
                if cmd_bytes in FAKE_RESPONSES:
                    resp = build_response(FAKE_RESPONSES[cmd_bytes])
                    ser.write(resp)
                    print(f"[fake-mppt] cevap gonderildi ({cmd_bytes.decode()}): {resp!r}")


if __name__ == "__main__":
    main()
