#pragma once
// Kalburum ana web arayuzu (HTML/CSS/JS) - main.cpp'den ayrildi (2026-08-30, boyut/okunabilirlik).
// PROGMEM + send_P kullanimi icin main.cpp handleRoot() icindeki gerekce yorumuna bakin.
  static const char PAGE_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="tr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Kalburum - Merkez Kontrol</title>
<style>
:root{--bg:#f6f8fa;--card:#fff;--text:#1f2937;--muted:#6b7280;--border:#e5e7eb;--border-strong:#4f7fe0;--primary:#2563eb;--accent:#10b981;--warn:#f59e0b;--danger:#ef4444;--danger-bg:#ffebee;--danger-bg-t:rgba(255,235,238,.6);--shadow:0 1px 3px rgba(0,0,0,.1);--grid-dot:rgba(79,127,224,.12)}
@media(prefers-color-scheme:dark){:root{--bg:#0b1220;--card:#111827;--text:#e5e7eb;--muted:#9ca3af;--border:#374151;--border-strong:#4a72c0;--primary:#60a5fa;--accent:#34d399;--warn:#fbbf24;--danger:#f87171;--danger-bg:#3a2222;--danger-bg-t:rgba(58,34,34,.6);--shadow:0 1px 3px rgba(0,0,0,.4);--grid-dot:rgba(96,165,250,.16)}}
*{margin:0;padding:0;box-sizing:border-box}
/* Simulasyon panosu hissi (kullanici talebi, 2026-08-28): sayfa arka planina
   soluk nokta izgarasi - kontrol panosu/HMI ekranlarindaki gibi. */
body{font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif;background-color:var(--bg);background-image:radial-gradient(var(--grid-dot) 1.2px,transparent 1.2px);background-size:20px 20px;color:var(--text);padding:12px;-webkit-tap-highlight-color:transparent}
.container{max-width:1100px;margin:0 auto}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}
.header h1{font-size:20px}
.header .meta{font-size:12px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:12px;margin-bottom:16px}
.card{background:var(--card);border:2.5px solid var(--border-strong);border-radius:12px;padding:16px;box-shadow:var(--shadow)}
.card h3,.card summary{font-size:15px;font-weight:700;color:var(--text);margin-bottom:8px;letter-spacing:.2px}
.card summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px}
.card summary::-webkit-details-marker{display:none}
.card summary::before{content:'▸';display:inline-block;font-size:12px;color:var(--muted);transition:transform .15s}
.card[open]>summary::before{transform:rotate(90deg)}
.card.zone-sudepo{border-left:7px solid #3b82f6}
.card.zone-konteyner{border-left:7px solid var(--warn)}
/* Acilir-kapanir (accordion) kartlarin her biri kendi renginde sol kenarla
   ayirt edilsin (kullanici talebi - Genel/Dashboard'daki KPI kartlarindan
   DEGIL, Ayarlar ve benzeri sekmelerdeki <details class=card> ogelerinden
   istendi, bkz 2026-08-27 geri alma). zone-sudepo/zone-konteyner ayri bir
   :not() ile disarida birakiliyor - onlar zaten kendi (mavi/sari) rengini
   tasiyor, karisik renk almasinlar.*/
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(1){border-left:7px solid #3b82f6}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(2){border-left:7px solid #06b6d4}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(3){border-left:7px solid #14b8a6}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(4){border-left:7px solid #f97316}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(5){border-left:7px solid #84cc16}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(6){border-left:7px solid #8b5cf6}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(7){border-left:7px solid #ec4899}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(8){border-left:7px solid #6366f1}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(9){border-left:7px solid #64748b}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(10){border-left:7px solid #f43f5e}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(11){border-left:7px solid #eab308}
details.card:not(.zone-sudepo):not(.zone-konteyner):nth-of-type(12){border-left:7px solid #22c55e}
.subdet{margin-top:16px;border:2.5px solid var(--border-strong);border-radius:8px;padding:10px 12px}
.subdet summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:7px;font-size:14px;font-weight:700;color:var(--text)}
.subdet summary::-webkit-details-marker{display:none}
.subdet summary::before{content:'▸';display:inline-block;font-size:11px;color:var(--muted);transition:transform .15s}
.subdet[open]>summary::before{transform:rotate(90deg)}
.kpi{font-family:Consolas,'DejaVu Sans Mono','Courier New',monospace;font-size:26px;font-weight:700;letter-spacing:1px;display:inline-block;padding:4px 10px;border-radius:6px;background:#0a0f0a;color:#22ff66;text-shadow:0 0 2px rgba(34,255,102,.9),0 0 8px rgba(34,255,102,.55);border:1px solid #163d1e}
.kpi.danger{color:#ff3b3b;text-shadow:0 0 2px rgba(255,59,59,.9),0 0 8px rgba(255,59,59,.55);border-color:#611515;animation:pulse 1.2s infinite}
#kpi-cm.danger,#kpi-litre.danger{color:var(--danger)}
.kpi small{font-size:12px;color:var(--muted);font-weight:400}
.bar{background:var(--border);height:18px;border-radius:999px;overflow:hidden;margin-top:8px}
.bar>div{height:100%;background:linear-gradient(90deg,var(--primary),var(--accent));width:0%;transition:width .4s}
/* LED-bar doluluk gostergesi (kullanici talebi, 2026-08-28: klasik gradyan
   bar yerine panel-tarzi segmentli LED siradizi - alt segmentler kirmizi
   (kritik dusuk seviye), orta sarilar, ust yesiller - yakit gostergesi mantigi). */
.ledbar{display:flex;gap:3px;margin-top:10px;padding:6px;background:#0a0f0a;border-radius:6px;border:1px solid #163d1e}
.ledbar span{flex:1;height:16px;border-radius:2px;background:#16241a;transition:background .2s,box-shadow .2s}
.ledbar span.lit-red{background:#ff3b3b;box-shadow:0 0 6px rgba(255,59,59,.85)}
.ledbar span.lit-yellow{background:#ffcc00;box-shadow:0 0 6px rgba(255,204,0,.85)}
.ledbar span.lit-green{background:#22ff66;box-shadow:0 0 6px rgba(34,255,102,.85)}
.row{display:flex;gap:8px;margin-top:10px;flex-wrap:wrap}
/* Fiziksel panel butonu hissi (kullanici talebi, 2026-08-28: "butonlarda
   daha canli ve gercekci olsun") - ust parlak cizgi + alt golge ile kabartma,
   basinca (:active) gomulu/basik gorunum. Renk varyantlari background-color
   kullanir (background shorthand DEGIL) ki bu parlaklik katmani ustte kalsin. */
.btn{padding:10px 12px;border:1px solid rgba(0,0,0,.18);border-radius:9px;cursor:pointer;font-weight:600;flex:1;min-width:120px;background-image:linear-gradient(180deg,rgba(255,255,255,.32),rgba(255,255,255,0) 45%,rgba(0,0,0,.10) 100%);box-shadow:0 2px 0 rgba(0,0,0,.22),0 5px 10px rgba(0,0,0,.18),inset 0 1px 0 rgba(255,255,255,.35);transition:transform .08s ease,box-shadow .08s ease,filter .08s ease}
.btn:active{transform:translateY(2px);box-shadow:inset 0 2px 5px rgba(0,0,0,.35);filter:brightness(.93)}
.btn-primary{background-color:var(--primary);color:#fff}
.btn-accent{background-color:var(--accent);color:#fff}
.btn-danger{background-color:var(--danger);color:#fff}
.btn-warn{background-color:var(--warn);color:#fff}
.btn:disabled{opacity:.6;cursor:not-allowed}
.sysdot{width:15px;height:15px;border-radius:50%;display:inline-block}
.sysdot.normal{background:var(--accent);box-shadow:0 0 7px var(--accent)}
.sysdot.kritik{background:var(--warn);box-shadow:0 0 7px var(--warn)}
.sysdot.tehlike{background:var(--danger);box-shadow:0 0 9px var(--danger);animation:softblink 1.8s ease-in-out infinite}
@keyframes softblink{0%,100%{opacity:1}50%{opacity:.3}}
.led{display:inline-block;width:11px;height:11px;border-radius:50%;background:var(--border);vertical-align:middle;margin-right:3px;transition:background .15s,box-shadow .15s}
.led.on{background:var(--danger);box-shadow:0 0 6px var(--danger)}
.led.ok{background:var(--accent);box-shadow:0 0 6px var(--accent)}
.led.pending{background:var(--warn);box-shadow:0 0 6px var(--warn);animation:pulse 1.2s infinite}
/* Buyuk/"canli" LED varyanti (kullanici talebi, 2026-08-27: "gercek ledmis
   gibi") - Konteyner Sensorleri karti icin. .led ile AYNI .on/.ok/.pending
   sinif adlarini kullanir, sadece boyut+parlaklik/govde stilini degistirir -
   kaynak sirasi .led kurallarindan SONRA oldugundan (esit ozgullukte) kazanir. */
.led-big{width:17px;height:17px;box-shadow:inset 0 -2px 3px rgba(0,0,0,.35),0 1px 1px rgba(255,255,255,.2);background:radial-gradient(circle at 35% 30%,#7b8494,#374151 75%)}
.led-big.on{background:radial-gradient(circle at 35% 30%,#ffc2c2,var(--danger) 55%,#5c0e0e 100%);box-shadow:inset 0 -2px 3px rgba(0,0,0,.3),0 0 6px 2px var(--danger),0 0 14px 5px rgba(239,68,68,.55)}
.led-big.ok{background:radial-gradient(circle at 35% 30%,#c3ffdf,var(--accent) 55%,#053f21 100%);box-shadow:inset 0 -2px 3px rgba(0,0,0,.3),0 0 6px 2px var(--accent),0 0 14px 5px rgba(16,185,129,.55)}
.led-big.pending{background:radial-gradient(circle at 35% 30%,#ffe9b3,var(--warn) 55%,#5c3d0e 100%);box-shadow:inset 0 -2px 3px rgba(0,0,0,.3),0 0 6px 2px var(--warn),0 0 14px 5px rgba(251,191,36,.55);animation:pulse 1.2s infinite}
/* "Karta/gostergeye tikla, ilgili ayara git" kisayolu (kullanici talebi,
   2026-08-27) - tum kartlar/projelerde tutarli kullanilacak genel sinif. */
.tikla{cursor:pointer;transition:opacity .15s}
.tikla:hover{opacity:.72}
@keyframes pulse{0%{opacity:1}50%{opacity:.5}100%{opacity:1}}
/* Etiket:deger listeleri (Sudepo/ESP8266'daki .info p ile AYNI desen, tum
   projelerde tutarli olsun diye buraya da eklendi - kullanici talebi,
   2026-08-27). Etiket sabit genislikte, deger hemen yaninda sabit hizada
   durur - kenara yaslama DEGIL (once denendi, "cok uzak" diye reddedildi). */
.kv p{margin:6px 0;font-size:13px;display:flex;align-items:center;gap:6px}
.kv .info-label{min-width:150px;color:var(--muted);flex-shrink:0}
#alarm-banner{position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);z-index:1000;background:var(--danger-bg-t);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px);color:var(--danger);border:2px solid var(--danger);padding:20px 24px;border-radius:12px;font-weight:700;font-size:17px;text-align:center;animation:pulse 1.2s infinite;box-shadow:0 8px 30px rgba(0,0,0,.35);max-width:90vw;width:420px}
.table{width:100%;border-collapse:collapse;font-size:13px}
.table th,.table td{padding:8px 6px;border-bottom:1px solid var(--border);text-align:left}
.table th{color:var(--muted);font-weight:600}
.input{padding:10px 12px;border:1px solid var(--border);border-radius:8px;background:var(--card);color:var(--text);width:100%}
.sz-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px 12px;margin-top:8px}
.sz-label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}
.sz-radio{display:block;font-size:13px;padding:6px 0}
.sz-cbgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:6px 10px;margin-top:4px}
.sz-cbgrid label{display:flex;align-items:center;gap:5px;font-size:12px;font-weight:normal}
.sz-cbgrid input{width:auto}
.section{display:none}
.section.active{display:block}
.nav{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap;position:sticky;top:0;z-index:100;padding:8px 0;background:rgba(246,248,250,.75);backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px)}
@media(prefers-color-scheme:dark){.nav{background:rgba(11,18,32,.75)}}
.nav button{padding:8px 12px;border:1px solid var(--border);background:var(--card);color:var(--text);border-radius:8px;cursor:pointer}
.nav button.active{background:var(--primary);color:#fff;border-color:var(--primary)}
.alert{padding:10px;border-radius:8px;background:rgba(239,68,68,.15);color:var(--danger);border:1px solid rgba(239,68,68,.3);margin-bottom:10px}
.badge{display:inline-block;padding:4px 8px;border-radius:999px;background:rgba(16,185,129,.15);color:var(--accent);font-weight:700;font-size:12px}
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div>
      <h1>Kalburum Merkez Kontrol</h1>
      <div class="meta">ESP32-S3 Master • RS485 Hub</div>
    </div>
    <div style="text-align:right">
      <div class="meta" id="clock">--</div>
      <div class="meta" id="wifi-topbar">WiFi: -</div>
    </div>
  </div>

  <div id="alarm-banner" style="display:none"></div>

  <div class="nav">
    <button onclick="show('dashboard')" id="nav-dashboard" class="active">Genel</button>
    <button onclick="show('kontrol')" id="nav-kontrol">Kontrol</button>
    <button onclick="show('ayarlar')" id="nav-ayarlar">Ayarlar</button>
    <button onclick="show('kumanda')" id="nav-kumanda">Kumanda</button>
    <button onclick="show('invertor')" id="nav-invertor">İnvertör</button>
    <button onclick="show('bilgiler')" id="nav-bilgiler">Bilgiler</button>
  </div>

  <div id="dashboard" class="section active">
    <div class="card" style="padding:10px 16px;margin-bottom:12px">
      <div class="row" style="margin-top:0;align-items:center;gap:8px">
        <span class="sysdot normal" id="alarm-dot"></span>
        <b style="font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.4px">Alarmlar</b>
        <span id="alarm-text" style="font-size:12px;font-weight:700"></span>
      </div>
      <div id="hata-box" style="margin-top:4px;color:var(--warn);font-size:12px"></div>
      <details id="alarm-log-det" style="margin-top:4px">
        <summary id="alarm-log-summary" style="cursor:pointer;font-size:11px;color:var(--muted);list-style:none">▸ -</summary>
        <div id="alarm-log-list" style="margin-top:4px;font-size:11px;color:var(--muted);max-height:110px;overflow-y:auto">Yükleniyor...</div>
      </details>
    </div>
    <div class="grid">
      <div class="card tikla" style="grid-column:span 2" onclick="gitAyar('ayar-sudepo')">
        <div style="display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px">
          <h3 style="margin-bottom:0">Sudepo Zonu Sensörleri</h3>
          <div style="display:flex;gap:6px;flex-wrap:wrap">
            <span class="badge" id="esp8266-badge" style="font-size:11px;padding:2px 7px">ESP8266: Bekleniyor</span>
            <span class="badge" id="nano-badge" style="font-size:11px;padding:2px 7px">Nano: Bekleniyor</span>
          </div>
        </div>
        <div class="row" style="column-gap:16px;row-gap:16px;margin-top:16px;align-items:center;flex-wrap:wrap">
          <div><span class="led led-big" id="db8-kapi1"></span> Sol Kapı</div>
          <div><span class="led led-big" id="db8-kapi2"></span> Sağ Kapı</div>
          <div><span class="led led-big" id="db8-pir"></span> PIR</div>
          <div><span class="led led-big" id="db8-kacak"></span> Kaçak</div>
          <div><span class="led led-big" id="db8-role"></span> Siren</div>
          <div><span class="led led-big" id="db8-lamba"></span> Lamba</div>
          <div><span class="led led-big" id="db8-nemrl"></span> Nem</div>
          <span style="font-size:12px;color:var(--muted)">Toprak Nemi</span>
          <div class="kpi" id="db8-nem" style="font-size:20px;padding:4px 12px">--</div>
          <span style="font-size:12px;color:var(--muted)">Mod: <b id="db8-nem-mod" style="color:var(--text)">-</b></span>
        </div>
      </div>
      <div class="card" style="grid-column:span 2"><h3>Konteyner Sensörleri</h3>
        <div class="row" style="gap:16px;margin-top:0;align-items:center;flex-wrap:wrap">
          <div class="tikla" onclick="gitAyar('ayar-kz-pir')"><span class="led led-big" id="db-pir"></span> PIR</div>
          <div class="tikla" onclick="gitAyar('ayar-kz-swan')"><span class="led led-big" id="db-swan"></span> Swan PIR</div>
          <div class="tikla" onclick="gitAyar('ayar-kz-sensorAktif')"><span class="led led-big" id="db-kapi"></span> Kapı</div>
          <div class="tikla" onclick="gitAyar('ayar-kz-gazduman')"><span class="led led-big" id="db-duman"></span> Duman</div>
          <div class="tikla" onclick="gitAyar('ayar-kz-gazduman')"><span class="led led-big" id="db-gaz"></span> Gaz</div>
          <div class="tikla" onclick="gitAyar('ayar-kz-siren')"><span class="led led-big" id="db-siren"></span> Siren</div>
          <div class="tikla" onclick="gitKontrol('acil-lamba-btn')"><span class="led led-big" id="db-acillamba"></span> Acil Lamba</div>
        </div>
        <div class="row" style="margin-top:10px;gap:24px;font-size:12px;color:var(--muted)">
          <div>Gaz (MQ6): <b id="db-mq6">-</b></div>
          <div>Duman (GP2Y10): <b id="db-gp2y10">-</b></div>
        </div>
      </div>
      <div class="card tikla" onclick="gitAyar('ayar-sudepo')"><h3>Depo Doluluk</h3>
        <div style="display:flex;align-items:center;gap:14px;flex-wrap:wrap">
          <div class="kpi" id="kpi-pct">--</div>
          <div style="font-size:13px;color:var(--muted);line-height:1.7">
            <div>Seviye: <b id="kpi-cm" style="color:var(--text)">--</b></div>
            <div>Hacim: <b id="kpi-litre" style="color:var(--text)">--</b></div>
          </div>
        </div>
        <div class="ledbar" id="ledbar-depo"></div>
      </div>
      <div class="card tikla" onclick="gitAyar('ayar-anaguc')"><h3>Ana Güç</h3><div class="kpi" id="kpi-ana-guc">--</div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="ana-guc-durum">-</div></div>
      <div class="card"><h3>Dış Sıcaklık ve Nem</h3><div class="kpi" id="kpi-temp">--</div><div style="margin-top:6px;font-size:13px;color:var(--muted)">Nem: <b id="kpi-nem">--</b></div></div>
      <div class="card"><h3>Akü (MPPT)</h3><div class="kpi" id="kpi-batarya">--</div><small id="batarya-soc"></small><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="batarya-durum">-</div><div style="margin-top:6px;font-size:12px;color:var(--muted)">☀️ Güneş: <b id="batarya-pv">-</b> | 🔌 Tüketim: <b id="batarya-yuk">-</b></div><div style="font-size:12px;color:var(--muted)" id="batarya-kalan">-</div><div style="margin-top:8px"><button class="btn" style="font-size:11px;padding:4px 10px" onclick="show('invertor')">Tüm invertör detayları →</button></div></div>
      <div class="card"><h3>Yedek Akü</h3><div class="kpi" id="kpi-yedek-aku">--</div><div style="margin-top:8px;font-size:12px;color:var(--muted)" id="yedek-aku-durum">-</div></div>
    </div>
  </div>

  <div id="kontrol" class="section">
    <div class="card">
      <h3>Lamba</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">İki zonun da kendi lambası var - Sudepo'nunki tamamen elle, Konteyner'inki hem elle hem alarm/siren ile otomatik yanabilir (ikisi birbirini bastırmaz - alarm sürerken elle kapatsanız bile alarm onu yeniden yakar).</p>
      <div class="row">
        <button class="btn btn-primary" id="lamba-btn" onclick="toggleLamba()">Sudepo Zonu: Aç</button>
        <button class="btn btn-primary" id="konteyner-lamba-btn" onclick="toggleKonteynerLamba()">Konteyner Zonu: Aç</button>
      </div>
      <div id="lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="konteyner-lamba-sonuc" style="margin-top:4px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card" style="border:2px solid var(--danger)">
      <h3>🚨 Acil Durum Lambası</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">Ana güç düşük seviyeye inince (aşağıdaki eşikler) sadece bildirim gelir, lamba OTOMATİK açılmaz - gerek görürsen buradan manuel aç. Panik veya Konteyner alarmında ise otomatik/anında yanar.</p>
      <button class="btn" style="font-size:16px;padding:14px 20px;width:100%" id="acil-lamba-btn" onclick="toggleAcilLamba()">⚪ Acil Durum Lambası (Kapalı)</button>
      <div id="acil-lamba-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Alarm</h3>
      <p style="font-size:12px;color:var(--muted);margin-top:-4px">Aç/Kapat anahtarları her zon için AYRI - birini kapatmak sadece o zonun tetikleyicilerini (siren/lamba/Telegram) susturur. Alarm Modu ve Sustur ise ortak (Sesli/Sessiz/Onaylı ikisi için de aynı).</p>
      <div class="row">
        <button class="btn btn-accent" id="alarm-btn" onclick="toggleAlarm()">Sudepo Zonu: Alarmı Kapat</button>
        <button class="btn btn-accent" id="konteyner-alarm-btn" onclick="toggleKonteynerAlarm()">Konteyner Zonu: Alarmı Kapat</button>
      </div>
      <div class="row" style="margin-top:8px">
        <button class="btn btn-primary" onclick="kapiKontrol(1)">Kapıyı Aç/Kapat</button>
      </div>
      <div class="row" style="margin-top:8px">
        <select id="alarm-mod-sel" onchange="setAlarmMod()">
          <option value="1">1 - Sesli</option>
          <option value="2">2 - Sessiz</option>
          <option value="3">3 - Onaylı</option>
        </select>
        <button class="btn btn-warn" id="alarm-mute-btn" onclick="alarmMute()">Sustur/Sireni Kapat</button>
      </div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">Onay bekleyen bir tetiklenme olursa "Sesli"/"Sessiz" secenekleri ekranin ustundeki uyari kutusunda (hangi sekmede olursan ol) cikar.</p>
      <div id="alarm-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="konteyner-alarm-sonuc" style="margin-top:4px;font-size:12px;color:var(--muted)"></div>
    </div>

    <div class="card">
      <h3>Panik</h3>
      <div class="row">
        <button class="btn btn-danger" id="panic-btn" onclick="togglePanic()">Panik</button>
      </div>
      <div id="panic-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </div>
  </div>

  <div id="ayarlar" class="section">
    <details class="card">
      <summary>OTA Güncelleme</summary>
      <p style="font-size:12px;color:var(--muted)">GitHub'daki en son firmware'i indirip yazar (main dalı).</p>
      <div class="row">
        <button class="btn btn-primary" onclick="otaGuncelle()">GitHub'dan Güncelle</button>
      </div>
      <div id="ota-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div class="row" style="margin-top:10px">
        <form id="otaDosyaForm" method="POST" action="/update" enctype="multipart/form-data" onsubmit="return otaDosyaOnay()">
          <input type="file" id="otaDosya" name="update" accept=".bin" required>
          <button class="btn btn-primary" type="submit">Dosyadan Yukle</button>
        </form>
      </div>
      <div id="ota-dosya-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>ESP8266 Firmware Deposu</summary>
      <p style="font-size:12px;color:var(--muted)">Bahcede internet olmadigindan, ESP8266'nin "URL'den Guncelle" kutusuna GitHub yerine buradaki adresi yaz - ikisi ayni WiFi agindayken calisir. ESP8266'nin web arayuzu artik firmware'in icine gomulu oldugu icin tek dosya (esp8266.bin) yeterli.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="fw-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <input type="file" id="fwDosya" accept=".bin">
        <button class="btn btn-primary" onclick="firmwareYukle()">Yukle</button>
      </div>
      <div id="fw-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Kayit Yedekleme</summary>
      <p style="font-size:12px;color:var(--muted)">ESP8266'nin kayitlar.csv dosyasinin yedegi - donanim arizasi/factory reset gibi durumlarda buradan geri yukleyebilirsin.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="yedek-durum-kutu">Yukleniyor...</div>
      <div class="row">
        <button class="btn btn-primary" onclick="kayitYedekle()">Simdi Yedekle</button>
        <button class="btn btn-warn" onclick="kayitGeriYukle()">ESP8266'ya Geri Yukle</button>
      </div>
      <div id="yedek-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Hava Durumu / Yağmur Tahmini</summary>
      <p style="font-size:12px;color:var(--muted)">Sabit konum (bahçe) - internet varken (örn. telefon hotspot'u) otomatik çekilir. 7 günden eski tahmin dikkate alınmaz, o durumda sulama normal devam eder.</p>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="weather-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <button class="btn btn-warn" onclick="weatherKontrolEt()">Şimdi Kontrol Et</button>
      </div>
      <div id="weather-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      <div id="weather-haftalik" style="margin-top:10px;display:flex;gap:6px;flex-wrap:wrap"></div>
    </details>

    <details class="card">
      <summary>Telegram Bildirimleri <small id="sum-telegram" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted)">Alarm YENİ başladığında (panik, kapı, PIR, kaçak vb. - Kalburum/Konteyner PIR'ı dahil) Telegram'a bildirim gönderir - sadece bu cihazın o an interneti varsa (örn. hotspot bağlıyken) çalışır.</p>
      <div class="row">
        <button class="btn" id="telegram-ac-kapa-btn" onclick="telegramAcKapa()">Yükleniyor...</button>
        <button class="btn btn-primary" onclick="telegramTest()">Test Mesajı Gönder</button>
      </div>
      <div id="telegram-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card zone-sudepo" id="ayar-sudepo">
      <summary>🚰 Su Deposu Zonu - Ayarlar <small id="sum-sudepo" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted)">Karar/yürütme hâlâ ESP8266+Nano'da yapılır (RS485 gecikmesi olmadan tepki verir) - burası sadece tek panelden yönetebilmen için köprü. Sudepo.local aynı ayarları gösterir.</p>
      <div id="sz-yukleniyor" style="font-size:12px;color:var(--muted)">Yükleniyor...</div>
      <div id="sz-form" style="display:none">
        <h3>Kalibrasyon &amp; Eşikler</h3>
        <p style="font-size:11px;color:var(--muted);margin-top:-4px">Bir alanı değiştirip dışına tıklayınca otomatik kaydedilir.</p>
        <div class="sz-grid">
          <div><label class="sz-label">Boş Mesafe (cm)</label><input class="input" type="number" step="0.1" id="sz_bosMesafe" onchange="szKaydet()"></div>
          <div><label class="sz-label">Dolu Mesafe (cm)</label><input class="input" type="number" step="0.1" id="sz_doluMesafe" onchange="szKaydet()"></div>
          <div><label class="sz-label">Kapasite (L)</label><input class="input" type="number" step="1" id="sz_kapasite" onchange="szKaydet()"></div>
          <div><label class="sz-label">Depo Şekli</label><select class="input" id="sz_depoYatay" onchange="szKaydet()"><option value="1">Yatay</option><option value="0">Dikey</option></select></div>
          <div><label class="sz-label">Alarm Eşiği (%)</label><input class="input" type="number" step="1" id="sz_alarmYuzde" onchange="szKaydet()"></div>
          <div><label class="sz-label">Dolum Eşiği (L)</label><input class="input" type="number" step="1" id="sz_minDolumLitre" onchange="szKaydet()"></div>
          <div><label class="sz-label">Kaçak Eşiği (dk)</label><input class="input" type="number" step="1" id="sz_kacakEsikDakika" onchange="szKaydet()"></div>
        </div>

        <h3 style="margin-top:18px">Alarm Modu, Gece &amp; PIR</h3>
        <div style="margin:8px 0">
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod1" value="1" onchange="szKaydet()"> <b>1 - Sesli</b> (siren hemen çalışır)</label>
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod2" value="2" onchange="szKaydet()"> <b>2 - Sessiz</b> (siren çalışmaz, sadece bildirim)</label>
          <label class="sz-radio"><input type="radio" name="sz_alarmMod" id="sz_mod3" value="3" onchange="szKaydet()"> <b>3 - Onaylı</b> (onay verince sesli gibi çalışır)</label>
        </div>
        <p style="font-size:11px;color:var(--muted)">Bu mod Kalburum/Konteyner Zonu için de geçerlidir (ortak Alarm Modu) - Kontrol sekmesindeki seçici ile de değiştirilebilir.</p>
        <div class="sz-grid">
          <div><label class="sz-label">Gece Başlangıç (saat)</label><input class="input" type="number" min="0" max="23" id="sz_geceBaslangic" onchange="szKaydet()"></div>
          <div><label class="sz-label">Gece Bitiş (saat)</label><input class="input" type="number" min="0" max="23" id="sz_geceBitis" onchange="szKaydet()"></div>
          <div><label class="sz-label">PIR Pencere (sn)</label><input class="input" type="number" min="0" max="120" id="sz_pirPencereSaniye" onchange="szKaydet()"></div>
          <div><label class="sz-label">PIR Min. Tetiklenme</label><input class="input" type="number" min="1" max="8" id="sz_pirMinTetiklenme" onchange="szKaydet()"></div>
        </div>

        <details class="subdet" open>
          <summary>Sensörler - Aktif/Pasif</summary>
          <p style="font-size:12px;color:var(--muted)">Mod/zamandan bağımsız, her zaman geçerli genel anahtar - kablosuz/arızalı bir sensörü buradan tamamen devre dışı bırakabilirsiniz (Konteyner Zonu'ndaki sensör anahtarlarıyla aynı mantık).</p>
          <div class="sz-cbgrid" id="sz-grid-sensorEtkin"></div>
        </details>

        <details class="subdet">
          <summary>Zamana Bağlı Tetikleyiciler</summary>
          <p class="sz-label">Gündüz</p>
          <div class="sz-cbgrid" id="sz-grid-gunduz"></div>
          <p class="sz-label" style="margin-top:10px">Gece</p>
          <div class="sz-cbgrid" id="sz-grid-gece"></div>
        </details>

        <details class="subdet">
          <summary>Mod Senaryoları</summary>
          <p style="font-size:12px;color:var(--muted)">Her modu hangi sensörlerin tetikleyeceği (girdi) ve neyin çalışacağı (çıkış).</p>
          <p class="sz-label" style="margin-top:8px">Sesli - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-sesli-girdi"></div>
          <p class="sz-label" style="margin-top:6px">Sesli - Çıkış</p>
          <div class="sz-cbgrid" id="sz-grid-sesli-cikis"></div>
          <p class="sz-label" style="margin-top:10px">Sessiz - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-sessiz-girdi"></div>
          <p class="sz-label" style="margin-top:6px">Sessiz - Çıkış</p>
          <div class="sz-cbgrid" id="sz-grid-sessiz-cikis"></div>
          <p class="sz-label" style="margin-top:10px">Onaylı - Girdi</p>
          <div class="sz-cbgrid" id="sz-grid-onayli-girdi"></div>
        </details>

        <div id="sz-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      </div>
    </details>

    <details class="card zone-konteyner" open>
      <summary>👁️ Konteyner Zonu - Konteyner Alarm Ayarları <small id="sum-konteyner" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>

      <p class="sz-label">Canlı Durum</p>
      <div class="row" style="gap:16px;align-items:center">
        <div><span class="led" id="kz-pir"></span> HC505-1 PIR</div>
        <div><span class="led" id="kz-swan"></span> Swan PIR</div>
        <div><span class="led" id="kz-kapi"></span> Kapı</div>
        <div><span class="led" id="kz-duman"></span> Duman</div>
        <div><span class="led" id="kz-gaz"></span> Gaz (MQ6)</div>
        <div><span class="led" id="kz-alarm"></span> Yerel Uyarı (LED/Buzzer) <small id="kz-alarm-txt" style="color:var(--muted)"></small></div>
        <div><span class="led" id="kz-siren"></span> Siren</div>
        <div><span class="led" id="kz-lamba"></span> Lamba</div>
      </div>

      <p class="sz-label" style="margin-top:12px">Sensör Aktif/Pasif</p>
      <div class="row" style="gap:16px" id="ayar-kz-sensorAktif">
        <label><input type="checkbox" id="kz_pirEtkin" onchange="konteynerSensorAktifKaydet()"> HC505-1 PIR Aktif</label>
        <label><input type="checkbox" id="kz_kapiEtkin" onchange="konteynerSensorAktifKaydet()"> Kapı Aktif</label>
        <label><input type="checkbox" id="kz_swanEtkin" onchange="konteynerSensorAktifKaydet()"> Swan PIR Aktif</label>
        <label><input type="checkbox" id="kz_dumanEtkin" onchange="konteynerSensorAktifKaydet()"> Duman Sensörü Aktif</label>
        <label><input type="checkbox" id="kz_gazEtkin" onchange="konteynerSensorAktifKaydet()"> Gaz (MQ6) Alarmı Aktif</label>
      </div>
      <p style="font-size:11px;color:var(--muted);margin-top:4px">Henüz kablolanmamış/arızalı bir sensörü buradan pasif yapabilirsiniz - pasifken hâlâ yukarıdaki "Canlı Durum"da ham değeri görünür ama alarma/banner'a hiç katkı yapmaz.</p>

      <details class="subdet" style="margin-top:12px" id="ayar-kz-gazduman">
        <summary>Gaz (MQ6) &amp; Duman (GP2Y10)</summary>
        <div class="row" style="gap:16px;margin-top:6px">
          <div style="font-size:12px;color:var(--muted)">MQ6: <b id="kz-mq6">-</b></div>
          <div style="font-size:12px;color:var(--muted)">GP2Y10 (duman/toz): <b id="kz-gp2y10">-</b></div>
        </div>
        <label style="display:block;margin-top:8px"><input type="checkbox" id="kz_mq6Test" onchange="konteynerMq6TestKaydet()"> MQ6 Test Modu (ısıtıcıyı sürekli açık tutar)</label>
        <p style="font-size:11px;color:var(--warn);margin-top:2px">Test Modu AÇIKKEN MQ6 değeri her zaman canlıdır (10dk/60sn döngüsü atlanır) - gaz/çakmak testi için kullanın, test bitince KAPATIN (sürekli ısıtıcı pil tüketir). Reset sonrası otomatik kapanır.</p>
        <div class="sz-grid" style="margin-top:8px">
          <div><label class="sz-label">Gaz Alarm Eşiği (Volt)</label><input class="input" type="number" step="0.1" min="0.1" max="3.3" id="kz_gazEsik" onchange="konteynerGazAyarKaydet()"></div>
          <div><label class="sz-label">Duman Alarm Eşiği (Volt)</label><input class="input" type="number" step="0.1" min="0.1" max="3.3" id="kz_dumanEsik" onchange="konteynerDumanAyarKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--warn);margin-top:4px">Kalibrasyon potu olmadığından bu eşikler tahmini varsayılan - sahada gerçek bir gaz/duman kaynağıyla test edip gerekirse ayarlayın. Gaz ve duman alarmı panik gibi davranır: mod/susturma/tetik animasyonundan bağımsız anında tam güçle çalar (gecikme istenmez).</p>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">MQ6'ün ısıtıcısı artık adaptif: gündüz ana güç (solar) ≥26V ise sürekli açık; gece/düşük güçte yedek akü doluluğuna göre 3dk/6dk/10dk'da bir 60sn açılıp kapanıyor (akü zayıfken en konservatif 10dk). Gösterilen değer, döngü kapalıyken en fazla o döngü kadar eski olabilir. GP2Y10'un ısıtıcısı yok, her zaman canlı (2sn'de bir tazelenir).</p>
      </details>

      <details class="subdet" style="margin-top:8px" id="ayar-kz-pir">
        <summary>HC505-1 PIR Hassasiyet</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">HC-SR505'in potansiyometresi yok, tetiklenince çıkışı sabit ~6-12sn HIGH'ta kalır. "Hassasiyet" bu yüzden iki kademeli süre mantığıyla ayarlanır: her yeni harekette hemen 1sn'lik bir bip/LED darbesi verilir (henüz alarm değil); hareket kesintisiz "Onay Süresi" kadar sürerse GERÇEK alarm sayılır.</p>
        <div class="sz-grid" style="margin-top:6px">
          <div><label class="sz-label">Tutma Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirTutma" onchange="konteynerPirKaydet()"></div>
          <div><label class="sz-label">Onay Süresi (sn)</label><input class="input" type="number" min="1" max="120" id="kz_pirOnay" onchange="konteynerPirKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">Tutma Süresi: hareket bittikten sonra ne kadar daha aktif sayılsın. Onay Süresi: kesintisiz hareketin kaç saniye sonra GERÇEK alarma dönüşeceği.</p>
      </details>

      <details class="subdet" style="margin-top:8px" id="ayar-kz-swan">
        <summary>Swan PIR Zamanlama</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">Swan Quad PET PIR'in kendi donanımsal debounce/evcil-hayvan-bağışıklığı var, bu yüzden varsayılan olarak (0/0) kapı gibi ANINDA eskale eder. Sahada yanlış tetiklenme yaşarsanız buraya da HC505-1 PIR'daki gibi bir Tutma/Onay süresi girebilirsiniz - 0 bırakılırsa davranış değişmez.</p>
        <div class="sz-grid" style="margin-top:6px">
          <div><label class="sz-label">Tutma Süresi (sn)</label><input class="input" type="number" min="0" max="120" id="kz_swanTutma" onchange="konteynerSwanKaydet()"></div>
          <div><label class="sz-label">Onay Süresi (sn)</label><input class="input" type="number" min="0" max="120" id="kz_swanOnay" onchange="konteynerSwanKaydet()"></div>
        </div>
        <p style="font-size:11px;color:var(--muted);margin-top:4px">0/0 = anında eskale (varsayılan/eski davranış). Onay Süresi &gt; 0 girilirse, hareket kesintisiz o kadar sürmeden GERÇEK alarma dönüşmez.</p>
      </details>

      <details class="subdet" style="margin-top:8px" id="ayar-kz-siren">
        <summary>Siren Zamanlama</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">Siren, İlk Gecikme sonra kısa Atış/Aralık döngüsünü tetik bitene kadar tekrarlar (sürekli kesintisiz çalmaz - Sudepo Zonu ile aynı prensip). Panik/Gaz bu mantığı atlar (anında sürekli çalar). En fazla Max Süre kesintisiz çalarsa otomatik susturulur, bir sonraki YENİ tetiklenmeye kadar öyle kalır.</p>
        <div class="sz-grid" style="margin-top:6px">
          <div><label class="sz-label">İlk Gecikme (sn)</label><input class="input" type="number" min="0" max="120" id="kz_sirenGecikme" onchange="konteynerSirenAyarKaydet()"></div>
          <div><label class="sz-label">Atış Süresi (ms)</label><input class="input" type="number" min="50" max="5000" id="kz_sirenAtis" onchange="konteynerSirenAyarKaydet()"></div>
          <div><label class="sz-label">Atışlar Arası (sn)</label><input class="input" type="number" min="1" max="60" id="kz_sirenAralik" onchange="konteynerSirenAyarKaydet()"></div>
          <div><label class="sz-label">Max Süre (dk)</label><input class="input" type="number" min="1" max="30" id="kz_sirenMax" onchange="konteynerSirenAyarKaydet()"></div>
        </div>
      </details>

      <details class="subdet" style="margin-top:8px">
        <summary>Mod Senaryoları</summary>
        <p style="font-size:12px;color:var(--muted);margin-top:6px">Her modu (Sesli/Sessiz/Onaylı) hangi sensörlerin tetikleyeceğini (girdi) ve tetiklendiğinde neyin çalışacağını (çıkış) ayrı ayrı seçin - Sudepo Zonu'ndaki Mod Senaryoları ile aynı mantık. Gaz alarmı hariç - o her zaman mod'dan bağımsız anında/sürekli çalar.</p>
        <p class="sz-label" style="margin-top:8px">Sesli - Girdi</p>
        <div class="sz-cbgrid" id="kz-grid-sesli-girdi"></div>
        <p class="sz-label" style="margin-top:6px">Sesli - Çıkış</p>
        <div class="sz-cbgrid" id="kz-grid-sesli-cikis"></div>
        <p class="sz-label" style="margin-top:10px">Sessiz - Girdi</p>
        <div class="sz-cbgrid" id="kz-grid-sessiz-girdi"></div>
        <p class="sz-label" style="margin-top:6px">Sessiz - Çıkış</p>
        <div class="sz-cbgrid" id="kz-grid-sessiz-cikis"></div>
        <p class="sz-label" style="margin-top:10px">Onaylı - Girdi</p>
        <div class="sz-cbgrid" id="kz-grid-onayli-girdi"></div>
        <div id="kz-mod-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
      </details>

      <div id="kz-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>🔋 Batarya (MPPT) Koruma Ayarları <small id="sum-batkoruma" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Akü voltajı, MPPT şarj kontrolcüsünden ayrı bir RS232/PI30 hattı üzerinden okunur. Koruma AÇIK olduğunda, voltaj "Kesme" eşiğinin altına inince sulama rölesi ve Konteyner lambası otomatik kapatılır (siren/alarm etkilenmez); voltaj "Geri Yükleme" eşiğine çıkınca otomatik serbest kalır.</p>
      <p style="font-size:12px;color:var(--warn);margin-bottom:8px"><b>Not:</b> Register/ölçek değerleri sahada doğrulanana kadar koruma varsayılan olarak KAPALI kalır - önce Genel sekmesindeki "Akü" kartından gelen voltajı bir multimetreyle karşılaştırıp doğrulayın, sonra korumayı açın.</p>
      <div class="row">
        <button class="btn btn-primary" id="batarya-koruma-btn" onclick="bateryaKorumaToggle()">Korumayı Aç</button>
      </div>
      <div class="sz-grid" style="margin-top:10px">
        <div><label class="sz-label">Kesme Voltajı (V)</label><input class="input" type="number" step="0.1" min="0" id="batarya-kesme" onchange="bateryaEsikKaydet()"></div>
        <div><label class="sz-label">Geri Yükleme Voltajı (V)</label><input class="input" type="number" step="0.1" min="0" id="batarya-geri" onchange="bateryaEsikKaydet()"></div>
      </div>
      <div id="batarya-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card" id="ayar-anaguc">
      <summary>⚡ Ana Güç (24V) İzleme Eşikleri <small id="sum-anaguc" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Ana hat voltajı ADS1115 (I2C) ile izlenir. Eşiklerin altına inince SADECE bildirim (Telegram) gönderilir - hiçbir röle/lamba otomatik açılmaz. Acil Durum Lambasını Kontrol sekmesinden manuel açabilirsiniz.</p>
      <div class="sz-grid">
        <div><label class="sz-label">Düşük (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik1" onchange="anaGucEsikKaydet()"></div>
        <div><label class="sz-label">Kritik (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik2" onchange="anaGucEsikKaydet()"></div>
        <div><label class="sz-label">Acil (V)</label><input class="input" type="number" step="0.1" min="0" id="ana-guc-esik3" onchange="anaGucEsikKaydet()"></div>
      </div>
      <div id="ana-guc-esik-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>WiFi <small id="sum-wifi" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)" id="wifi-durum-kutu">Yükleniyor...</div>
      <div class="row">
        <select class="input" id="staSSIDSel"><option value="">Ağları tara...</option></select>
        <button class="btn btn-warn" onclick="wifiScan()">Ağları Tara</button>
      </div>
      <div class="row">
        <input class="input" id="staSSID" placeholder="Ya da elle SSID girin">
        <input class="input" id="staPASS" placeholder="Şifre" type="password">
      </div>
      <div class="row">
        <button class="btn btn-primary" onclick="wifiKaydet()">Bağlan &amp; Kaydet</button>
        <button class="btn btn-danger" onclick="wifiKaldir()">Kaldır</button>
      </div>
      <div id="wifi-sonuc" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Nem Ayarları <small id="sum-nem" style="margin-left:auto;font-weight:400;color:var(--muted)"></small></summary>
      <div style="margin-bottom:8px;font-size:13px;color:var(--muted)">
        Nem: <b id="settings-moisture-val">-</b> | Çıkış: <b id="settings-moisture-out">-</b> | Mod: <b id="settings-moisture-mod">-</b>
      </div>
      <div class="row">
        <button class="btn btn-primary" id="moisture-settings-toggle-btn" onclick="toggleMoisture()">Aç/Kapat</button>
        <button class="btn btn-accent" id="moisture-settings-auto-btn" onclick="toggleMoistureAuto()">Otomatik / Manuel</button>
      </div>
      <div style="margin-top:10px;">
        <label style="display:block;font-size:12px;color:var(--muted);">Alt Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-low" min="0" max="100" style="width:120px;display:inline-block;" value="0" onchange="setMoistureThresholds()">
        <label style="display:block;font-size:12px;color:var(--muted);margin-top:8px;">Üst Eşik (%)</label>
        <input class="input" type="number" id="moisture-settings-high" min="0" max="100" style="width:120px;display:inline-block;" value="0" onchange="setMoistureThresholds()">
      </div>
      <div id="moisture-settings-msg" style="margin-top:8px;font-size:12px;color:var(--muted)"></div>
    </details>

    <details class="card">
      <summary>Sistem</summary>
      <div class="row">
        <button class="btn btn-danger" onclick="restartSistem()">Yeniden Başlat</button>
      </div>
    </details>
  </div>

  <div id="kumanda" class="section">
    <details class="card zone-konteyner">
      <summary>📡 Konteyner Zonu - IR Kumanda</summary>
      <p style="font-size:12px;color:var(--muted);margin-bottom:8px">Konteynerdaki IR alıcıya kumanda tuşu tanımla - "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa bas, sonra hangi komutu çalıştıracağını seç. Birden fazla kumanda eklenebilir.</p>
      <div id="ir-liste" style="font-size:13px">Yükleniyor...</div>
      <div class="row" style="margin-top:10px">
        <button class="btn btn-primary" onclick="irOgrenBaslat()">➕ Yeni Tuş Öğren</button>
      </div>
      <div id="ir-ogren-durum" style="margin-top:8px;font-size:13px"></div>
    </details>
  </div>

  <div id="invertor" class="section">
    <div class="card">
      <h3 style="margin:0 0 10px">🔌 İnvertör (MPPT) — Tüm Değerler</h3>
      <p style="font-size:12px;color:var(--muted);margin-bottom:12px">RS232/PI30 üzerinden okunan tüm QPIGS/QMOD/QPIWS/QPIRI alanları — salt-okunur, buradan hiçbir ayar değiştirilmez. Bağlantı: <b id="inv-baglanti">-</b></p>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Durum</h4>
      <table class="table">
        <tr><td>Çalışma modu</td><td id="inv-mode">-</td></tr>
        <tr><td>Aktif uyarı/arıza</td><td id="inv-warn">-</td></tr>
        <tr><td>Yük çıkışı</td><td id="inv-load-on">-</td></tr>
        <tr><td>Şarj (genel/solar/AC)</td><td id="inv-charging">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Akü / Güneş / Yük (anlık)</h4>
      <table class="table">
        <tr><td>Akü voltajı</td><td id="inv-batt-v">-</td></tr>
        <tr><td>Akü SOC</td><td id="inv-soc">-</td></tr>
        <tr><td>Akü şarj akımı</td><td id="inv-batt-charge-a">-</td></tr>
        <tr><td>Akü deşarj akımı</td><td id="inv-batt-discharge-a">-</td></tr>
        <tr><td>PV voltajı</td><td id="inv-pv-v">-</td></tr>
        <tr><td>PV akımı</td><td id="inv-pv-a">-</td></tr>
        <tr><td>PV gücü</td><td id="inv-pv-w">-</td></tr>
        <tr><td>Yük voltajı (AC çıkış)</td><td id="inv-load-v">-</td></tr>
        <tr><td>Yük gücü (aktif/görünür)</td><td id="inv-load-w">-</td></tr>
        <tr><td>Yük yüzdesi</td><td id="inv-load-pct">-</td></tr>
        <tr><td>Kalan süre tahmini</td><td id="inv-kalan">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Şebeke / Sistem</h4>
      <table class="table">
        <tr><td>Şebeke voltajı/frekansı</td><td id="inv-grid">-</td></tr>
        <tr><td>AC çıkış frekansı</td><td id="inv-ac-hz">-</td></tr>
        <tr><td>BUS voltajı</td><td id="inv-bus-v">-</td></tr>
        <tr><td>Isı (kanat sıcaklığı)</td><td id="inv-temp">-</td></tr>
      </table>

      <h4 style="margin:14px 0 6px;font-size:13px;color:var(--muted)">Ayarlar / Anma Değerleri (QPIRI — 60sn'de bir okunur)</h4>
      <p style="font-size:11px;color:var(--muted);margin-bottom:6px">Bu bölümdeki alan sırası topluluk kaynaklarına dayanır, cihazın gerçek çıktısıyla henüz birebir doğrulanmadı — DOĞRULA.</p>
      <table class="table">
        <tr><td>Akü anma voltajı</td><td id="inv-ri-battv">-</td></tr>
        <tr><td>Akü yeniden şarj voltajı</td><td id="inv-ri-recharge">-</td></tr>
        <tr><td>Akü düşük (kesme) voltajı</td><td id="inv-ri-under">-</td></tr>
        <tr><td>Akü bulk voltajı</td><td id="inv-ri-bulk">-</td></tr>
        <tr><td>Akü float voltajı</td><td id="inv-ri-float">-</td></tr>
        <tr><td>Akü tipi</td><td id="inv-ri-type">-</td></tr>
        <tr><td>Maks. AC şarj akımı</td><td id="inv-ri-maxac">-</td></tr>
        <tr><td>Maks. şarj akımı</td><td id="inv-ri-maxchg">-</td></tr>
        <tr><td>Çıkış kaynağı önceliği</td><td id="inv-ri-outpri">-</td></tr>
        <tr><td>Şarj kaynağı önceliği</td><td id="inv-ri-chgpri">-</td></tr>
      </table>
    </div>
  </div>

  <div id="bilgiler" class="section">
    <details class="card zone-konteyner">
      <summary>ESP32 Master Pinout (Konteyner)</summary>
      <table class="table">
        <tr><th>Pin</th><th>GPIO</th><th>Modül</th><th>Fonksiyon / Bağlantı</th></tr>
        <tr><td>RX (UART1)</td><td>37</td><td>MAX485 RS485</td><td>RO (Alıcı)</td></tr>
        <tr><td>TX (UART1)</td><td>38</td><td>MAX485 RS485</td><td>DI (Verici)</td></tr>
        <tr><td>-</td><td>39</td><td>MAX485 RS485</td><td>DE/RE (Enable)</td></tr>
        <tr><td>UART0</td><td>1/3</td><td>Debug Serial</td><td>9600 baud (USB programlama/monitör)</td></tr>
        <tr><td>D4</td><td>4</td><td>IR Alıcı Modülü</td><td>OUT/sinyal ucu bu pine; VCC/GND ayrı (3.3V veya 5V modüle göre) besleme hattından</td></tr>
        <tr><td>D5</td><td>5</td><td>Kırmızı LED + Buzzer</td><td>İkisi PARALEL bu pine bağlı (pin tasarrufu) - LED'e seri direnç (~220-330Ω) şart, buzzer aktif tip olmalı (kendi osilatörü olan, doğrudan HIGH/LOW ile çalışan)</td></tr>
        <tr><td>-</td><td>6</td><td>GP2Y10 (duman/toz sensörü) analog çıkış (Vo)</td><td>ADC1 kanal 5 — 2026-08-20'de PIR2'den boşaltıldı</td></tr>
        <tr><td>D17</td><td>17</td><td>PIR HC-SR505 (Konteyner)</td><td>OUT ucu bu pine; VCC/GND sensörün kendi besleme uçlarından (mini tip, 3.3-5V) — eski GPIO6'dan buraya taşındı</td></tr>
        <tr><td>D7</td><td>7</td><td>Kapı Reed Switch</td><td>Bir ucu bu pine, diğer ucu GND'ye (dahili pull-up kullanılıyor, ek direnç gerekmez)</td></tr>
        <tr><td>D8</td><td>8</td><td>Alarm Sireni Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif (Sudepo Zonu'ndaki "Alarm Rölesi" ile aynı mantık)</td></tr>
        <tr><td>D9</td><td>9</td><td>Uyarı Lambası Rölesi</td><td>Röle modülünün IN ucu bu pine; varsayılan HIGH=aktif - siren ile birlikte VEYA Onaylı modda "Sessiz (Lamba)" onayında tek başına yanar</td></tr>
        <tr><td>RX (UART2)</td><td>41</td><td>MAX3232 (MPPT, RS232)</td><td>R1OUT ucu bu pine (MAX3232 çip pin adı - modül silkscreen'i "TX"/"RX" farklı yazabilir, işleve göre bağlayın) - MPPT şarj kontrolcüden akü/PV/yük verisi, ESP8266 hattından (UART1) tamamen ayrı. MPPT'nin portu RS485 DEĞİL, RS232 (bkz not) - MAX485 kullanılmaz. Eski MAX485 modülü söküldükten (2026-08-14) sonra bu pinlere taşındı.</td></tr>
        <tr><td>TX (UART2)</td><td>40</td><td>MAX3232 (MPPT, RS232)</td><td>T1IN ucu bu pine</td></tr>
        <tr><td>ADC1</td><td>2</td><td>Yedek Akü (gerilim bölücü)</td><td>3x12V paralel yedek akü bankasının voltajını izler — salt-okunur, hiçbir röleyi tetiklemez</td></tr>
        <tr><td>-</td><td>21</td><td>Serbest (kullanılmıyor)</td><td>2026-08-24: eski yedek akü şarj rölesi mantığı kaldırıldı (Schulzz PWM solar kontrolcü artık donanımsal yapıyor). 2026-08-28: Acil Durum Lambası da buradan Sarı RCA/GPIO12'ye taşındı, GPIO21 tamamen boşta</td></tr>
        <tr><td>-</td><td>12</td><td>Acil Durum Lambası (MOSFET tetikleme)</td><td>Sarı RCA üzerinden dışarı çıkar (2026-08-28) — manuel web butonu VEYA panik/Konteyner-alarm durumunda doğrudan/otomatik yanar, onay beklemez</td></tr>
        <tr><td>I2C SDA</td><td>36</td><td>AHT10 + ADS1115 (Sıcaklık/Nem, Ana Güç)</td><td>Konteyner'e özel, elle I2C protokolüyle okunur</td></tr>
        <tr><td>I2C SCL</td><td>42</td><td>AHT10 + ADS1115 (Sıcaklık/Nem, Ana Güç)</td><td>-</td></tr>
        <tr><td>ADC1</td><td>10</td><td>MQ6 (gaz sensörü, analog çıkış)</td><td>Eşik aşılınca panik gibi anında Konteyner alarmını tetikler (bkz Ayarlar → Konteyner Alarm Ayarları, Gaz Alarm Eşiği)</td></tr>
        <tr><td>-</td><td>16</td><td>MQ6 Güç Kontrolü (IRF520 MOSFET modülü)</td><td>2026-08-26: Adaptif döngü — gündüz ana güç ≥26V'da sürekli açık, gece/düşük güçte yedek akü doluluğuna göre 3dk/6dk/10dk'da bir 60sn açılıp kapanır (bkz main.cpp mq6EtkinCycleMs()) - IRF520 modülünün SIG ucu bu pine. Dış çıkışı SCART Pin 20 (Mavi/Beyaz).</td></tr>
        <tr><td>-</td><td>18</td><td>GP2Y10 (duman/toz sensörü) LED sürücü kontrol</td><td>MOSFET modülü üzerinden, ~320us darbe — Ayarlar → Konteyner Alarm Ayarları, Duman Alarm Eşiği</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>Not:</b> Reed switch'in ve Swan PIR'in "açık/kapalı" okuma yönü (HIGH=açık mı kapalı mı) kablolamaya göre ters olabilir - <code>/api/status</code>'taki <code>konteyner.kapi_acik</code> / <code>konteyner.swan_pir</code> alanlarından gerçek davranışı görüp gerekirse kod tarafında (main.cpp, <code>konteynerSensorleriOku()</code>) tek satır değiştirerek düzeltilir. Siren/Lamba röleleriniz aktif-LOW ise aynı şekilde <code>alarmLedGuncelle()</code>'daki <code>digitalWrite</code> satırları ters çevrilir. MPPT bağlantısı için adım adım kılavuz: <code>docs/mppt-baglanti-kilavuzu.html</code>; yedek akü kablolaması için: <code>docs/yedek-aku-baglanti-kilavuzu.html</code>.</p>
      <p style="font-size:12px;color:var(--muted);margin-top:4px"><b>Serbest/kullanılabilir GPIO'lar</b> (ileride yeni eklenti için): 21, 35, 43, 44, 47, 48. <b>Asla kullanılmaması gerekenler:</b> 0, 3, 45, 46 (strapping/boot pinleri), 26-32 (Quad Flash için ayrılmış).</p>
    </details>

    <details class="card zone-sudepo">
      <summary>ESP8266 Slave Pinout</summary>
      <table class="table">
        <tr><th>Pin (NodeMCU)</th><th>GPIO</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>D0</td><td>16</td><td>MAX485</td><td>DI (RS485 TX)</td></tr>
        <tr><td>D7</td><td>13</td><td>MAX485</td><td>RO (RS485 RX)</td></tr>
        <tr><td>D3</td><td>0</td><td>MAX485</td><td>DE/RE (Enable)</td></tr>
        <tr><td>D5</td><td>14</td><td>HC-SR04</td><td>TRIG</td></tr>
        <tr><td>D6</td><td>12</td><td>HC-SR04</td><td>ECHO</td></tr>
        <tr><td>D1</td><td>5</td><td>DS1307 RTC</td><td>SCL</td></tr>
        <tr><td>D2</td><td>4</td><td>DS1307 RTC</td><td>SDA</td></tr>
        <tr><td>RX (D9)</td><td>3</td><td>Arduino Nano</td><td>UART0 RX ← Nano TX</td></tr>
        <tr><td>TX (D10)</td><td>1</td><td>Arduino Nano</td><td>UART0 TX → Nano RX</td></tr>
        <tr><td>A0</td><td>ADC0</td><td>Toprak Nem</td><td>Analog nem sensörü</td></tr>
      </table>
    </details>

    <details class="card zone-sudepo">
      <summary>Arduino Nano IO (v2 Pin Planı)</summary>
      <table class="table">
        <tr><th>Pin</th><th>Modül</th><th>Fonksiyon</th></tr>
        <tr><td>D0 (RX)</td><td>ESP8266 TX</td><td>Seri haberleşme</td></tr>
        <tr><td>D1 (TX)</td><td>ESP8266 RX</td><td>Seri haberleşme</td></tr>
        <tr><td>D2</td><td>Kapı 1 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D3</td><td>Kapı 2 sensör</td><td>INPUT_PULLUP</td></tr>
        <tr><td>D4</td><td>Alarm rölesi</td><td>OUTPUT (varsayılan LOW=aktif, NC röle) - polarite web'den (/role/polarite) çalışırken de değiştirilip EEPROM'a kalıcı yazılabilir</td></tr>
        <tr><td>D5</td><td>Nem rölesi</td><td>OUTPUT</td></tr>
        <tr><td>D6</td><td>PIR HC-SR501 hareket sensörü</td><td>INPUT - ESP8266 PIN_READ:6 ile okur (Nano kodu değişmez)</td></tr>
        <tr><td>D7-D12</td><td>Yedek GPIO</td><td>ESP'den PIN_MODE/PIN_WRITE/PIN_READ ile dinamik</td></tr>
        <tr><td>D13</td><td>Depo iç lamba rölesi</td><td>OUTPUT (sadece lamba)</td></tr>
        <tr><td>A0-A5</td><td>Yedek GPIO</td><td>Analog + digital I/O</td></tr>
        <tr><td>A6-A7</td><td>Yedek</td><td>Sadece analog input</td></tr>
      </table>
      <p style="font-size:12px;color:var(--muted);margin-top:8px"><b>GPIO Komut Protokolü (ESP→Nano):</b> PIN_MODE:<pin>,<mod> | PIN_WRITE:<pin>,<0/1> | PIN_READ:<pin> | PIN_READ_ALL</p>
    </details>

    <details class="card">
      <summary>WiFi & Sistem</summary>
      <div id="bilgi-sistem" class="kv" style="font-size:13px">Yükleniyor...</div>
      <div id="build-info" style="font-size:12px;color:var(--muted);margin-top:8px">Yükleniyor...</div>
    </details>

    <details class="card">
      <summary>Güncelleme Aralıkları</summary>
      <div id="guncelleme-bilgi" class="kv" style="font-size:13px">Yükleniyor...</div>
      <p style="font-size:12px;color:var(--muted);margin-top:8px">SSE (anlık push): ESP8266/Nano'dan yeni veri gelir gelmez, en geç 1sn'de bir yedek olarak. Tarayıcı 5sn'de bir de yedek polling yapar (SSE koparsa fark edilmesin diye).</p>
    </details>

    <details class="card">
      <summary>Kullanım Kılavuzu</summary>
      <div style="font-size:13px;line-height:1.6">
        <p><b>Alarm Modları</b> (Kontrol → Alarm): 1-Sesli (tetiklenince siren hemen çalışır), 2-Sessiz (siren çalışmaz, sadece bu sayfada/ESP8266'da bildirim), 3-Onaylı (tetiklenince onay bekler, "Tetiklenmeyi Onayla" ile sesli moda geçer). Bu mod artık hem Sudepo Zonu hem Kalburum/Konteyner için ORTAK - Konteyner PIR'ı eskale olup gerçek alarma dönüştüğünde de aynı moda göre davranır (bkz Ayarlar → Konteyner Alarm Ayarları).</p>
        <p><b>Sustur/Sireni Kapat:</b> Alarm koşulu sürse bile röleyi susturur; koşul temizlenince otomatik sıfırlanır.</p>
        <p><b>Panik:</b> Tetikleyicilerden bağımsız, elle aç/kapat anahtarı gibi çalışır - röleyi zorla açık tutar. Konteyner'in siren+lambasını da (Alarm Modu/etkin-pasif durumundan bağımsız) doğrudan tetikler.</p>
        <p><b>Kapı/PIR/Kaçak/Düşük seviye</b> tetikleyicileri ve gündüz/gece + mod bazlı senaryolar ESP8266 panelinin "Alarm" sekmesinden ayarlanır (bu panel sadece görüntüler ve mod/susturma/onay/panik komutlarını iletir).</p>
        <p><b>WiFi:</b> Ayarlar sekmesinden ev ağını tarayıp kaydedebilirsiniz; AP (<code id="ag-ap-bilgi">-</code>) STA bağlantısından bağımsız her zaman açıktır, ağ ayarları NVS'de kalıcıdır.</p>
        <p><b>Hava Durumu / Yağmur Tahmini:</b> Bahçenin sabit konumu için haftalık tahmin, bu cihazın interneti olduğu anda (örn. telefon hotspotu bağlıyken) otomatik çekilip hafızada saklanır. Bahçede kalıcı internet olmadığı için tahmin bayatlayabilir - 7 günden eski ise dikkate alınmaz ve sulama normal devam eder. Yarın yağmur bekleniyorsa bugünkü sulama otomatik atlanır (su israfını önlemek için).</p>
        <p><b>Telegram Bildirimleri:</b> Bir alarm YENİ tetiklendiğinde (panik, kapı, PIR, kaçak, sensör hatası) sayfa açık olmasa bile telefona Telegram mesajı gider - yalnızca cihazın o an interneti varsa (hotspot bağlıyken) çalışır, aksi halde birkaç dakika tekrar denenir. Bot token/chat ID <code>secrets.h</code> içinde saklanır.</p>
        <p><b>Mimari:</b> Nano (kapı/röle/lamba/PIR/nem çıkışı) ⇄ ESP8266 (sensörler + web) ⇄ RS485 ⇄ ESP32 (bu panel, MQTT yayını). Nano firmware'i genel amaçlı GPIO komutlarıyla (PIN_MODE/WRITE/READ) çalıştığı için donanım eklemelerinde çoğunlukla yeniden flaşlanması gerekmez.</p>
        <p><b>Kumanda:</b> IR kumanda tuş eşleştirmesi ayrı "Kumanda" sekmesinde - "Yeni Tuş Öğren" ile başlayıp kumandada ilgili tuşa basılınca kod yakalanır, sonra hangi komutu çalıştıracağı seçilir.</p>
      </div>
    </details>

    <details class="card zone-sudepo">
      <summary>💧 Sudepo Su Akışı Kayıtları</summary>
      <p style="font-size:12px;color:var(--muted)">ESP8266'daki (Sudepo) su alım/tüketim kayıtları - buradaki liste en son "Yenile" ile çekilen yedeğin görüntüsüdür (canlı değil, RS485 üzerinden istek üzerine çekilir).</p>
      <div class="row">
        <button class="btn btn-primary" onclick="suAkisiYenile()">Yenile (ESP8266'dan çek)</button>
      </div>
      <div id="su-akisi-durum" style="margin-top:8px;font-size:12px;color:var(--muted)">Yükleniyor...</div>
      <div id="su-akisi-liste" style="margin-top:8px;font-size:12px;max-height:260px;overflow-y:auto">Yükleniyor...</div>
    </details>

    <details class="card">
      <summary>📋 Alarm Kayıtları (Günlük / Aylık)</summary>
      <p style="font-size:12px;color:var(--muted)">Genel sayfadaki "Son 5 Alarm" sadece en yeni 5 kaydı RAM'den gösterir - burası SPIFFS'teki kalıcı günlüğün (reboot'lara dayanıklı) tamamını okur.</p>
      <div class="row" style="gap:16px">
        <div style="flex:1;min-width:180px"><p class="sz-label">Günlük Özet</p><div id="alarm-log-gunluk" style="font-size:12px">Yükleniyor...</div></div>
        <div style="flex:1;min-width:180px"><p class="sz-label">Aylık Özet</p><div id="alarm-log-aylik" style="font-size:12px">Yükleniyor...</div></div>
      </div>
      <p class="sz-label" style="margin-top:12px">Tüm Kayıtlar</p>
      <div id="alarm-log-tam" style="font-size:12px;max-height:300px;overflow-y:auto">Yükleniyor...</div>
    </details>
  </div>
</div>

<script>
const $=s=>document.querySelector(s);

// === SERVER TRUTH MODEL ===
// Tüm state sunucudan gelir. Komut sonrası local state YOK.
// guncelle() her şeyi taze sunucu verisinden çizer.
// Butonlar sadece "işlem sürüyor" sırasında '...' gösterir,
// sonra guncelle() doğru metni yazar.

// Bir input "Kaydet" butonu olmadan onchange ile otomatik kaydedilince, kayit
// butonuna tiklamanin aksine input BLUR olur (odak kaybolur) - tam o anda
// gelen bir periyodik guncelleme (SSE/poll) sunucunun henuz eski degerini
// gosterip degeri bir an icin "eski hale donup tekrar gelmis" gibi
// gosterebilirdi (odak koruması artik gecerli degil). Bu, alani kisa bir
// sure (varsayilan 2.5sn) "yakinda kullanici tarafindan degistirildi" olarak
// isaretleyip o sure icinde sunucudan gelen degerle UZERINE YAZMAYI atlayan
// basit bir koruma.
const yakinDuzenlenenler = new Map(); // id -> koruma bitis zamani (ms)
function yakinDuzenlendi(id, ms=2500){ yakinDuzenlenenler.set(id, Date.now()+ms); }
function yakinKorumali(id){ const t=yakinDuzenlenenler.get(id); return !!t && Date.now()<t; }

// Alarm tetiklendiginde kisa bip - ESP8266 panelindeki ile ayni desen.
// Sadece "kapali -> acik" gecisinde calar, her renderUI'da degil.
let alarmOncekiDurum = false;
let sysDurumOncekiTehlike = false;
function bipSesi(){
  try{
    const ctx = new (window.AudioContext||window.webkitAudioContext)();
    const osc = ctx.createOscillator(), gain = ctx.createGain();
    osc.type='square'; osc.frequency.value=880;
    gain.gain.setValueAtTime(0.15, ctx.currentTime);
    osc.connect(gain); gain.connect(ctx.destination);
    osc.start(); osc.stop(ctx.currentTime+0.18);
    osc.onended=function(){ctx.close();};
  }catch(e){}
}

// Her ad hangi cihaza (Sudepo/Konteyner) ait oldugunu belirtir.
const tetikleyiciAdlari=['Sudepo: Sol Kapı','Sudepo: Sağ Kapı','Sudepo: PIR (Hareket)','Sudepo: Su Seviyesi','Sudepo: Kaçak','Sudepo: Sensör Hatası'];
function tetikleyenMetni(mask,panicAktif,konteynerPir,konteynerKapi,konteynerSwan,konteynerDuman,konteynerGaz){
  if(panicAktif) return 'Panik (elle açıldı)';
  const l=[];
  for(let i=0;i<6;i++) if(mask&(1<<i)) l.push(tetikleyiciAdlari[i]);
  if(konteynerPir) l.push('Konteyner: HC505-1 PIR');
  if(konteynerKapi) l.push('Konteyner: Kapı');
  if(konteynerSwan) l.push('Konteyner: Swan PIR');
  if(konteynerDuman) l.push('Konteyner: Duman');
  if(konteynerGaz) l.push('Konteyner: Gaz (MQ6)');
  return l.length?l.join(', '):'-';
}

function show(id){
  document.querySelectorAll('.section').forEach(s=>s.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.nav button').forEach(b=>b.classList.remove('active'));
  document.getElementById('nav-'+id).classList.add('active');
  try{ localStorage.setItem('sonSekme', id); }catch(e){}
  if(id==='bilgiler' && typeof alarmLoguTamYukle==='function') alarmLoguTamYukle();
  if(id==='bilgiler' && typeof suAkisiYukle==='function') suAkisiYukle();
}
// Genel "karta/gostergeye tikla, ilgili ayara git" kisayolu (kullanici
// talebi, 2026-08-27: "ana guc kartina tikladigimda ana guc ayarlarina
// gitsin, swan pira tikladigimda onun ayarina gitsin - bu kisa yolu tum
// projelerimizde tanimlayalim"). Ayarlar sekmesine gecer, hedef elemani
// (ve varsa kapali ust <details> zincirini) acar, gorunur alana kaydirir.
function gitAyar(hedefId){
  show('ayarlar');
  const el = document.getElementById(hedefId);
  if(!el) return;
  if(el.tagName === 'DETAILS') el.open = true;
  let p = el.parentElement;
  while(p){ if(p.tagName === 'DETAILS') p.open = true; p = p.parentElement; }
  setTimeout(()=>{ el.scrollIntoView({behavior:'smooth', block:'start'}); }, 60);
}
// Ayarlar sekmesinde degil, Kontrol sekmesinde bir elemana gitmek icin (orn.
// Acil Durum Lambasi butonu Ayarlar'da degil Kontrol'de).
function gitKontrol(hedefId){
  show('kontrol');
  if(!hedefId) return;
  setTimeout(()=>{ const el=document.getElementById(hedefId); if(el) el.scrollIntoView({behavior:'smooth', block:'center'}); }, 60);
}
(function(){
  try{
    const sonSekme = localStorage.getItem('sonSekme');
    if(sonSekme && document.getElementById(sonSekme)) show(sonSekme);
  }catch(e){}
})();

// LED-bar doluluk gostergesi: 20 segment, her segment kendi konumuna gore
// kirmizi(<=20%)/sari(<=40%)/yesil renklenir - klasik yakit gostergesi mantigi
// (kullanici talebi, 2026-08-28: "simulasyon panosu" hissi).
(function(){
  const lb = document.getElementById('ledbar-depo');
  if(!lb) return;
  for(let i=0;i<20;i++){ const s=document.createElement('span'); lb.appendChild(s); }
})();
function ledBarGuncelle(id, pct){
  const lb = document.getElementById(id);
  if(!lb) return;
  const segs = lb.children;
  const lit = Math.round((Math.max(0,Math.min(100,pct))/100)*segs.length);
  for(let i=0;i<segs.length;i++){
    const segPct = ((i+1)/segs.length)*100;
    segs[i].className = (i<lit) ? (segPct<=20?'lit-red':segPct<=40?'lit-yellow':'lit-green') : '';
  }
}
function renderUI(d){
  const mo = d.moisture||{};
  // Su seviyesi - bu degerler ESP8266'dan geliyor (sensorData struct, RS485
  // ile dolduruluyor). ESP8266 baglantisi koparsa backend bu alanlari
  // SIFIRLAMIYOR, son bilinen degeri struct'ta tutmaya devam ediyor (bkz
  // durumJson main.cpp) - bu yuzden burada esp8266_online kontrolu YAPMAZSAK
  // bağlanti koptuktan cok sonra bile son gorulen (artik yanlis/eski) seviye
  // ekranda "canliymis gibi" kalirdi. FIX: baglanti yoksa "--" goster.
  const esp8266Baglı = d.esp8266_online !== false;
  // 7-segment dijital gosterge: normalde yesil, tehlikede (dusuk seviye
  // alarmi) kirmizi - bkz .kpi/.kpi.danger CSS.
  const depoTehlike = !!(d.alarm && d.alarm.low_level);
  ['kpi-pct','kpi-cm','kpi-litre'].forEach(id=>{ const el=$('#'+id); if(el) el.classList.toggle('danger', depoTehlike); });
  if(esp8266Baglı){
    $('#kpi-pct').textContent=(d.level_percent||0).toFixed(1)+'%';
    $('#kpi-cm').textContent=(d.level_cm||0).toFixed(1)+' cm';
    $('#kpi-litre').textContent=(d.level_liters||0).toFixed(0)+' L';
    ledBarGuncelle('ledbar-depo', d.level_percent||0);
  } else {
    $('#kpi-pct').textContent='--';
    $('#kpi-cm').textContent='--';
    $('#kpi-litre').textContent='--';
    ledBarGuncelle('ledbar-depo', 0);
  }
  // Dis Sicaklik/Nem - AHT10, Konteyner/ESP32'nin kendi yerel sensoru, ESP8266
  // baglantisindan BAGIMSIZ (eskiden burada ESP8266'nin hep 0.0 gonderdigi
  // olu bir yer tutucu - sensorData.temperature - gosteriliyordu).
  const kzAhtOk = d.konteyner && d.konteyner.aht_ok;
  $('#kpi-temp').textContent = kzAhtOk ? (d.konteyner.sicaklik||0).toFixed(1)+' °C' : '--';
  $('#kpi-nem').textContent = kzAhtOk ? (d.konteyner.nem||0).toFixed(1)+' %' : '--';
  // Alarm
  const ad=$('#alarm-dot'); let at='';
  // FIX: alarm sistemi kapaliyken (d.alarm.enabled===false) bile ham kapi/
  // kacak/seviye durumuna bakip banner gosteriyordu - esp8266_slave ise
  // sistem kapaliyken hicbir sey gostermiyor (tetikleyici mask'i sifirliyor).
  // Ikisi tutarli olsun diye burada da enabled kontrolu eklendi.
  // FIX: anyAlarm sadece leak/low_level/door bayraklarina bakiyordu - PIR ve
  // sensor hatasi tetikleyicileri bu 3 bayraga hic yansimiyor (sadece
  // trigger_mask'te var), yani PIR ile tetiklenen bir alarmda banner HICBIR
  // modda (sessiz dahil) gorunmuyordu. trigger_mask ESP8266'nin zaten
  // mod+zaman senaryosuna gore filtreledigi otoriter kaynak - artik o
  // kullaniliyor. Panik de ayrica eklendi (eskiden hic banner tetiklemiyordu).
  const alarmMask = (d.alarm && d.alarm.trigger_mask) || 0;
  // Konteyner (ESP32-yerel) sensorleri de genel alarm sistemine dahil - ama
  // KENDI bagimsiz ac/kapa anahtarina (d.konteyner.enabled) uyar, Sudepo'nun
  // d.alarm.enabled'inden AYRI - iki zon birbirinden bagimsiz kapatilabilir.
  const enabledMi = !(d.alarm && d.alarm.enabled === false);
  const kz = d.konteyner||{};
  const konteynerEnabledMi = !(kz.enabled === false);
  const konteynerPirVar = konteynerEnabledMi && kz.pir_en!==false && !!kz.pir_alarm;
  const konteynerKapiVar = konteynerEnabledMi && kz.kapi_en!==false && !!kz.kapi_acik;
  const konteynerSwanVar = konteynerEnabledMi && kz.swan_en!==false && !!kz.swan_alarm;
  const konteynerDumanVar = konteynerEnabledMi && kz.duman_en!==false && !!kz.duman;
  // Gaz (MQ6) panik gibi davranir - backend'de konteynerAlarmEtkin'den de
  // BAGIMSIZ (bkz alarmLedGuncelle konteynerAcilDurum), bu yuzden burada da
  // konteynerEnabledMi ile gate'lenmez, sadece kendi Etkin anahtarina bakar.
  const konteynerGazVar = kz.gaz_en!==false && !!kz.gaz;
  // Panik, alarm sistemi kapali (enabled===false) olsa bile ESP8266 tarafinda
  // her seyin onunde calisir (bkz esp8266_slave main.cpp panicRoleAktif) - bu
  // yuzden panic iken enabled kontrolunu atlar, aksi halde alarm sistemi
  // kapatilmisken panik basilinca banner hic gorunmuyordu.
  const anyAlarm = !!(d.alarm && ((enabledMi && alarmMask !== 0) || d.alarm.panic || konteynerPirVar || konteynerKapiVar || konteynerSwanVar || konteynerDumanVar || konteynerGazVar));
  if(d.alarm){
    if(d.alarm.panic) at='PANİK AKTİF';
    else if(konteynerGazVar) at='ALARM: Konteyner gaz sızıntısı!';
    else if(d.alarm.leak) at='ALARM: Sudepo kaçak!';
    else if(d.alarm.low_level) at='ALARM: Sudepo düşük seviye!';
    else if(d.alarm.door) at='ALARM: Sudepo kapı açık!';
    else if(alarmMask & 4) at='ALARM: Sudepo hareket algılandı!';
    else if(alarmMask & 32) at='ALARM: Sudepo sensör hatası!';
    else if(konteynerPirVar) at='ALARM: Konteyner hareket!';
    else if(konteynerKapiVar) at='ALARM: Konteyner kapı açık!';
    else if(konteynerSwanVar) at='ALARM: Konteyner Swan PIR hareket!';
    else if(konteynerDumanVar) at='ALARM: Konteyner duman!';
  }
  // ESP8266/Nano ile RS485 iletisimi kopuksa "Sistem Normal" YANLIS bir
  // guven verir - kullanici talebi: iletisim sorunu varsa acikca belirt.
  // Gercek bir alarm zaten varsa onun metnine eklenir (ikisi de gorunur
  // olsun), yoksa "Sistem Normal" yerine dogrudan baglanti sorunu yazilir.
  const baglantiSorunu = (d.esp8266_online === false) || (d.nano_online === false);
  const digerSorun = (d.rtc_ok === false) || !!d.sensor_err;
  if(baglantiSorunu || digerSorun){
    const sorunMetni = (d.esp8266_online === false && d.nano_online === false) ? 'ESP8266 VE Nano bağlantısı yok'
      : (d.esp8266_online === false) ? 'ESP8266 (Sudepo) bağlantısı yok'
      : (d.nano_online === false) ? 'Nano bağlantısı yok'
      : (d.rtc_ok === false) ? 'RTC geçersiz' : 'Sensör hatası';
    at = at ? (at + ' | ' + sorunMetni) : sorunMetni;
  }
  // Sistem durumu 3 kademeli: Tehlike (kırmızı, soft yanıp söner) = aktif
  // alarm veya onay bekleniyor; Kritik (sarı) = alarm yok ama bağlantı/RTC/
  // sensör sorunu var; Normal (yeşil) = ikisi de yok. "Sistem Normal" metni
  // KALDIRILDI (kullanıcı talebi) - durum sadece ışıkla anlatılır, metin
  // sadece bir sorun/alarm varsa gösterilir.
  const bekliyorGenel = !!((d.alarm && d.alarm.pending) || (d.konteyner && d.konteyner.pending));
  const tehlikeVar = anyAlarm || bekliyorGenel;
  const kritikVar = !tehlikeVar && (baglantiSorunu || digerSorun);
  ad.className = 'sysdot ' + (tehlikeVar ? 'tehlike' : (kritikVar ? 'kritik' : 'normal'));
  $('#alarm-text').textContent = tehlikeVar ? (bekliyorGenel && !anyAlarm ? ('ONAY BEKLİYOR - '+at) : at) : (kritikVar ? at : '');
  // Yeni bir tehlike baslarsa listeyi hemen tazele (aksi halde periyodik
  // alarmLoguYukle() dongusune kadar - en fazla 15sn - eski liste gorunur).
  if(tehlikeVar && !sysDurumOncekiTehlike) alarmLoguYukle();
  sysDurumOncekiTehlike = tehlikeVar;
  // Buyuk uyari banner'i - ESP8266'daki gibi, tetiklendiginde sayfanin
  // her sekmesinde gorunur olsun diye header'in hemen altina konuldu.
  const ban=$('#alarm-banner');
  if(ban){
    const bekliyor = (d.alarm && d.alarm.pending) || (d.konteyner && d.konteyner.pending);
    const alarmSimdiVar = !!(anyAlarm || bekliyor);
    if(alarmSimdiVar && !alarmOncekiDurum) bipSesi();
    alarmOncekiDurum = alarmSimdiVar;
    const panikAktif = !!(d.alarm && d.alarm.panic);
    if(anyAlarm || bekliyor){
      // Panik: susturma/tetikleyen bilgisi anlamsiz (esp8266 tarafinda panik
      // susturmayi hic hesaba katmiyor, tetikleyici de yok - elle acildi) -
      // sadece "Panik Kapat" gosterilir. Bkz esp8266_slave data/app.js (ayni
      // duzeltme orada da yapildi, iki panel tutarli olsun diye).
      let msg = panikAktif ? at : (bekliyor ? ('ONAY BEKLIYOR - '+at) : at);
      // d.alarm.muted sadece Sudepo/ESP8266'yi yansitir, Konteyner ayri (bkz konteynerSusturuldu, main.cpp).
      const konteynerSusturulduMu = !!(d.konteyner && d.konteyner.susturuldu);
      const herhangiSusturulmus = (d.alarm && d.alarm.muted) || konteynerSusturulduMu;
      if(!panikAktif){
        if(herhangiSusturulmus) msg += ' (Susturuldu)';
        const tk = tetikleyenMetni((d.alarm&&d.alarm.trigger_mask)||0, false, konteynerPirVar, konteynerKapiVar, konteynerSwanVar, konteynerDumanVar, konteynerGazVar);
        msg += ' | Tetikleyen: '+tk;
      }
      let html = '⚠ '+msg;
      if(panikAktif){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/panic\')">Panik Kapat</button></div>';
      } else if(bekliyor){
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-danger" onclick="bannerAksiyon(this,\'/api/alarm/onayla\')">Sesli</button><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/onayla_lamba\')">Sessiz (Lamba)</button></div>';
      } else if(anyAlarm){
        const susLabel = herhangiSusturulmus ? 'Susturmayi Kaldir' : 'Sustur/Sireni Kapat';
        html += '<div class="row" style="margin-top:10px;justify-content:center"><button class="btn btn-warn" onclick="bannerAksiyon(this,\'/api/alarm/mute\')">'+susLabel+'</button></div>';
      }
      ban.innerHTML = html;
      ban.style.display='block';
    } else {
      ban.style.display='none';
    }
  }
  // WiFi ust bar - ESP8266'daki gibi bagli ag + IP
  const wt=$('#wifi-topbar');
  if(wt){
    if(d.wifi_baglibi && d.wifi_ssid){ wt.textContent='WiFi: '+d.wifi_ssid+' ('+d.wifi_ip+')'; }
    else if(d.wifi_ssid){ wt.textContent='WiFi: '+d.wifi_ssid+' (bağlanamadı, AP: '+(d.ap_ip||'-')+')'; }
    else { wt.textContent='WiFi: tanımsız, AP: '+(d.ap_ip||'-'); }
  }
  const wdk=$('#wifi-durum-kutu');
  if(wdk){
    if(d.wifi_ssid){
      wdk.innerHTML='Ağ: <b>'+d.wifi_ssid+'</b><br>Durum: <b>'+(d.wifi_baglibi?('Bağlı ('+d.wifi_ip+')'):'Bağlı değil')+'</b>';
    } else {
      wdk.innerHTML='Ev WiFi tanımlı değil (AP: '+(d.ap_ip||'-')+')';
    }
  }
  const sw=$('#sum-wifi'); if(sw) sw.textContent = d.wifi_baglibi&&d.wifi_ssid ? d.wifi_ssid : (d.wifi_ssid?'bağlanamadı':'tanımsız');
  // Cihaz durumları
  const esp8266Ok = d.esp8266_online !== false;
  const nanoOk    = d.nano_online !== false;
  const espBadge=$('#esp8266-badge'), nanoBadge=$('#nano-badge');
  espBadge.textContent='ESP8266: '+(esp8266Ok?'OK':'Offline');
  espBadge.style.background=esp8266Ok?'rgba(16,185,129,.15)':'rgba(239,68,68,.15)';
  espBadge.style.color=esp8266Ok?'var(--accent)':'var(--danger)';
  nanoBadge.textContent='Nano: '+(nanoOk?'OK':'Offline');
  nanoBadge.style.background=nanoOk?'rgba(16,185,129,.15)':'rgba(239,68,68,.15)';
  nanoBadge.style.color=nanoOk?'var(--accent)':'var(--danger)';
  // Cihaz Durumu - genel sorun uyarilari (RTC, sensor, RS485 baglanti)
  const hb=$('#hata-box');
  if(hb){
    const sorunlar=[];
    if(!esp8266Ok) sorunlar.push('ESP8266 (Sudepo) bağlantısı yok');
    if(!nanoOk) sorunlar.push('Nano bağlantısı yok');
    if(d.rtc_ok===false) sorunlar.push('RTC (tarih/saat) geçersiz - ESP8266\'daki DS1307 zamanı kaybetmiş, Sudepo sayfasından zamanı yeniden ayarlayın');
    if(d.sensor_err) sorunlar.push('Mesafe sensörü (HC-SR04) hata veriyor');
    hb.innerHTML = sorunlar.length ? sorunlar.map(s=>'⚠️ '+s).join('<br>') : '';
  }
  // Konteyner Zonu PIR - durum + ayar alanlari (odaklıyken üzerine yazma).
  // Var/Yok metni yerine LED gostergesi: aktifken yanan (kirmizi glow), pasifken sonuk.
  const kzp=$('#kz-pir'); if(kzp) kzp.classList.toggle('on', !!kz.pir);
  const kzsw=$('#kz-swan'); if(kzsw) kzsw.classList.toggle('on', !!kz.swan_pir);
  const kzk=$('#kz-kapi'); if(kzk) kzk.classList.toggle('on', !!kz.kapi_acik);
  const kzav=$('#kz-duman'); if(kzav) kzav.classList.toggle('on', !!kz.duman);
  const kzgv=$('#kz-gaz'); if(kzgv) kzgv.classList.toggle('on', !!kz.gaz);
  const kzsic=$('#kz-sicaklik'); if(kzsic) kzsic.textContent = kz.aht_ok ? (kz.sicaklik||0).toFixed(1)+' °C' : '--';
  const kznem=$('#kz-nem'); if(kznem) kznem.textContent = kz.aht_ok ? (kz.nem||0).toFixed(1)+' %' : '--';
  const kzmq=$('#kz-mq6'); if(kzmq) kzmq.textContent = (kz.mq6_volt!=null) ? kz.mq6_volt.toFixed(2)+'V ('+kz.mq6_raw+') '+(kz.mq6_powered?'[CANLI]':'[ISITICI KAPALI - ESKI DEGER]') : '--';
  const kzmqt=$('#kz_mq6Test'); if(kzmqt && document.activeElement!==kzmqt) kzmqt.checked = !!kz.mq6_test_modu;
  const kzgp=$('#kz-gp2y10'); if(kzgp) kzgp.textContent = (kz.gp2y10_volt!=null) ? kz.gp2y10_volt.toFixed(2)+'V ('+kz.gp2y10_raw+')' : '--';
  // Genel/Dashboard sayfasindaki ayni LED'lerin+degerlerin kopyasi (kullanici
  // talebi, 2026-08-27) - Ayarlar sekmesine gitmeden tek bakista gorunsun.
  const dbp=$('#db-pir'); if(dbp) dbp.classList.toggle('on', !!kz.pir);
  const dbsw=$('#db-swan'); if(dbsw) dbsw.classList.toggle('on', !!kz.swan_pir);
  const dbk=$('#db-kapi'); if(dbk) dbk.classList.toggle('on', !!kz.kapi_acik);
  const dbav=$('#db-duman'); if(dbav) dbav.classList.toggle('on', !!kz.duman);
  const dbgv=$('#db-gaz'); if(dbgv) dbgv.classList.toggle('on', !!kz.gaz);
  const dbsi=$('#db-siren'); if(dbsi) dbsi.classList.toggle('on', !!kz.siren);
  const dbal=$('#db-acillamba'); if(dbal) dbal.classList.toggle('on', !!(d.ana_guc && d.ana_guc.acil_lamba));
  const dbmq=$('#db-mq6'); if(dbmq) dbmq.textContent = (kz.mq6_volt!=null) ? kz.mq6_volt.toFixed(2)+'V ('+kz.mq6_raw+') '+(kz.mq6_powered?'[CANLI]':'[ISITICI KAPALI]') : '--';
  const dbgp=$('#db-gp2y10'); if(dbgp) dbgp.textContent = (kz.gp2y10_volt!=null) ? kz.gp2y10_volt.toFixed(2)+'V ('+kz.gp2y10_raw+')' : '--';
  const kza=$('#kz-alarm'); if(kza){ const kzAktif=kz.pir_alarm||(kz.kapi_en!==false&&kz.kapi_acik)||(kz.swan_en!==false&&kz.swan_alarm)||(kz.duman_en!==false&&kz.duman)||(kz.gaz_en!==false&&kz.gaz); kza.classList.toggle('on', !!kzAktif && !kz.pending); kza.classList.toggle('pending', !!kz.pending); const kzat=$('#kz-alarm-txt'); if(kzat) kzat.textContent = kz.pending?'ONAY BEKLİYOR':''; }
  const kzs=$('#kz-siren'); if(kzs) kzs.classList.toggle('on', !!kz.siren);
  const kzl=$('#kz-lamba'); if(kzl) kzl.classList.toggle('on', !!kz.lamba);
  const kzpe=$('#kz_pirEtkin'); if(kzpe && document.activeElement!==kzpe) kzpe.checked = kz.pir_en!==false;
  const kzke=$('#kz_kapiEtkin'); if(kzke && document.activeElement!==kzke) kzke.checked = kz.kapi_en!==false;
  const kzsue=$('#kz_swanEtkin'); if(kzsue && document.activeElement!==kzsue) kzsue.checked = kz.swan_en!==false;
  const kzale=$('#kz_dumanEtkin'); if(kzale && document.activeElement!==kzale) kzale.checked = kz.duman_en!==false;
  const kzge=$('#kz_gazEtkin'); if(kzge && document.activeElement!==kzge) kzge.checked = kz.gaz_en!==false;
  const kzges=$('#kz_gazEsik'); if(kzges && !kzges.matches(':focus') && !yakinKorumali('kz_gazEsik') && kz.gaz_esik!=null) kzges.value=kz.gaz_esik;
  const kzdes=$('#kz_dumanEsik'); if(kzdes && !kzdes.matches(':focus') && !yakinKorumali('kz_dumanEsik') && kz.duman_esik!=null) kzdes.value=kz.duman_esik;
  const kzpt=$('#kz_pirTutma'); if(kzpt && !kzpt.matches(':focus') && !yakinKorumali('kz_pirTutma') && kz.pir_tutma!=null) kzpt.value=kz.pir_tutma;
  const kzpo=$('#kz_pirOnay'); if(kzpo && !kzpo.matches(':focus') && !yakinKorumali('kz_pirOnay') && kz.pir_onay!=null) kzpo.value=kz.pir_onay;
  const kzst=$('#kz_swanTutma'); if(kzst && !kzst.matches(':focus') && !yakinKorumali('kz_swanTutma') && kz.swan_tutma!=null) kzst.value=kz.swan_tutma;
  const kzso=$('#kz_swanOnay'); if(kzso && !kzso.matches(':focus') && !yakinKorumali('kz_swanOnay') && kz.swan_onay!=null) kzso.value=kz.swan_onay;
  // Nano IO - dashboard (K1/K2/R/LAMBA hepsi ESP8266'nin tek RS485 mesajinda
  // geliyor - esp8266Baglı degilse hepsi ayni sekilde bayat kalir, level/
  // sicaklik icin kullanilan ayni tazelik esigiyle '--' gosterilir)
  // Sudepo Zonu Sensorleri karti (Konteyner Sensorleri ile ayni LED deseni,
  // kullanici talebi 2026-08-28) - baglanti yoksa hepsi sonuk kalir.
  { const e=$('#db8-kapi1'); if(e) e.classList.toggle('on', esp8266Ok && !!d.nano.door1); }
  { const e=$('#db8-kapi2'); if(e) e.classList.toggle('on', esp8266Ok && !!d.nano.door2); }
  { const e=$('#db8-role');  if(e) e.classList.toggle('on', esp8266Ok && !!d.nano.relay); }
  { const e=$('#db8-lamba'); if(e) e.classList.toggle('on', esp8266Ok && !!d.nano.lamp); }
  { const e=$('#db8-pir');   if(e) e.classList.toggle('on', esp8266Ok && !!(alarmMask & 4)); }
  { const e=$('#db8-kacak'); if(e) e.classList.toggle('on', esp8266Ok && !!(d.alarm && d.alarm.leak)); }
  { const e=$('#db8-nem'); if(e) e.textContent = esp8266Ok ? (mo.percent||0).toFixed(1)+'%' : '--'; }
  { const e=$('#db8-nem-mod'); if(e) e.textContent = esp8266Ok ? (mo.auto?'Otomatik':'Manuel') : '--'; }
  { const e=$('#db8-nemrl'); if(e) e.classList.toggle('on', esp8266Ok && !!mo.output); }
  // === BUTON METİNLERİ - SUNUCUDAN GELIR, local state YOK ===
  // Her buton her zaman taze sunucu verisiyle guncellenir - eskiden busySet
  // ile "islemde" butonlar atlaniyordu, bu da tepkinin gec/tikanik hissi
  // vermesine neden oluyordu (kullanici bildirdi), kaldirildi.
  $('#lamba-btn').textContent = 'Sudepo Zonu: ' + (esp8266Ok ? (d.nano.lamp ? 'Kapat' : 'Aç') : '--');
  { const klb=$('#konteyner-lamba-btn'); if(klb) klb.textContent = 'Konteyner Zonu: ' + ((d.konteyner&&d.konteyner.lamba) ? 'Kapat' : 'Aç'); }
  $('#alarm-btn').textContent = 'Sudepo Zonu: ' + ((d.alarm&&d.alarm.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç');
  { const kab=$('#konteyner-alarm-btn'); if(kab) kab.textContent = 'Konteyner Zonu: ' + ((d.konteyner&&d.konteyner.enabled!==false) ? 'Alarmı Kapat' : 'Alarmı Aç'); }
  { const ss=$('#sum-sudepo'); if(ss) ss.textContent = (d.alarm&&d.alarm.enabled!==false) ? 'Aktif' : 'Kapalı'; }
  { const sk=$('#sum-konteyner'); if(sk) sk.textContent = (d.konteyner&&d.konteyner.enabled!==false) ? 'Aktif' : 'Kapalı'; }
  { const pb=$('#panic-btn'); if(pb) pb.textContent = (d.alarm&&d.alarm.panic) ? 'Panik Açık' : 'Panik'; }
  { const ams=$('#alarm-mod-sel'); if(ams && d.alarm && d.alarm.mode) ams.value=String(d.alarm.mode); }
  { const amb=$('#alarm-mute-btn'); if(amb) amb.textContent = (d.alarm&&d.alarm.muted) ? 'Susturma Kaldir' : 'Sustur/Sireni Kapat'; }
  { const msb=$('#moisture-settings-toggle-btn'); if(msb) msb.textContent = mo.output ? 'Kapat' : 'Aç'; }
  { const sab=$('#moisture-settings-auto-btn'); if(sab) sab.textContent = mo.auto ? 'Manuel' : 'Otomatik'; }
  if(typeof d.telegram_aktif==='boolean'){ telegramAktifBilinen=d.telegram_aktif; const tb=$('#telegram-ac-kapa-btn'); if(tb) tb.textContent=telegramAktifBilinen?'🔔 Bildirimler Açık':'🔕 Bildirimler Kapalı'; const st=$('#sum-telegram'); if(st) st.textContent=telegramAktifBilinen?'Açık':'Kapalı'; }
  // Nem verileri - ESP8266 uzerinden geliyor, ayni tazelik esigiyle korunur
  // (bkz. level/sicaklik icin yukarida yapilan esp8266Baglı fix'i - kullanici
  // ayni donmus-deger sorununun nem icin de var oldugunu bildirdi)
  // Ayarlar sekmesi nem göstergeleri
  const smv=$('#settings-moisture-val'); if(smv) smv.textContent=esp8266Ok?(mo.percent||0).toFixed(1)+'%':'--';
  const smo=$('#settings-moisture-out'); if(smo) smo.textContent=esp8266Ok?(mo.output?'Açık':'Kapalı'):'--';
  const smm=$('#settings-moisture-mod'); if(smm) smm.textContent=esp8266Ok?(mo.auto?'Otomatik':'Manuel'):'--';
  { const sn=$('#sum-nem'); if(sn) sn.textContent = esp8266Ok ? ((mo.output?'Açık':'Kapalı')+' - '+(mo.auto?'Otomatik':'Manuel')) : 'Bağlantı yok'; }
  const sml=$('#moisture-settings-low'); if(sml&&!sml.matches(':focus')&&!yakinKorumali('moisture-settings-low')) sml.value=mo.low||0;
  const smh=$('#moisture-settings-high'); if(smh&&!smh.matches(':focus')&&!yakinKorumali('moisture-settings-high')) smh.value=mo.high||0;
  // Batarya (MPPT) - kendi ayri "online" bayragi var (esp8266Ok'tan bagimsiz,
  // MPPT UART2 hatti ESP8266 RS485'inden tamamen ayri bir bus)
  const bat = d.battery || {};
  const batOk = bat.online !== false;
  const bkpi=$('#kpi-batarya'); if(bkpi){ bkpi.textContent = batOk ? (bat.voltage||0).toFixed(1)+' V' : '--'; bkpi.classList.toggle('danger', !!(batOk && bat.kritik)); }
  const bsoc=$('#batarya-soc'); if(bsoc) bsoc.textContent = (batOk && bat.soc!=null && bat.soc>=0) ? ('('+bat.soc+'%)') : '';
  const bdur=$('#batarya-durum');
  if(bdur){
    if(!bat.koruma_aktif) bdur.textContent = batOk ? 'İzleniyor (koruma kapalı)' : 'Bağlantı yok';
    else bdur.textContent = !batOk ? 'Bağlantı yok' : (bat.kritik ? 'KRİTİK - sulama/lamba kesildi' : 'Normal');
    bdur.style.color = (batOk && bat.kritik) ? 'var(--danger)' : '';
  }
  const bpv=$('#batarya-pv'); if(bpv) bpv.textContent = batOk ? (bat.pv_watt||0).toFixed(0)+'W ('+(bat.pv_amp||0).toFixed(1)+'A)' : '--';
  const byuk=$('#batarya-yuk'); if(byuk) byuk.textContent = batOk ? (bat.load_watt||0).toFixed(0)+'W ('+(bat.load_amp||0).toFixed(1)+'A)' : '--';
  const bkalan=$('#batarya-kalan');
  if(bkalan){
    if(!batOk) bkalan.textContent='';
    else if(bat.kalan_saat==null || bat.kalan_saat<0) bkalan.textContent = (bat.pv_watt>bat.load_watt) ? '🔆 Şarj oluyor' : '';
    else bkalan.textContent = '⏳ Bu tüketimle ~'+bat.kalan_saat.toFixed(1)+' saat kaldı';
  }
  { const bkb=$('#batarya-koruma-btn'); if(bkb) bkb.textContent = bat.koruma_aktif ? 'Korumayı Kapat' : 'Korumayı Aç'; const sbk=$('#sum-batkoruma'); if(sbk) sbk.textContent = bat.koruma_aktif ? 'Açık' : 'Kapalı'; }
  // İnvertör sekmesi - tum QPIGS/QMOD/QPIWS/QPIRI detaylari
  {
    const inv = bat.inverter || {};
    const set = (id, val) => { const el = $(id); if(el) el.textContent = val; };
    set('#inv-baglanti', batOk ? '✅ Bağlı' : '❌ Bağlantı yok');
    set('#inv-mode', inv.mode_ok ? (inv.mode_text||'-') : '-');
    set('#inv-warn', !inv.warn_ok ? '-' : (inv.warn_list ? ('⚠️ '+inv.warn_list) : '✅ Uyarı/arıza yok'));
    set('#inv-load-on', batOk ? (inv.load_on?'Açık':'Kapalı') : '-');
    set('#inv-charging', batOk ? ((inv.charging_on?'Genel:Açık':'Genel:Kapalı')+' / '+(inv.scc_charging_on?'Solar:Açık':'Solar:Kapalı')+' / '+(inv.ac_charging_on?'AC:Açık':'AC:Kapalı')) : '-');
    set('#inv-batt-v', batOk ? (bat.voltage||0).toFixed(2)+' V' : '-');
    set('#inv-soc', batOk && bat.soc!=null ? bat.soc+' %' : '-');
    set('#inv-batt-charge-a', batOk ? (inv.batt_charge_a||0).toFixed(1)+' A' : '-');
    set('#inv-batt-discharge-a', batOk ? (inv.batt_discharge_a||0).toFixed(1)+' A' : '-');
    set('#inv-pv-v', batOk ? (bat.pv_volt||0).toFixed(1)+' V' : '-');
    set('#inv-pv-a', batOk ? (bat.pv_amp||0).toFixed(1)+' A' : '-');
    set('#inv-pv-w', batOk ? (bat.pv_watt||0).toFixed(0)+' W' : '-');
    set('#inv-load-v', batOk ? (bat.load_volt||0).toFixed(1)+' V' : '-');
    set('#inv-load-w', batOk ? (bat.load_watt||0).toFixed(0)+' W / '+(inv.apparent_va||0).toFixed(0)+' VA' : '-');
    set('#inv-load-pct', batOk ? (inv.load_pct||0)+' %' : '-');
    set('#inv-kalan', (batOk && bat.kalan_saat!=null && bat.kalan_saat>=0) ? ('~'+bat.kalan_saat.toFixed(1)+' saat') : '-');
    set('#inv-grid', batOk ? (inv.grid_volt||0).toFixed(1)+'V / '+(inv.grid_hz||0).toFixed(1)+'Hz' : '-');
    set('#inv-ac-hz', batOk ? (inv.ac_out_hz||0).toFixed(1)+' Hz' : '-');
    set('#inv-bus-v', batOk ? (inv.bus_volt||0).toFixed(1)+' V' : '-');
    set('#inv-temp', batOk ? (inv.temp_c||0).toFixed(0)+' °C' : '-');
    const battTipleri = {0:'AGM',1:'Sulu',2:'Kullanıcı',3:'PYL',4:'SH'};
    const oncelikler = {0:'Şebeke önce',1:'Solar önce',2:'SBU',3:'Sadece solar'};
    set('#inv-ri-battv', inv.ri_ok ? (inv.ri_batt_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-recharge', inv.ri_ok ? (inv.ri_batt_recharge_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-under', inv.ri_ok ? (inv.ri_batt_under_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-bulk', inv.ri_ok ? (inv.ri_batt_bulk_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-float', inv.ri_ok ? (inv.ri_batt_float_v||0).toFixed(1)+' V' : '-');
    set('#inv-ri-type', inv.ri_ok ? (battTipleri[inv.ri_batt_type]||('#'+inv.ri_batt_type)) : '-');
    set('#inv-ri-maxac', inv.ri_ok ? inv.ri_max_ac_charge_a+' A' : '-');
    set('#inv-ri-maxchg', inv.ri_ok ? inv.ri_max_charge_a+' A' : '-');
    set('#inv-ri-outpri', inv.ri_ok ? (oncelikler[inv.ri_out_priority]||('#'+inv.ri_out_priority)) : '-');
    set('#inv-ri-chgpri', inv.ri_ok ? (oncelikler[inv.ri_charger_priority]||('#'+inv.ri_charger_priority)) : '-');
  }
  // Yedek Aku - pasif donanimsal failover, sadece bilgi amacli gosterge
  const yak = d.yedek_aku || {};
  const yakOk = yak.guncel !== false;
  const yakKpi=$('#kpi-yedek-aku'); if(yakKpi){ yakKpi.textContent = yakOk ? (yak.volt||0).toFixed(1)+' V' : '--'; yakKpi.classList.toggle('danger', !!(yakOk && yak.durum==='zayif')); }
  const yakDurum=$('#yedek-aku-durum');
  if(yakDurum){
    const etiket = {dolu:'✅ Dolu/boşta (devrede değil)', devrede:'🔋 Devrede', zayif:'⚠️ Zayıf - şarj/değişim gerekir', bilinmiyor:'-'};
    yakDurum.textContent = yakOk ? (etiket[yak.durum]||yak.durum||'-') : 'Bağlantı yok';
    yakDurum.style.color = (yakOk && yak.durum==='zayif') ? 'var(--danger)' : '';
  }
  // Ana Guc (ADS1115) - 3 kademeli bildirim + Acil Durum Lambasi
  const ag = d.ana_guc || {};
  const agOk = ag.ok !== false;
  const agKpi=$('#kpi-ana-guc'); if(agKpi){ agKpi.textContent = agOk ? (ag.volt||0).toFixed(1)+' V' : '--'; agKpi.classList.toggle('danger', !!(agOk && ag.kademe>=2)); }
  const agDurum=$('#ana-guc-durum');
  if(agDurum){
    const kademeEtiket = {0:'✅ Normal', 1:'⚠️ Düşük', 2:'🔶 Kritik', 3:'🔴 Acil'};
    agDurum.textContent = agOk ? (kademeEtiket[ag.kademe]||'-') : 'Bağlantı yok';
    const sag=$('#sum-anaguc'); if(sag) sag.textContent = agOk ? (ag.volt||0).toFixed(1)+'V - '+(kademeEtiket[ag.kademe]||'-') : 'Bağlantı yok';
    agDurum.style.color = (agOk && ag.kademe>=2) ? 'var(--danger)' : '';
  }
  const ae1=$('#ana-guc-esik1'); if(ae1&&!ae1.matches(':focus')&&!yakinKorumali('ana-guc-esik1')&&ag.esik1!=null) ae1.value=ag.esik1;
  const ae2=$('#ana-guc-esik2'); if(ae2&&!ae2.matches(':focus')&&!yakinKorumali('ana-guc-esik2')&&ag.esik2!=null) ae2.value=ag.esik2;
  const ae3=$('#ana-guc-esik3'); if(ae3&&!ae3.matches(':focus')&&!yakinKorumali('ana-guc-esik3')&&ag.esik3!=null) ae3.value=ag.esik3;
  const alBtn=$('#acil-lamba-btn');
  if(alBtn){
    alBtn.textContent = ag.acil_lamba ? '🔴 ACİL LAMBA AÇIK (Kapat)' : '⚪ Acil Durum Lambası (Kapalı)';
    alBtn.style.background = ag.acil_lamba ? 'var(--danger)' : '';
  }
  const bkv=$('#batarya-kesme'); if(bkv&&!bkv.matches(':focus')&&!yakinKorumali('batarya-kesme')&&bat.kesme_volt!=null) bkv.value=bat.kesme_volt;
  const bgv=$('#batarya-geri'); if(bgv&&!bgv.matches(':focus')&&!yakinKorumali('batarya-geri')&&bat.geri_volt!=null) bgv.value=bat.geri_volt;
  // Bilgiler sekmesi - sistem bilgileri
  const bi=$('#bilgi-sistem');
  if(bi){
    bi.innerHTML='<p><span class=info-label>ESP32 IP</span><b>'+(d.esp32_ip||'-')+'</b> ('+(d.esp32_mode||'-')+')</p><p><span class=info-label>AP SSID</span><b>'+(d.ap_ssid||'-')+'</b></p><p><span class=info-label>mDNS</span><b>'+(d.mdns||'-')+'</b></p><p><span class=info-label>ESP8266</span><b>'+(d.esp8266_online?'OK':'Offline')+'</b> ('+(d.esp8266_last_sec!=null?d.esp8266_last_sec+'sn önce':'-')+')</p><p><span class=info-label>Nano</span><b>'+(d.nano_online?'OK':'Offline')+'</b></p><p><span class=info-label>Depo</span><b>'+(d.level_percent||0).toFixed(1)+'%</b> ('+(d.level_liters||0).toFixed(0)+' L)</p><p><span class=info-label>Nem</span><b>'+(mo.percent||0).toFixed(1)+'%</b></p><p><span class=info-label>Kalibrasyon</span>Boş '+(d.bosMesafe||0)+' cm / Dolu '+(d.doluMesafe||0)+' cm</p><p><span class=info-label>Kapasite</span><b>'+(d.kapasite||0)+' L</b> | Alarm: %'+(d.alarmYuzde||0)+'</p>';
  }
  const bfi=$('#build-info');
  if(bfi) bfi.textContent='Firmware derleme tarihi: '+(d.build_date||'-')+' | Çalışma süresi: '+fmtSure(d.uptime_sec||0);
  const gb=$('#guncelleme-bilgi');
  if(gb){
    gb.innerHTML='<p><span class=info-label>RS485 poll</span><b>'+(d.rs485_interval_ms||'-')+' ms</b></p><p><span class=info-label>Son ESP8266 verisi</span><b>'+(d.esp8266_last_sec!=null?d.esp8266_last_sec+' sn önce':'-')+'</b></p><p><span class=info-label>Tarayıcı bağlantısı</span><b>'+(_es?'SSE (anlık)':'Polling (5sn)')+'</b></p>';
  }
  const agap=$('#ag-ap-bilgi'); if(agap) agap.textContent=d.ap_ssid||'-';
}
function fmtSure(sn){
  sn=Math.floor(sn);
  const g=Math.floor(sn/86400), sa=Math.floor((sn%86400)/3600), dk=Math.floor((sn%3600)/60);
  if(g>0) return g+'g '+sa+'sa';
  if(sa>0) return sa+'sa '+dk+'dk';
  return dk+'dk';
}

function guncelle(){
  if(herhangiBirAlanaYaziliyorMu()) return;
  fetch('/api/status')
    .then(r=>r.json())
    .then(d=>renderUI(d))
    .catch(()=>{});
}

// SSE: RS485'ten yeni veri gelir gelmez (ya da en gec 1sn'de bir) sunucu
// push eder. setInterval polling'e kiyasla gecikmeyi buyuk olcude azaltir.
// Baglanti koparsa 3sn sonra otomatik yeniden dener; guncelle() 5sn'de bir
// yedek olarak calismaya devam eder (SSE tamamen kesilirse bile ekran donmaz).
let _es = null;
// Kullanici herhangi bir input/select/textarea'da yaziyorken (odaklanmisken)
// arka plandaki otomatik guncellemeler (SSE + guncelle()) O ANKI render'i
// tamamen atlar - "input/selectbox'lar agresif, yazarken siliniyor" tipi
// sorunlarin KOKUNDEN onlenmesi icin genel bir kilit (tek tek her alana
// :focus kontrolu eklemek yerine, hicbir yazma islemi arka plan
// guncellemesiyle asla yarismasin diye tum renderUI() cagrilarini kapsar).
function herhangiBirAlanaYaziliyorMu(){
  const a = document.activeElement;
  if(!a) return false;
  const t = a.tagName;
  return t==='INPUT' || t==='SELECT' || t==='TEXTAREA';
}
function connectSSE(){
  if(_es) return;
  _es = new EventSource('/events');
  _es.onmessage = function(e){
    if(herhangiBirAlanaYaziliyorMu()) return;
    try{ renderUI(JSON.parse(e.data)); }catch(err){}
  };
  _es.onerror = function(){
    if(_es){ _es.close(); _es=null; }
    setTimeout(connectSSE, 3000);
  };
}

function api(p){
  return fetch(p)
    .then(r=>r.ok?r.json():r.json().catch(()=>({basarili:false,mesaj:'HTTP '+r.status})))
    .catch(()=>({basarili:false,mesaj:'Bağlantı hatası'}));
}

// sendCommand: butonu KİLİTLEMİYOR/disable etmiyor artık - tekrar basma
// engeli, komut surerken butonda gorunen "yasak/dur" imleci gibi UI
// surtunmesi gecikme hissi yaratiyordu. RS485 tarafinda gercek komut zaten
// mutex ile guvenli sekilde siraya giriyor (bkz RS485Kilit), SET_* komutlari
// da idempotent (PANIC/ALARM_MUTE dahil) - art arda hizli tiklamak zararsiz.
function sendCommand(btnId, path, resultId, label){
  api(path).then(d=>{
    if(resultId){ const el=$(resultId); if(el) el.textContent=d.mesaj||''; }
  }).finally(()=>{
    // Sunucudan taze veri çek — buton metnini renderUI yazar
    fetch('/api/status')
      .then(r=>r.json())
      .then(d=>{ renderUI(d); })
      .catch(()=>{})
      .finally(()=>{
        // Sonucu birkaç saniye göster sonra temizle
        if(resultId) setTimeout(()=>{ const el=$(resultId); if(el) el.textContent=''; }, 4000);
      });
  });
}

function toggleLamba(){
  // Mevcut durumu buton metninden değil, sunucu state'inden bil
  // Butonda 'Kapat' yazıyorsa lamba açık demek → hedef=0
  const acik = $('#lamba-btn').textContent.trim().endsWith('Kapat');
  sendCommand('#lamba-btn', '/api/lamba?durum='+(acik?0:1), '#lamba-sonuc');
}
function toggleKonteynerLamba(){
  const acik = $('#konteyner-lamba-btn').textContent.trim().endsWith('Kapat');
  sendCommand('#konteyner-lamba-btn', '/api/konteyner/lamba?durum='+(acik?0:1), '#konteyner-lamba-sonuc');
}
function toggleAcilLamba(){
  const acik = $('#acil-lamba-btn').textContent.includes('AÇIK');
  sendCommand('#acil-lamba-btn', '/api/acil-lamba?durum='+(acik?0:1), '#acil-lamba-sonuc');
}
function anaGucEsikKaydet(){
  yakinDuzenlendi('ana-guc-esik1'); yakinDuzenlendi('ana-guc-esik2'); yakinDuzenlendi('ana-guc-esik3');
  const e1=$('#ana-guc-esik1').value, e2=$('#ana-guc-esik2').value, e3=$('#ana-guc-esik3').value;
  api('/api/ana-guc-esik?esik1='+encodeURIComponent(e1)+'&esik2='+encodeURIComponent(e2)+'&esik3='+encodeURIComponent(e3)).then(()=>{
    $('#ana-guc-esik-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#ana-guc-esik-sonuc').textContent='Hata oluştu'; });
}
function toggleMoisture(){
  const acik = $('#moisture-settings-toggle-btn').textContent.trim() === 'Kapat';
  sendCommand('#moisture-settings-toggle-btn', '/api/moisture?durum='+(acik?0:1), '#moisture-settings-msg');
}
function toggleMoistureAuto(){
  const manuel = $('#moisture-settings-auto-btn').textContent.trim() === 'Manuel';
  sendCommand('#moisture-settings-auto-btn', '/api/moisture/auto?aktif='+(manuel?0:1), '#moisture-settings-msg');
}
function setMoistureThresholds(){
  yakinDuzenlendi('moisture-settings-low'); yakinDuzenlendi('moisture-settings-high');
  const low=parseInt($('#moisture-settings-low').value)||0;
  const high=parseInt($('#moisture-settings-high').value)||0;
  if(low<0||low>100||high<0||high>100||low>=high){
    $('#moisture-settings-msg').textContent='0-100 arası ve alt < üst olmalı'; return;
  }
  sendCommand(null,'/api/moisture/threshold?low='+low+'&high='+high,'#moisture-settings-msg');
}
function toggleAlarm(){
  const aktif = $('#alarm-btn').textContent.trim().endsWith('Alarmı Kapat');
  sendCommand('#alarm-btn', '/api/alarm?aktif='+(aktif?0:1), '#alarm-sonuc');
}
function toggleKonteynerAlarm(){
  const aktif = $('#konteyner-alarm-btn').textContent.trim().endsWith('Alarmı Kapat');
  sendCommand('#konteyner-alarm-btn', '/api/konteyner/alarm?aktif='+(aktif?0:1), '#konteyner-alarm-sonuc');
}
function kapiKontrol(v){
  sendCommand(null, '/api/kapi?durum='+v, '#lamba-sonuc');
}
function togglePanic(){
  sendCommand('#panic-btn', '/api/panic', '#panic-sonuc');
}
function setAlarmMod(){
  const m = $('#alarm-mod-sel').value;
  sendCommand(null, '/api/alarm/mod?mod='+m, '#alarm-sonuc');
}
function alarmMute(){
  sendCommand('#alarm-mute-btn', '/api/alarm/mute', '#alarm-sonuc');
}
// Banner butonlari icin - lamba/alarm butonlarindaki ayni gecikme-hissi
// sorunu (disable + tekrar-tiklama engeli) burada da vardi, ayni sekilde
// kaldirildi - RS485/durum tarafinda gercek koruma zaten mutex+idempotent
// komutlarla saglaniyor (bkz sendCommand aciklamasi).
function bannerAksiyon(btn, path){
  api(path).finally(()=>{
    fetch('/api/status').then(r=>r.json()).then(renderUI).catch(()=>{});
  });
}
function otaGuncelle(){
  if(!confirm('GitHub\'daki en son firmware indirilip yazılacak, cihaz yeniden başlayacak. Emin misin?'))return;
  $('#ota-sonuc').textContent='Güncelleniyor...';
  api('/api/ota').then(d=>{$('#ota-sonuc').textContent=d.mesaj||'';});
}
function firmwareDurumYukle(){
  fetch('/api/firmware/durum').then(r=>r.json()).then(d=>{
    $('#fw-durum-kutu').innerHTML = d.varMi
      ? ('Yuklu: <b>'+(d.boyut/1024).toFixed(0)+' KB</b> (yukleme: '+d.yuklemeZamani+')<br>URL: <code>'+d.url+'</code>')
      : 'Henuz firmware yuklenmedi';
  }).catch(()=>{});
}
function firmwareYukle(){
  const f=$('#fwDosya').files[0];
  if(!f){$('#fw-sonuc').textContent='Dosya secin';return;}
  $('#fw-sonuc').textContent='Yukleniyor...';
  const fd=new FormData(); fd.append('firmware',f);
  fetch('/firmware/upload',{method:'POST',body:fd})
    .then(r=>r.json()).then(d=>{$('#fw-sonuc').textContent='Yuklendi'; firmwareDurumYukle();})
    .catch(()=>{$('#fw-sonuc').textContent='Hata!';});
}
function yedekDurumYukle(){
  fetch('/api/kayit/yedek_durum').then(r=>r.json()).then(d=>{
    $('#yedek-durum-kutu').textContent = d.varMi ? ('Yedek dosyasi: '+d.dosya+' - Son yedekleme: '+d.sonYedek) : 'Henuz yedek alinmadi';
  }).catch(()=>{});
}
function kayitYedekle(){
  $('#yedek-sonuc').textContent='Yedekleniyor...';
  api('/api/kayit/yedekle').then(d=>{$('#yedek-sonuc').textContent=d.mesaj||''; yedekDurumYukle();});
}
function kayitGeriYukle(){
  if(!confirm('ESP8266\'nin mevcut kayitlar.csv dosyasi, buradaki yedekle degistirilecek. Emin misin?'))return;
  $('#yedek-sonuc').textContent='Geri yukleniyor...';
  api('/api/kayit/geri_yukle').then(d=>{$('#yedek-sonuc').textContent=d.mesaj||'';});
}
function weatherYukleUI(){
  const kutu=$('#weather-haftalik'); if(!kutu) return;
  fetch('/api/weather').then(r=>r.json()).then(d=>{
    const wdk=$('#weather-durum-kutu');
    if(wdk){
      wdk.innerHTML = d.sayi>0
        ? ('Son çekim: <b>'+(d.tarih||'-')+'</b> ('+(d.guncel?'güncel':'ESKİ - dikkate alınmıyor')+')<br>Yarın yağmur: <b>'+(d.oneri?'Evet, sulama atlanacak':'Hayır')+'</b><br><span style="font-size:11px;color:var(--muted)">Durum: '+(d.durum||'-')+'</span>')
        : ('Henüz tahmin çekilmedi<br><span style="font-size:11px;color:var(--muted)">Durum: '+(d.durum||'-')+'</span>');
    }
    const gunler=['Paz','Pzt','Sal','Çar','Per','Cum','Cmt'];
    const liste=d.haftalik||[];
    if(liste.length===0){kutu.innerHTML='';return;}
    kutu.innerHTML=liste.map((g,i)=>{
      const tarih=new Date(g.tarih+'T12:00:00');
      const gunAdi=i===0?'Bugün':(i===1?'Yarın':gunler[tarih.getDay()]);
      const yagmurVar=g.mm>=1.0;
      const stil=yagmurVar?'background:rgba(37,99,235,.15);border-color:var(--primary)':'';
      return '<div style="padding:6px 10px;border:1px solid var(--border);border-radius:8px;font-size:12px;text-align:center;'+stil+'">'+gunAdi+'<br><b>'+g.mm.toFixed(1)+'mm</b>'+(yagmurVar?' 🌧':'')+'</div>';
    }).join('');
  }).catch(()=>{});
}
function weatherKontrolEt(){
  $('#weather-sonuc').textContent='Kontrol ediliyor...';
  api('/api/weather/check').then(d=>{$('#weather-sonuc').textContent=d.mesaj||'';weatherYukleUI();});
}
function telegramTest(){
  $('#telegram-sonuc').textContent='Gönderiliyor...';
  api('/api/telegram/test').then(d=>{$('#telegram-sonuc').textContent=d.mesaj||'';});
}
let telegramAktifBilinen=true;
function telegramAcKapa(){
  api('/api/telegram/ayar?aktif='+(telegramAktifBilinen?0:1)).then(d=>{
    telegramAktifBilinen=d.aktif;
    $('#telegram-ac-kapa-btn').textContent=telegramAktifBilinen?'🔔 Bildirimler Açık':'🔕 Bildirimler Kapalı';
  });
}

function konteynerPirKaydet(){
  yakinDuzenlendi('kz_pirTutma'); yakinDuzenlendi('kz_pirOnay');
  const tutma=$('#kz_pirTutma').value;
  const onay=$('#kz_pirOnay').value;
  api('/api/konteyner/pir_ayar?tutma='+encodeURIComponent(tutma)+'&onay='+encodeURIComponent(onay)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerSwanKaydet(){
  yakinDuzenlendi('kz_swanTutma'); yakinDuzenlendi('kz_swanOnay');
  const tutma=$('#kz_swanTutma').value;
  const onay=$('#kz_swanOnay').value;
  api('/api/konteyner/swan_ayar?tutma='+encodeURIComponent(tutma)+'&onay='+encodeURIComponent(onay)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerSensorAktifKaydet(){
  const pir = $('#kz_pirEtkin').checked?1:0;
  const kapi = $('#kz_kapiEtkin').checked?1:0;
  const swan = $('#kz_swanEtkin').checked?1:0;
  const duman = $('#kz_dumanEtkin').checked?1:0;
  const gaz = $('#kz_gazEtkin').checked?1:0;
  api('/api/konteyner/sensor_aktif?pir='+pir+'&kapi='+kapi+'&swan='+swan+'&duman='+duman+'&gaz='+gaz).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerMq6TestKaydet(){
  const durum = $('#kz_mq6Test').checked?1:0;
  api('/api/konteyner/mq6_test?durum='+durum).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerGazAyarKaydet(){
  yakinDuzenlendi('kz_gazEsik');
  const esik=$('#kz_gazEsik').value;
  api('/api/konteyner/gaz_ayar?esik='+encodeURIComponent(esik)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function konteynerDumanAyarKaydet(){
  yakinDuzenlendi('kz_dumanEsik');
  const esik=$('#kz_dumanEsik').value;
  api('/api/konteyner/duman_ayar?esik='+encodeURIComponent(esik)).then(()=>{
    $('#kz-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#kz-sonuc').textContent='Hata oluştu'; });
}

function bateryaKorumaToggle(){
  const acik = $('#batarya-koruma-btn').textContent.trim().startsWith('Korumayı Kapat');
  api('/api/batarya/ayar?aktif='+(acik?0:1)).then(()=>{
    $('#batarya-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#batarya-sonuc').textContent='Hata oluştu'; });
}
function bateryaEsikKaydet(){
  yakinDuzenlendi('batarya-kesme'); yakinDuzenlendi('batarya-geri');
  const kesme=$('#batarya-kesme').value;
  const geri=$('#batarya-geri').value;
  api('/api/batarya/ayar?kesme='+encodeURIComponent(kesme)+'&geri='+encodeURIComponent(geri)).then(()=>{
    $('#batarya-sonuc').textContent='Kaydedildi ✓';
  }).catch(()=>{ $('#batarya-sonuc').textContent='Hata oluştu'; });
}

// === SUDEPO ZONU (ESP8266+Nano) AYARLARI - Kalburum'dan koprulu yonetim ===
const szTetikleyiciler=[['kapi1','Sol Kapı'],['kapi2','Sağ Kapı'],['pir','PIR'],['seviye','Su Seviyesi'],['kacak','Kaçak'],['sensor','Sensör Hatası']];
function szGridHtml(prefix){
  return szTetikleyiciler.map(t=>'<label><input type="checkbox" id="'+prefix+'_'+t[0]+'" onchange="szKaydet()">'+t[1]+'</label>').join('');
}
function szOutputGridHtml(prefix){
  return '<label><input type="checkbox" id="'+prefix+'_siren" onchange="szKaydet()">Siren</label><label><input type="checkbox" id="'+prefix+'_lamba" onchange="szKaydet()">Lamba</label>';
}
function szCalcTrigger(prefix){
  let v=0;
  szTetikleyiciler.forEach((t,i)=>{ if($('#'+prefix+'_'+t[0]).checked) v|=(1<<i); });
  return v;
}
function szCalcOutput(prefix){
  let v=0;
  if($('#'+prefix+'_siren').checked) v|=1;
  if($('#'+prefix+'_lamba').checked) v|=2;
  return v;
}
function szUncheckAll(prefix, adlar){ adlar.forEach(a=>{ const el=$('#'+prefix+'_'+a); if(el) el.checked=false; }); }
function szSetTrigger(prefix, mask){
  szTetikleyiciler.forEach((t,i)=>{ const el=$('#'+prefix+'_'+t[0]); if(el) el.checked=((mask&(1<<i))!==0); });
}
function szSetOutput(prefix, mask){
  const s=$('#'+prefix+'_siren'), l=$('#'+prefix+'_lamba');
  if(s) s.checked=((mask&1)!==0);
  if(l) l.checked=((mask&2)!==0);
}
function szAyarlarYukle(){
  $('#sz-grid-sensorEtkin').innerHTML=szGridHtml('sz_Etkin');
  $('#sz-grid-gunduz').innerHTML=szGridHtml('sz_Gunduz');
  $('#sz-grid-gece').innerHTML=szGridHtml('sz_Gece');
  $('#sz-grid-sesli-girdi').innerHTML=szGridHtml('sz_Sesli');
  $('#sz-grid-sesli-cikis').innerHTML=szOutputGridHtml('sz_Sesli');
  $('#sz-grid-sessiz-girdi').innerHTML=szGridHtml('sz_Sessiz');
  $('#sz-grid-sessiz-cikis').innerHTML=szOutputGridHtml('sz_Sessiz');
  $('#sz-grid-onayli-girdi').innerHTML=szGridHtml('sz_Onayli');
  fetch('/api/sudepo_ayarlar').then(r=>r.json()).then(d=>{
    if(!d.basarili){ $('#sz-yukleniyor').textContent='ESP8266\'dan yanıt alınamadı - bağlı olduğundan emin olun.'; return; }
    $('#sz-yukleniyor').style.display='none';
    $('#sz-form').style.display='block';
    const set=(id,v)=>{ const el=$('#'+id); if(el) el.value=v; };
    set('sz_bosMesafe',d.bosMesafe); set('sz_doluMesafe',d.doluMesafe); set('sz_kapasite',d.kapasite); set('sz_depoYatay',d.depoYatay);
    set('sz_alarmYuzde',d.alarmYuzde); set('sz_geceBaslangic',d.geceBaslangic); set('sz_geceBitis',d.geceBitis);
    set('sz_pirPencereSaniye',d.pirPencereSaniye); set('sz_pirMinTetiklenme',d.pirMinTetiklenme);
    set('sz_minDolumLitre',d.minDolumLitre); set('sz_kacakEsikDakika',d.kacakEsikDakika);
    const modEl=$('#sz_mod'+Math.round(d.alarmMod)); if(modEl) modEl.checked=true;
    szSetTrigger('sz_Etkin', d.alarmSensorEtkin==null?255:d.alarmSensorEtkin);
    szSetTrigger('sz_Gunduz', d.triggerGunduz); szSetTrigger('sz_Gece', d.triggerGece);
    szSetTrigger('sz_Sesli', d.alarmMaskSesli); szSetOutput('sz_Sesli', d.alarmOutputSesli);
    szSetTrigger('sz_Sessiz', d.alarmMaskSessiz); szSetOutput('sz_Sessiz', d.alarmOutputSessiz);
    szSetTrigger('sz_Onayli', d.alarmMaskOnayli);
  }).catch(()=>{ $('#sz-yukleniyor').textContent='ESP8266\'dan yanıt alınamadı.'; });
}
function szKaydet(){
  $('#sz-sonuc').textContent='Kaydediliyor...';
  const modSecili=document.querySelector('input[name=sz_alarmMod]:checked');
  const q=new URLSearchParams({
    bosMesafe:$('#sz_bosMesafe').value, doluMesafe:$('#sz_doluMesafe').value, kapasite:$('#sz_kapasite').value, depoYatay:$('#sz_depoYatay').value,
    alarmYuzde:$('#sz_alarmYuzde').value, geceBaslangic:$('#sz_geceBaslangic').value, geceBitis:$('#sz_geceBitis').value,
    pirPencereSaniye:$('#sz_pirPencereSaniye').value, pirMinTetiklenme:$('#sz_pirMinTetiklenme').value,
    minDolumLitre:$('#sz_minDolumLitre').value, kacakEsikDakika:$('#sz_kacakEsikDakika').value,
    alarmMod: modSecili?modSecili.value:1,
    alarmSensorEtkin:szCalcTrigger('sz_Etkin'),
    triggerGunduz:szCalcTrigger('sz_Gunduz'), triggerGece:szCalcTrigger('sz_Gece'),
    alarmMaskSesli:szCalcTrigger('sz_Sesli'), alarmOutputSesli:szCalcOutput('sz_Sesli'),
    alarmMaskSessiz:szCalcTrigger('sz_Sessiz'), alarmOutputSessiz:szCalcOutput('sz_Sessiz'),
    alarmMaskOnayli:szCalcTrigger('sz_Onayli')
  });
  fetch('/api/sudepo_ayarlar/kaydet?'+q.toString()).then(r=>r.json()).then(d=>{
    $('#sz-sonuc').textContent=d.basarili?'Kaydedildi ✓':(d.mesaj||'Hata');
  }).catch(()=>{ $('#sz-sonuc').textContent='Hata'; });
}
szAyarlarYukle();

// === KONTEYNER ZONU - Siren Zamanlama + Mod Senaryolari (kullanici talebi,
// 2026-08-27: "Sudepo'daki gibi mod senaryolari, siren zamanlama ayarlari
// Kalburum'da da olsun") - Sudepo Zonu'ndaki sz* fonksiyonlarinin AYNISI,
// sadece Konteyner'in kendi 4 sensorune (PIR/Swan/Kapi/Duman, gaz haric)
// gore. Yerel (RS485 kopru yok) - dogrudan /api/konteyner/* kullanir.
const kzTetikleyiciler=[['pir','HC505-1 PIR'],['swan','Swan PIR'],['kapi','Kapı'],['duman','Duman']];
function kzGridHtml(prefix){
  return kzTetikleyiciler.map(t=>'<label><input type="checkbox" id="'+prefix+'_'+t[0]+'" onchange="konteynerModSenaryoKaydet()">'+t[1]+'</label>').join('');
}
function kzOutputGridHtml(prefix){
  return '<label><input type="checkbox" id="'+prefix+'_siren" onchange="konteynerModSenaryoKaydet()">Siren</label><label><input type="checkbox" id="'+prefix+'_lamba" onchange="konteynerModSenaryoKaydet()">Lamba</label>';
}
function kzCalcTrigger(prefix){
  let v=0;
  kzTetikleyiciler.forEach((t,i)=>{ if($('#'+prefix+'_'+t[0]).checked) v|=(1<<i); });
  return v;
}
function kzCalcOutput(prefix){
  let v=0;
  if($('#'+prefix+'_siren').checked) v|=1;
  if($('#'+prefix+'_lamba').checked) v|=2;
  return v;
}
function kzSetTrigger(prefix, mask){
  kzTetikleyiciler.forEach((t,i)=>{ const el=$('#'+prefix+'_'+t[0]); if(el) el.checked=((mask&(1<<i))!==0); });
}
function kzSetOutput(prefix, mask){
  const s=$('#'+prefix+'_siren'), l=$('#'+prefix+'_lamba');
  if(s) s.checked=((mask&1)!==0);
  if(l) l.checked=((mask&2)!==0);
}
function konteynerSirenAyarYukle(){
  $('#kz-grid-sesli-girdi').innerHTML=kzGridHtml('kz_Sesli');
  $('#kz-grid-sesli-cikis').innerHTML=kzOutputGridHtml('kz_Sesli');
  $('#kz-grid-sessiz-girdi').innerHTML=kzGridHtml('kz_Sessiz');
  $('#kz-grid-sessiz-cikis').innerHTML=kzOutputGridHtml('kz_Sessiz');
  $('#kz-grid-onayli-girdi').innerHTML=kzGridHtml('kz_Onayli');
  fetch('/api/konteyner/siren_ayar').then(r=>r.json()).then(d=>{
    const set=(id,v)=>{ const el=$('#'+id); if(el) el.value=v; };
    set('kz_sirenGecikme', d.gecikmeSn); set('kz_sirenAtis', d.atisMs); set('kz_sirenAralik', d.aralikSn); set('kz_sirenMax', d.maxDk);
    kzSetTrigger('kz_Sesli', d.maskSesli); kzSetOutput('kz_Sesli', d.outSesli);
    kzSetTrigger('kz_Sessiz', d.maskSessiz); kzSetOutput('kz_Sessiz', d.outSessiz);
    kzSetTrigger('kz_Onayli', d.maskOnayli);
  }).catch(()=>{});
}
function konteynerSirenAyarKaydet(){
  const q=new URLSearchParams({gecikmeSn:$('#kz_sirenGecikme').value, atisMs:$('#kz_sirenAtis').value, aralikSn:$('#kz_sirenAralik').value, maxDk:$('#kz_sirenMax').value});
  $('#kz-sonuc').textContent='Kaydediliyor...';
  fetch('/api/konteyner/siren_ayar/kaydet?'+q.toString()).then(r=>r.json()).then(d=>{ $('#kz-sonuc').textContent=d.basarili?'Kaydedildi ✓':'Hata'; }).catch(()=>{ $('#kz-sonuc').textContent='Hata'; });
}
function konteynerModSenaryoKaydet(){
  const q=new URLSearchParams({
    maskSesli:kzCalcTrigger('kz_Sesli'), outSesli:kzCalcOutput('kz_Sesli'),
    maskSessiz:kzCalcTrigger('kz_Sessiz'), outSessiz:kzCalcOutput('kz_Sessiz'),
    maskOnayli:kzCalcTrigger('kz_Onayli')
  });
  $('#kz-mod-sonuc').textContent='Kaydediliyor...';
  fetch('/api/konteyner/mod_senaryo/kaydet?'+q.toString()).then(r=>r.json()).then(d=>{ $('#kz-mod-sonuc').textContent=d.basarili?'Kaydedildi ✓':'Hata'; }).catch(()=>{ $('#kz-mod-sonuc').textContent='Hata'; });
}
konteynerSirenAyarYukle();

function otaDosyaOnay(){
  const f=$('#otaDosya').files[0];
  if(!f){$('#ota-dosya-sonuc').textContent='Dosya secin';return false;}
  if(!confirm(f.name+' yuklenecek ve cihaz yeniden baslayacak. Emin misin?'))return false;
  $('#ota-dosya-sonuc').textContent='Yukleniyor... (bitince cihaz yeniden baslar)';
  return true;
}
function wifiKaydet(){
  const sel=$('#staSSIDSel').value;
  const s=$('#staSSID').value || sel;
  if(!s){$('#wifi-sonuc').textContent='SSID gerekli';return;}
  api('/api/wifi?ssid='+encodeURIComponent(s)+'&sifre='+encodeURIComponent($('#staPASS').value))
    .then(d=>{$('#wifi-sonuc').textContent=d.mesaj||''; setTimeout(guncelle,4000);});
}
function wifiKaldir(){
  if(!confirm('Kayıtlı ağ kaldırılsın mı?')) return;
  api('/api/wifi?ssid=').then(d=>{
    $('#wifi-sonuc').textContent=d.mesaj||'';
    $('#staSSID').value=''; $('#staPASS').value='';
    guncelle();
  });
}
function wifiScan(){
  $('#wifi-sonuc').textContent='Taranıyor...';
  api('/api/wifi/scan').then(list=>{
    const sel=$('#staSSIDSel');
    sel.innerHTML='<option value="">Ağ seçin...</option>';
    (Array.isArray(list)?list:[]).forEach(n=>{
      const o=document.createElement('option');
      o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+'dBm'+(n.secured?', kilitli':'')+')';
      sel.appendChild(o);
    });
    $('#wifi-sonuc').textContent=(Array.isArray(list)?list.length:0)+' ağ bulundu';
  });
}
function restartSistem(){
  if(confirm('Yeniden başlatılsın mı?'))
    api('/api/restart').then(()=>{$('#wifi-sonuc').textContent='Yeniden başlatılıyor...';}).catch(()=>{});
}
connectSSE();
setInterval(guncelle, 5000); guncelle();
yedekDurumYukle();
setInterval(weatherYukleUI, 5*60*1000); weatherYukleUI();
function alarmLoguYukle(){
  api('/api/alarm/log').then(list=>{
    const sum=$('#alarm-log-summary');
    const el=$('#alarm-log-list'); if(!el) return;
    if(!Array.isArray(list) || list.length===0){
      if(sum) sum.textContent='▸ -';
      el.textContent='Kayıtlı alarm yok';
      return;
    }
    const k0=list[0];
    if(sum) sum.textContent='▸ '+(k0.zaman||'-')+' - '+(k0.baslik||'-')+' ('+(k0.tetikleyen||'-')+')';
    el.innerHTML = list.map(k=>'<div style="padding:3px 0;border-bottom:1px solid var(--border)"><b>'+(k.zaman||'-')+'</b> - '+(k.baslik||'-')+' <span style="color:var(--muted)">('+(k.tetikleyen||'-')+')</span></div>').join('');
  }).catch(()=>{});
}
setInterval(alarmLoguYukle, 15000); alarmLoguYukle();
function alarmLoguTamYukle(){
  const tamEl=$('#alarm-log-tam'), gunEl=$('#alarm-log-gunluk'), ayEl=$('#alarm-log-aylik');
  if(!tamEl) return;
  api('/api/alarm/log/tam').then(list=>{
    if(!Array.isArray(list) || list.length===0){
      tamEl.textContent='Kayıtlı alarm yok'; if(gunEl) gunEl.textContent='-'; if(ayEl) ayEl.textContent='-';
      return;
    }
    // zaman formati "DD/MM/YYYY HH:MM:SS" - gunluk grup icin ilk 10 karakter,
    // aylik grup icin MM/YYYY (3-10 arasi).
    const gunSay={}, aySay={};
    list.forEach(k=>{
      const z=k.zaman||''; const gun=z.substring(0,10); const ay=z.substring(3,10);
      if(gun) gunSay[gun]=(gunSay[gun]||0)+1;
      if(ay) aySay[ay]=(aySay[ay]||0)+1;
    });
    const gunSirali=Object.keys(gunSay).sort().reverse();
    const aySirali=Object.keys(aySay).sort().reverse();
    if(gunEl) gunEl.innerHTML = gunSirali.map(g=>'<div>'+g+': <b>'+gunSay[g]+'</b></div>').join('') || '-';
    if(ayEl) ayEl.innerHTML = aySirali.map(a=>'<div>'+a+': <b>'+aySay[a]+'</b></div>').join('') || '-';
    tamEl.innerHTML = list.slice().reverse().map(k=>'<div style="padding:3px 0;border-bottom:1px solid var(--border)"><b>'+(k.zaman||'-')+'</b> - '+(k.baslik||'-')+' <span style="color:var(--muted)">('+(k.tetikleyen||'-')+')</span></div>').join('');
  }).catch(()=>{ tamEl.textContent='Yüklenemedi'; });
}
function suAkisiYukle(){
  const durEl=$('#su-akisi-durum'), listEl=$('#su-akisi-liste');
  if(!listEl) return;
  api('/api/kayit/liste_goster').then(d=>{
    if(durEl) durEl.textContent='Son yedekleme: '+(d.sonYedek||'-');
    const list=d.kayitlar||[];
    if(!Array.isArray(list) || list.length===0){ listEl.textContent='Kayıt yok (henüz yedek çekilmemiş olabilir)'; return; }
    listEl.innerHTML = '<table class="table"><tr><th>Tarih</th><th>Kişi</th><th>Litre</th><th>Ücret</th><th>Kaynak</th></tr>' +
      list.slice().reverse().map(k=>'<tr><td>'+(k.tarih||'-')+'</td><td>'+(k.kisi||'-')+'</td><td>'+(k.litre||0)+'</td><td>'+(k.ucret||0)+'</td><td>'+(k.kaynak||'-')+'</td></tr>').join('') +
      '</table>';
  }).catch(()=>{ if(listEl) listEl.textContent='Yüklenemedi'; });
}
function suAkisiYenile(){
  $('#su-akisi-durum').textContent='Çekiliyor (RS485)...';
  api('/api/kayit/yedekle').then(()=>{ suAkisiYukle(); }).catch(()=>{ $('#su-akisi-durum').textContent='Hata oluştu'; });
}
if(document.getElementById('bilgiler') && document.getElementById('bilgiler').classList.contains('active')){ alarmLoguTamYukle(); suAkisiYukle(); }
// Son 5 Alarm listesi normalde kapali (tek satir) - tiklayinca (native
// <details>) veya fare ile ustune gelince acilsin.
(function(){
  const det=$('#alarm-log-det'); if(!det) return;
  det.addEventListener('mouseenter', ()=>{ det.open=true; });
  det.addEventListener('mouseleave', ()=>{ det.open=false; });
})();

// === IR KUMANDA - OGRENME/ESLESTIRME ===
const irKomutAdlari={LAMBA_TOGGLE:'Lamba Aç/Kapat (tek tuş)',LAMBA_AC:'Lamba Aç',LAMBA_KAPAT:'Lamba Kapat',ALARM_TOGGLE:'Alarm Aç/Kapat (tek tuş)',ALARM_AC:'Alarm Aç',ALARM_KAPAT:'Alarm Kapat','ALARM_MOD=1':'Mod: Sesli','ALARM_MOD=2':'Mod: Sessiz','ALARM_MOD=3':'Mod: Onaylı',ALARM_SUSTUR:'Sustur',ALARM_ONAYLA:'Onayla',KAPI_TOGGLE:'Kapı Aç/Kapat (tek tuş)',KAPI_AC:'Kapı Aç',KAPI_KAPAT:'Kapı Kapat',PANIK:'Panik'};
let irOgrenPolling=null;
function irListesiYukle(){
  fetch('/api/ir/liste').then(r=>r.json()).then(list=>{
    const el=$('#ir-liste'); if(!el) return;
    if(!Array.isArray(list)||!list.length){ el.innerHTML='<p class="muted">Henüz tanımlı tuş yok.</p>'; return; }
    el.innerHTML=list.map(e=>
      '<div class="row" style="justify-content:space-between;align-items:center;padding:4px 0;border-bottom:1px solid var(--input-border)">'
      +'<span>'+(e.etiket||e.komut)+' <span class="muted" style="font-size:11px">(0x'+e.kod+')</span></span>'
      +'<button class="btn-sil" onclick="irSil(\''+e.kod+'\')">🗑</button></div>'
    ).join('');
  }).catch(()=>{});
}
function irOgrenBaslat(){
  fetch('/api/ir/ogren_baslat').then(()=>{
    $('#ir-ogren-durum').innerHTML='Kumandada bir tuşa basın... (20sn içinde)';
    if(irOgrenPolling) clearInterval(irOgrenPolling);
    irOgrenPolling=setInterval(irOgrenKontrolEt, 800);
  }).catch(()=>{});
}
function irOgrenKontrolEt(){
  const buIstekIcinPolling = irOgrenPolling; // bkz asagidaki "GEC GELEN YANIT" notu
  fetch('/api/ir/ogren_durum').then(r=>r.json()).then(d=>{
    // GEC GELEN YANIT KORUMASI: 800ms'de bir istek atiliyor, fetch()'ler
    // gonderildikleri sirayla DONMEK ZORUNDA DEGIL - kod yakalanip form
    // gosterildikten (irOgrenPolling=null yapilip interval durdurulduktan)
    // SONRA, daha ONCE gonderilmis ama gec gelen bir istek burada hala
    // "hazir:false" ile cozulup formu (select dahil) "Kumandada bir tusa
    // basin..." yazisiyla EZIYORDU - kullanicinin "secim yapamiyorum, liste
    // hemen kayboluyor, hizli davranirsam yakaliyorum" sikayetinin sebebi
    // buydu. Bu istegi atarken gecerli olan polling referansi artik
    // gecersizse (baska bir yanit zaten islenip interval durdurulmus/
    // yeniden baslatilmissa), bu YANITI TAMAMEN YOKSAY.
    if (buIstekIcinPolling !== irOgrenPolling) return;
    const teshis=' <span class="muted" style="font-size:11px">(teşhis: '+d.denemeSayisi+' deneme, son protokol: '+d.sonProtokol+', darbe: '+d.sonRawlen+')</span>';
    if(d.hazir){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      irKodAtamaFormuGoster(d.kod);
    } else if(d.zamanAsimi){
      clearInterval(irOgrenPolling); irOgrenPolling=null;
      $('#ir-ogren-durum').innerHTML='Zaman aşımı, tuş algılanamadı - tekrar deneyin.'+(d.denemeSayisi>0?' (Kumanda algılandı ama protokolü tanınamadı - "'+d.sonProtokol+'" olarak geldi, bu genelde desteklenmeyen/bozuk sinyal demektir.)':' (Hiç IR sinyali algılanmadı - alıcıya doğru mu tutuyorsunuz?)');
    } else {
      $('#ir-ogren-durum').innerHTML='Kumandada bir tuşa basın... (20sn içinde)'+teshis;
    }
  }).catch(()=>{});
}
function irKodAtamaFormuGoster(kod){
  let secenekler='';
  for(const k in irKomutAdlari) secenekler+='<option value="'+k+'">'+irKomutAdlari[k]+'</option>';
  $('#ir-ogren-durum').innerHTML='Kod alındı: <b>0x'+kod+'</b><br>'
    +'<select id="ir-komut-sec" style="margin-top:6px">'+secenekler+'</select><br>'
    +'<input id="ir-not" class="input" placeholder="Not: örn. kumandanın kırmızı tuşu (opsiyonel)" style="margin-top:6px;width:100%;max-width:280px">'
    +'<br><button class="btn btn-yesil" onclick="irKaydet(\''+kod+'\')" style="margin-top:6px">Kaydet</button>';
}
function irKaydet(kod){
  const sel=$('#ir-komut-sec'); const komut=sel.value; const komutAdi=sel.options[sel.selectedIndex].text;
  const not_=($('#ir-not')?.value||'').trim();
  const etiket=not_?(komutAdi+' — '+not_):komutAdi;
  fetch('/api/ir/kaydet?kod='+kod+'&komut='+encodeURIComponent(komut)+'&etiket='+encodeURIComponent(etiket)).then(()=>{
    $('#ir-ogren-durum').innerHTML='Kaydedildi ✓';
    irListesiYukle();
  }).catch(()=>{});
}
function irSil(kod){
  if(!confirm('Bu tuş eşlemesi silinsin mi?')) return;
  fetch('/api/ir/sil?kod='+kod).then(()=>irListesiYukle()).catch(()=>{});
}
irListesiYukle();
</script>
</body>
</html>
  )html";
