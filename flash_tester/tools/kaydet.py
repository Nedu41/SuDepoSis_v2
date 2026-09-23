#!/usr/bin/env python
"""Flash test cihazindan bir okuma yakalar ve PC'ye kalici olarak kaydeder.

Cipin fabrika seri numarasi (UID) anahtar olarak kullanilir - ayni cip
tekrar takildiginda "bu cipi daha once gorduk" diye uyarir. Boylece her
seferinde lehimleyip sokmek, kagit banta not yazmak gerekmez.

Kullanim:
    python kaydet.py COM8 "eski S3 karttan cikan"
    python kaydet.py --liste
"""
import json, os, sys, time

KAYIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "kayitlar.json")
KAYIT = os.path.normpath(KAYIT)

ALANLAR = {
    "JEDEC ID": "jedec", "Uretici": "uretici", "Kapasite": "kapasite",
    "Status": "status", "SERI NO": "seri", "Ilk 16 B": "ilk16",
    "ICERIK": "icerik", "SONUC": "sonuc",
}


def yukle():
    if os.path.exists(KAYIT):
        with open(KAYIT, encoding="utf-8") as f:
            return json.load(f)
    return []


def kaydet(veri):
    with open(KAYIT, "w", encoding="utf-8") as f:
        json.dump(veri, f, ensure_ascii=False, indent=2)


def yazdir(k):
    print("  Seri no  :", k.get("seri", "-"))
    print("  JEDEC    :", k.get("jedec", "-"))
    print("  Uretici  :", k.get("uretici", "-"))
    print("  Kapasite :", k.get("kapasite", "-"))
    print("  Icerik   :", k.get("icerik", "-"))
    print("  Sonuc    :", k.get("sonuc", "-"))
    print("  Not      :", k.get("not", "-"))
    print("  Tarih    :", k.get("tarih", "-"))


def liste():
    veri = yukle()
    if not veri:
        print("Henuz kayit yok.")
        return
    print("Kayitli %d cip:\n" % len(veri))
    for i, k in enumerate(veri, 1):
        print("[%d] %s  %s  %s" % (i, k.get("seri", "?"), k.get("kapasite", "?"),
                                   k.get("sonuc", "?")))
        print("    not: %s   (%s)" % (k.get("not", "-"), k.get("tarih", "-")))
    print("\nDosya:", KAYIT)


def oku(port, notu):
    import serial
    try:
        s = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print("PORT ACILAMADI:", e)
        return 1

    print("Okunuyor... (cipin takili oldugundan emin ol)")
    blok, t0 = {}, time.time()
    while time.time() - t0 < 40:
        sat = s.readline().decode("utf-8", "replace").strip()
        if not sat:
            continue
        if sat.startswith("---"):
            if "sonuc" in blok:
                break
            blok = {}
            continue
        if ":" in sat:
            etiket, _, deger = sat.partition(":")
            anahtar = ALANLAR.get(etiket.strip())
            if anahtar:
                blok[anahtar] = deger.strip()
    s.close()

    if "sonuc" not in blok:
        print("Okuma alinamadi - kart calisiyor mu, dogru port mu?")
        return 1

    if blok.get("sonuc") != "SAGLAM":
        print("\nSONUC: %s" % blok.get("sonuc"))
        print("Cip cevap vermiyor - kayit yapilmadi.")
        print("(olu, takili degil, ters takilmis veya WP#/HOLD# 3.3V'a bagli degil)")
        return 0

    seri = blok.get("seri", "")
    veri = yukle()

    eski = next((k for k in veri if k.get("seri") == seri and seri), None)
    if eski:
        print("\n*** BU CIP DAHA ONCE KAYDEDILMIS ***\n")
        yazdir(eski)
        if notu:
            eski["not"] = notu
            eski["tarih"] = time.strftime("%Y-%m-%d %H:%M")
            kaydet(veri)
            print("\n  -> not guncellendi:", notu)
        return 0

    blok["not"] = notu or "-"
    blok["tarih"] = time.strftime("%Y-%m-%d %H:%M")
    veri.append(blok)
    kaydet(veri)
    print("\n*** YENI CIP KAYDEDILDI (toplam %d) ***\n" % len(veri))
    yazdir(blok)
    print("\nDosya:", KAYIT)
    return 0


if __name__ == "__main__":
    if "--liste" in sys.argv:
        liste()
    else:
        port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
        notu = sys.argv[2] if len(sys.argv) > 2 else ""
        sys.exit(oku(port, notu))
