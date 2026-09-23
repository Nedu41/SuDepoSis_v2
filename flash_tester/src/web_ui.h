// Flash test cihazinin web arayuzu - HMI panel temasi (koyu zemin, nokta-izgara,
// buyuk LED gostergesi). Sayfa /api'den 1 saniyede bir JSON cekip kendini
// gunceller, cip takilip cikarilirken canli izlenebilir.
#pragma once
#include <Arduino.h>

const char SAYFA_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="tr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Flash Test</title><style>
:root{--bg:#0e1419;--panel:#161d24;--line:#243039;--cyan:#22d3ee;--orange:#f59e0b;
--green:#22c55e;--red:#ef4444;--txt:#e2e8f0;--dim:#7d8b99}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--txt);
background-image:radial-gradient(var(--line) 1px,transparent 1px);
background-size:18px 18px;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;
display:flex;justify-content:center}
.wrap{width:100%;max-width:560px}
h1{font-size:15px;letter-spacing:.14em;text-transform:uppercase;color:var(--cyan);
margin:0 0 14px;font-weight:600}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;
padding:18px;box-shadow:0 2px 0 #0a0f13,inset 0 1px 0 #1f2a33;margin-bottom:12px}
.durum{display:flex;align-items:center;gap:14px}
.led{width:30px;height:30px;border-radius:50%;background:#2a3540;flex:none;
box-shadow:inset 0 2px 4px rgba(0,0,0,.6)}
.led.ok{background:var(--green);box-shadow:0 0 16px rgba(34,197,94,.65),inset 0 1px 2px rgba(255,255,255,.35)}
.led.err{background:var(--red);box-shadow:0 0 16px rgba(239,68,68,.65),inset 0 1px 2px rgba(255,255,255,.35)}
.sonuc{font-size:21px;font-weight:700;letter-spacing:.03em}
.sonuc.ok{color:var(--green)}.sonuc.err{color:var(--red)}
.not{color:var(--dim);font-size:12.5px;margin-top:6px;line-height:1.5}
.sat{display:flex;align-items:baseline;padding:7px 0;border-bottom:1px solid var(--line)}
.sat:last-child{border-bottom:0}
.eti{width:96px;flex:none;color:var(--dim);font-size:12.5px;letter-spacing:.05em}
.deg{font-family:ui-monospace,Consolas,monospace;font-size:14px;color:var(--txt)}
.deg.vurgu{color:var(--orange);font-weight:600}
.hex{font-family:ui-monospace,Consolas,monospace;font-size:12px;color:var(--dim);
word-break:break-all;line-height:1.7}
.alt{text-align:center;color:var(--dim);font-size:11.5px;letter-spacing:.06em}
</style></head><body><div class="wrap">
<h1>SPI NOR Flash Test</h1>

<div class="panel"><div class="durum">
<div class="led" id="led"></div>
<div><div class="sonuc" id="sonuc">baglaniyor...</div>
<div class="not" id="not"></div></div>
</div></div>

<div class="panel">
<div class="sat"><div class="eti">JEDEC ID</div><div class="deg vurgu" id="jedec">--</div></div>
<div class="sat"><div class="eti">URETICI</div><div class="deg" id="uretici">--</div></div>
<div class="sat"><div class="eti">KAPASITE</div><div class="deg vurgu" id="kap">--</div></div>
<div class="sat"><div class="eti">STATUS</div><div class="deg" id="sr">--</div></div>
<div class="sat"><div class="eti">SERI NO</div><div class="deg vurgu" id="seri">--</div></div>
<div class="sat"><div class="eti">ICERIK</div><div class="deg" id="icerik">--</div></div>
</div>

<div class="panel"><div class="eti" style="margin-bottom:8px">ADRES 0 - ILK 16 BAYT</div>
<div class="hex" id="hex">--</div></div>

<div class="alt">CS=5 &nbsp; MISO=19 &nbsp; MOSI=23 &nbsp; CLK=18 &nbsp;|&nbsp; WP#/HOLD# &rarr; 3V3</div>
</div><script>
async function yenile(){
 try{
  const r=await fetch('/api',{cache:'no-store'});const d=await r.json();
  const ok=d.saglam;
  document.getElementById('led').className='led '+(ok?'ok':'err');
  const s=document.getElementById('sonuc');
  s.textContent=ok?'SAGLAM':'CEVAP YOK';s.className='sonuc '+(ok?'ok':'err');
  document.getElementById('not').textContent=ok?'':
   'Cip olu, takili degil, ters takilmis veya WP#/HOLD# 3.3V\'a bagli degil.';
  document.getElementById('jedec').textContent=d.jedec;
  document.getElementById('uretici').textContent=ok?d.uretici:'--';
  document.getElementById('kap').textContent=ok?d.kapasite:'--';
  document.getElementById('sr').textContent=ok?d.status:'--';
  document.getElementById('seri').textContent=ok?d.seri:'--';
  document.getElementById('icerik').textContent=ok?d.icerik:'--';
  document.getElementById('hex').textContent=ok?d.hex:'--';
 }catch(e){}
}
yenile();setInterval(yenile,1000);
</script></body></html>)HTML";
