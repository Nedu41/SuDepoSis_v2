# PlatformIO pre-build script: HER "pio run" cagrisinda (hangi kaynak dosya
# degismis olursa olsun) taze bir zaman damgasi ile include/build_info.h
# uretir. Amac: firmwareBuild alani __DATE__/__TIME__ ile main.cpp'ye
# baglanirsa SADECE main.cpp yeniden derlendiginde guncelleniyordu - baska
# bir dosyaya (orn. bahce_kapisi.cpp) yapilan degisiklikte eski tarih
# goruntuleniyor, bu da "flas gercekten uygulandi mi" sorusunu yanlis
# cevaplatiyordu (2026-09-17 kullanici sikayeti). Bu script HER derlemede
# calisir, dosya icerigi her seferinde degisir - main.cpp bu header'i
# include ettigi icin PlatformIO onu da her seferinde yeniden derler.
import os
from datetime import datetime

Import("env")
ROOT = env["PROJECT_DIR"]
OUT_PATH = os.path.join(ROOT, "include", "build_info.h")
VERSION_PATH = os.path.join(ROOT, "..", "VERSION")

ts = datetime.now().strftime("%b %d %Y %H:%M:%S")
try:
    with open(VERSION_PATH, "r", encoding="utf-8") as vf:
        version = vf.read().strip()
except FileNotFoundError:
    version = "0.0.0-unknown"

out = (
    "#pragma once\n"
    "// OTOMATIK URETILDI - scripts/gen_build_info.py tarafindan HER derlemede\n"
    "// yeniden uretilir. Elle duzenlemeyin. Surum icin repo kokundeki VERSION\n"
    "// dosyasini duzenleyin (buyuk degisikliklerde elle atanir).\n\n"
    '#define FIRMWARE_VERSION "%s"\n'
    '#define FIRMWARE_BUILD_TS "%s"\n' % (version, ts)
)

os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
with open(OUT_PATH, "w", encoding="utf-8") as f:
    f.write(out)

print("[gen_build_info] %s -> %s" % (OUT_PATH, ts))
