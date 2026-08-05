# -*- coding: utf-8 -*-
import io

path = 'src/main.cpp'
with io.open(path, 'r', encoding='utf-8') as f:
    c = f.read()

# 1)
old1 = "if(ad=='ayarlar'){wifiDurumYukle();}}"
new1 = "if(ad=='ayarlar'){wifiKartYukle();}}"
if old1 in c:
    c = c.replace(old1, new1)
    print("[1] sekmeAc guncellendi")
else:
    print("[1] zaten guncellenmis veya bulunamadi")

# 2)
old2 = '  js += "function guncelle(d){'
fn = "function wifiKartYukle(){fetch('/wifi/kart').then(r=>r.text()).then(function(h){document.getElementById('wifiKartKutu').innerHTML=h;wifiDurumYukle();}).catch(function(e){document.getElementById('wifiKartKutu').innerHTML='<p class=muted>WiFi yuklenemedi</p>';});}"
new2_fn = '  js += "' + fn + '";\n' + '  js += "function guncelle(d){'
if old2 in c:
    c = c.replace(old2, new2_fn, 1)
    print("[2] wifiKartYukle fonksiyonu eklendi")
else:
    print("[2] function guncelle bulunamadi")

with io.open(path, 'w', encoding='utf-8') as f:
    f.write(c)

print("OK")
