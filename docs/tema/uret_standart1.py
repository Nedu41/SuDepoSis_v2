import urllib.parse
def svg_uri(w,h,freq,octv,alpha,mat):
    svg=(f"<svg xmlns='http://www.w3.org/2000/svg' width='{w}' height='{h}'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='{freq}' numOctaves='{octv}' stitchTiles='stitch'/>"
         f"<feColorMatrix values='{mat}'/></filter><rect width='100%' height='100%' filter='url(#n)'/></svg>")
    return "url('data:image/svg+xml,"+urllib.parse.quote(svg,safe="")+"')"
# lekeli boya (buyuk bloklar): alfa = R kanali * k
leke=svg_uri(700,700,'.007',3,0,'0 0 0 0 .75  0 0 0 0 .78  0 0 0 0 .85  1.1 0 0 0 -.38')
leke2=svg_uri(500,500,'.012',3,0,'0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  1.3 0 0 0 -.55')
gren=svg_uri(160,160,'.9',2,0,'0 0 0 0 1  0 0 0 0 1  0 0 0 0 1  0 0 0 .16 0')
def vida(pos): return f"radial-gradient(circle 4.5px at {pos},#a3a9b4 0,#59606c 38%,#16181d 75%,transparent 82%)"
vidalar=",".join(vida(x) for x in ["12px 12px","calc(100% - 12px) 12px","12px calc(100% - 12px)","calc(100% - 12px) calc(100% - 12px)"])
pas=lambda x,w,a: f"linear-gradient(180deg,rgba(150,72,26,{a}),rgba(150,72,26,0) 42%) {x} 0/{w} 100% no-repeat"
body_bg=",".join([
 "radial-gradient(ellipse at 50% 38%,rgba(0,0,0,0) 30%,rgba(0,0,0,.85) 100%)",
 pas("7%","5px",".30"),pas("23%","3px",".22"),pas("58%","6px",".26"),pas("81%","4px",".28"),pas("94%","3px",".2"),
 "repeating-linear-gradient(118deg,transparent 0 170px,rgba(255,255,255,.05) 171px,transparent 173px,transparent 310px,rgba(255,255,255,.035) 311px,transparent 312px)",
 leke,leke2,gren])
CSS=("/* === PANO TEMASI (ESP32 web_ui.h ve Sudepo main.cpp handleCSS AYNI blok - birini degistirince digerini de guncelle) ===\n"
"   Eski, koyu boyali elektrik panosu: lekeli boya + pas izi + citik + gren + kenar kararmasi; kartlar cerceve+civata plakasi, basliklar yaslanmis etiket. */\n"
":root,.dark{--bg:#060607;--card:#0b0c0f;--text:#e5e7eb;--muted:#9ca3af;--border:#3a3f48;--border-strong:#5b616c;--primary:#60a5fa;--accent:#34d399;--warn:#fbbf24;--danger:#f87171;--danger-bg:#3a2222;--danger-bg-t:rgba(58,34,34,.6);--tab-bg:#101216;--shadow:0 2px 8px rgba(0,0,0,.8);--grid-dot:rgba(150,160,180,.05)}\n"
"body{color-scheme:dark;background-color:#08090a;background-attachment:fixed;background-image:"+body_bg+";background-size:auto,auto,auto,auto,auto,auto,700px 700px,500px 500px,160px 160px}\n"
".card,details.card{background-color:transparent;border-width:4px;border-style:solid;border-top-color:#6a707c;border-right-color:#4b515c;border-bottom-color:#3a3f48;border-radius:6px;"
"box-shadow:0 0 0 2px #000,inset 0 0 0 1px #000,inset 0 2px 0 rgba(255,255,255,.07),inset 0 0 24px rgba(0,0,0,.6),0 5px 14px rgba(0,0,0,.9);"
"background-repeat:no-repeat;background-image:"+vidalar+"}\n"
".card h3,.card>summary,details.card>summary{text-transform:uppercase;letter-spacing:1.6px;font-family:Consolas,'DejaVu Sans Mono','Courier New',monospace;font-size:13px;font-weight:700;color:#dcd2ac;text-shadow:0 1px 0 #000,0 0 7px rgba(220,210,172,.18)}\n"
".card h3{padding-bottom:6px;border-bottom:1px dashed #454a54}\n"
".btn{padding:10px 12px;border:1px solid #000;border-radius:9px;cursor:pointer;font-weight:600;background-color:#020203;background-image:linear-gradient(180deg,rgba(255,255,255,.09),rgba(255,255,255,0) 45%,rgba(0,0,0,.4));color:#a3a8b2;box-shadow:inset 0 1px 0 rgba(255,255,255,.14),0 1px 2px rgba(0,0,0,.5);transition:transform .08s ease,box-shadow .25s,color .25s;filter:none}\n"
".btn:active{transform:translateY(2px);box-shadow:inset 0 2px 5px rgba(0,0,0,.6);filter:none}\n"
"/* Kapaliyken TUM butonlar sonuk gri (LED sonuk hissi); anlam tonu (--t) sadece basilirken ve durum acikken (btn-on) yanar: primary/mavi=genel islem, accent/yesil=olumlu, warn/turuncu=dikkat, danger/kirmizi=tehlikeli */\n"
".btn{color:#7d828c}\n"
".btn-primary,.btn-mavi{--t:96,165,250}.btn-accent,.btn-yesil{--t:34,230,96}.btn-danger,.btn-kirmizi{--t:255,85,85}.btn-warn,.btn-turuncu{--t:255,190,70}\n"
".btn:active{color:rgb(var(--t,200,205,215));text-shadow:0 0 5px rgb(var(--t,200,205,215)),0 0 12px rgba(var(--t,200,205,215),.6)}\n"
".btn-primary,.btn-accent,.btn-danger,.btn-warn,.btn-yesil,.btn-turuncu,.btn-mavi,.btn-kirmizi{background-color:#020203}\n"
"/* Durum (acik/aktif): zemin siyah, yazi + cevre --t tonunda neon, yavasca nefes alir. Varsayilan yesil; ton-kirmizi=tehlike aktif, ton-amber=uyari durumu, ton-mavi=bilgi/bahce kapisi */\n"
".btn.btn-on{--t:34,230,96;font-weight:800;color:rgb(var(--t));text-shadow:0 0 4px rgb(var(--t)),0 0 10px rgba(var(--t),.8),0 0 20px rgba(var(--t),.6);box-shadow:inset 0 1px 0 rgba(255,255,255,.14),0 0 6px 1px rgba(var(--t),.6),0 0 22px 5px rgba(var(--t),.35);animation:btnNefes 3.6s ease-in-out infinite}\n"
".btn.btn-on.ton-kirmizi{--t:255,85,85}.btn.btn-on.ton-amber{--t:255,190,70}.btn.btn-on.ton-mavi{--t:70,150,255}.btn.btn-on.sabit{animation:none}\n"
"@keyframes btnNefes{50%{box-shadow:inset 0 1px 0 rgba(255,255,255,.14),0 0 5px 1px rgba(var(--t),.45),0 0 16px 3px rgba(var(--t),.22);text-shadow:0 0 3px rgb(var(--t)),0 0 7px rgba(var(--t),.7),0 0 14px rgba(var(--t),.5)}}\n"
"@media(prefers-reduced-motion:reduce){.btn.btn-on{animation:none}}\n"
".nav,.sekmeler,.dark .sekmeler{background:rgba(5,5,6,.35)}\n"
"body .nav button,body .sekme-btn{background:transparent;border:1px solid #3a3f48;color:#a3a8b2;box-shadow:none}\n"
"body .nav button.active,body .sekme-btn.aktif{background:transparent;color:#9ec3f5;border-color:#60a5fa;text-shadow:0 0 6px rgba(96,165,250,.8);box-shadow:0 0 8px rgba(96,165,250,.45)}\n"
"input,select,.input,textarea{background:#060708;border:1px solid #3a3f48;color:#e5e7eb}\n"
".tema-btn{display:none}\n"
"/* === /PANO TEMASI === */\n")
open("standart1_pano_temasi.css","w",encoding="utf-8").write(CSS)
print(len(CSS))
