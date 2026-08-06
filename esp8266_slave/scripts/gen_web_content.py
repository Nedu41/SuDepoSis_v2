# PlatformIO pre-build script: web/index.html ve web/app.js icerigini
# include/web_content.h icine PROGMEM raw-string sabiti olarak gomer.
#
# Amac: web arayuzunu firmware.bin'in icine gomup ayri "uploadfs" adimini
# (ve onunla birlikte cihazdaki kayitlar.csv/tuketim dosyalarinin
# uploadfs sirasinda silinmesi riskini) tamamen ortadan kaldirmak.
#
# NOT: Kaynak dosyalar bilincli olarak "data/" DEGIL "web/" klasorunde -
# PlatformIO'nun data/ klasoru uploadfs/buildfs hedefleriyle ozel anlamli;
# web/ kullanmak, birinin farkinda olmadan eski "uploadfs" aliskanligiyla
# bu klasoru cihaza yukleyip calisma zamani kayitlarini silmesini engeller.
#
# Web arayuzunu degistirmek icin web/index.html / web/app.js duzenleyin -
# bu script her "pio run" oncesi otomatik calisip include/web_content.h'i
# yeniden uretir, elle bir sey yapmaniz gerekmez.
import os

# PlatformIO "pre:" scriptleri SCons ile exec() edilir - __file__ tanimli
# olmayabilir. env.PROJECT_DIR (calisma dizini = esp8266_slave/) guvenilir.
Import("env")
ROOT = env["PROJECT_DIR"]
DATA_DIR = os.path.join(ROOT, "web")
OUT_PATH = os.path.join(ROOT, "include", "web_content.h")


def to_progmem(var_name, path):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    delim = "WEBUI"
    n = 0
    while ")" + delim + '"' in content:
        n += 1
        delim = "WEBUI%d" % n
    return 'static const char %s[] PROGMEM = R"%s(\n%s\n)%s";\n' % (
        var_name, delim, content, delim,
    )


def main():
    html_path = os.path.join(DATA_DIR, "index.html")
    js_path = os.path.join(DATA_DIR, "app.js")

    out = (
        "#pragma once\n"
        "// OTOMATIK URETILDI - scripts/gen_web_content.py tarafindan\n"
        "// data/index.html ve data/app.js icerigi ile her derlemede yeniden\n"
        "// olusturulur. Elle duzenlemeyin - kaynak dosyalari duzenleyin.\n\n"
    )
    out += to_progmem("INDEX_HTML", html_path)
    out += to_progmem("APP_JS", js_path)

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(out)

    print("[gen_web_content] %s uretildi (%d + %d bayt kaynak)" % (
        OUT_PATH, os.path.getsize(html_path), os.path.getsize(js_path),
    ))


main()
