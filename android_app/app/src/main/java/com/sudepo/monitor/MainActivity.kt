package com.sudepo.monitor

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.appcompat.app.AppCompatActivity
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout
import android.widget.Button
import android.widget.ProgressBar

class MainActivity : AppCompatActivity() {

    private lateinit var webView: WebView
    private lateinit var swipeRefresh: SwipeRefreshLayout
    private lateinit var progressBar: ProgressBar
    private lateinit var btnSudepo: Button
    private lateinit var btnKonteyner: Button

    private val urlSudepo = "http://sudepo.local"
    private val urlKonteyner = "http://kalburum.local"

    // ESP32 (kalburum) ve ESP8266 (sudepo) kendi AP'lerinde AYNI varsayilan IP'yi
    // kullanir (192.168.4.1) - telefon o an hangisinin AP'sine bagliysa oraya gider.
    private val apFallbackUrl = "http://192.168.4.1"

    // Ev agindaki bilinen son IP'ler (router DHCP rezervasyonu onerilir, degisirse
    // burasi guncellenmeli). mDNS (.local) bircok Android cihazda/agda guvenilir
    // calismiyor (Note5'te dogrulandi: IP ile aciliyor, .local acilmiyordu).
    private val evIpSudepo = "http://192.168.1.138"
    private val evIpKonteyner = "http://192.168.1.165"

    // mDNS (.local) cozumu Android'de guvenilir degil - basarisiz olunca WebView
    // eski sayfayi ekranda birakiyordu ("Sudepo'ya basiyorum ama Konteyner kaliyor"
    // sikayetinin sebebi buydu). Artik hata alinca sirayla AP IP, sonra bilinen ev
    // IP'si deneniyor, hicbiri olmazsa acik bir hata ekrani gosteriliyor - sessizce
    // eski sayfada kalinmiyor.
    private var pendingHost = ""
    private var pendingDevice = ""
    private var fallbackQueue: MutableList<String> = mutableListOf()
    private var taramaYapildiBuTurda = false
    private var sudepoAktif = true

    // WebView'in kendi baglanti zaman asimi, olu/eski bir LAN IP'sine
    // baglanmaya calisirken cok uzun surebiliyor (TCP retry'lari, bazen
    // 20-30sn+) - bu da altta calisan guvenilir ag taramasina (CihazBulucu)
    // sira gelmeden kullanicinin "hala eski IP'de takili" hissetmesine sebep
    // oluyordu. Her deneme icin kisa, elle bir zaman asimi koyup suresi
    // dolunca WebView'i beklemeden bir sonraki fallback'e (sonunda taramaya)
    // geciyoruz - WLED'in yaklasimindaki hizli-basarisiz-ol prensibiyle ayni.
    private val mainHandler = Handler(Looper.getMainLooper())
    private var timeoutRunnable: Runnable? = null
    private val YUKLEME_ZAMAN_ASIMI_MS = 2000L

    private fun loadWithTimeout(url: String, host: String) {
        timeoutRunnable?.let { mainHandler.removeCallbacks(it) }
        pendingHost = host
        webView.loadUrl(url)
        val runnable = Runnable { hataGeldi(host) }
        timeoutRunnable = runnable
        mainHandler.postDelayed(runnable, YUKLEME_ZAMAN_ASIMI_MS)
    }

    private val prefs by lazy { getSharedPreferences("sudepo_monitor", MODE_PRIVATE) }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        webView = findViewById(R.id.webView)
        swipeRefresh = findViewById(R.id.swipeRefresh)
        progressBar = findViewById(R.id.progressBar)
        btnSudepo = findViewById(R.id.btnSudepo)
        btnKonteyner = findViewById(R.id.btnKonteyner)

        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.loadWithOverviewMode = true
        webView.settings.useWideViewPort = true

        webView.webViewClient = object : WebViewClient() {
            override fun onPageFinished(view: WebView?, url: String?) {
                super.onPageFinished(view, url)
                timeoutRunnable?.let { mainHandler.removeCallbacks(it) }
                progressBar.visibility = android.view.View.GONE
                swipeRefresh.isRefreshing = false
            }

            override fun onPageStarted(view: WebView?, url: String?, favicon: android.graphics.Bitmap?) {
                super.onPageStarted(view, url, favicon)
                progressBar.visibility = android.view.View.VISIBLE
            }

            override fun onReceivedError(view: WebView?, request: WebResourceRequest?, error: WebResourceError?) {
                super.onReceivedError(view, request, error)
                if (request?.isForMainFrame != true) return
                val failedHost = request.url?.host ?: return
                hataGeldi(failedHost)
            }

            // API 23 oncesi (Android 6.0 alti) sistem SADECE bu eski overload'i
            // cagirir, yukaridaki WebResourceRequest'li versiyon hic tetiklenmez.
            // minSdk 21 oldugu icin bu ikisi birlikte olmali - eksikse eski
            // cihazlarda fallback/tarama mantigi sessizce hic calismiyordu.
            @Suppress("DEPRECATION", "OverridingDeprecatedMember")
            override fun onReceivedError(
                view: WebView?,
                errorCode: Int,
                description: String?,
                failingUrl: String?
            ) {
                super.onReceivedError(view, errorCode, description, failingUrl)
                val failedHost = failingUrl?.let { Uri.parse(it).host } ?: return
                hataGeldi(failedHost)
            }
        }

        swipeRefresh.setOnRefreshListener {
            if (sudepoAktif) openTarget(urlSudepo, evIpSudepo, "sudepo", btnSudepo, btnKonteyner)
            else openTarget(urlKonteyner, evIpKonteyner, "kalburum", btnKonteyner, btnSudepo)
        }

        btnSudepo.setOnClickListener {
            sudepoAktif = true
            openTarget(urlSudepo, evIpSudepo, "sudepo", btnSudepo, btnKonteyner)
        }
        btnKonteyner.setOnClickListener {
            sudepoAktif = false
            openTarget(urlKonteyner, evIpKonteyner, "kalburum", btnKonteyner, btnSudepo)
        }

        val lastUrl = prefs.getString("last_url", urlSudepo) ?: urlSudepo
        if (lastUrl == urlKonteyner) {
            sudepoAktif = false
            openTarget(urlKonteyner, evIpKonteyner, "kalburum", btnKonteyner, btnSudepo)
        } else {
            sudepoAktif = true
            openTarget(urlSudepo, evIpSudepo, "sudepo", btnSudepo, btnKonteyner)
        }
    }

    private fun hataGeldi(failedHost: String) {
        if (failedHost != pendingHost) return // eski/ilgisiz bir istek
        timeoutRunnable?.let { mainHandler.removeCallbacks(it) }

        if (fallbackQueue.isNotEmpty()) {
            val nextUrl = fallbackQueue.removeAt(0)
            loadWithTimeout(nextUrl, Uri.parse(nextUrl).host ?: "")
        } else if (!taramaYapildiBuTurda) {
            taramaYapildiBuTurda = true
            taramaBaslat()
        } else {
            showBaglantiHatasi()
        }
    }

    private fun openTarget(
        url: String,
        evIpUrl: String,
        deviceId: String,
        activeBtn: Button,
        inactiveBtn: Button
    ) {
        prefs.edit().putString("last_url", url).apply()
        activeBtn.setBackgroundColor(0xFF00BCD4.toInt())
        activeBtn.setTextColor(0xFF0D1117.toInt())
        inactiveBtn.setBackgroundColor(0xFF263238.toInt())
        inactiveBtn.setTextColor(0xFFFFFFFF.toInt())

        pendingDevice = deviceId
        taramaYapildiBuTurda = false

        // Onceki taramada bulunan IP varsa, guvenilmez .local'den once onu dene -
        // en hizli ve en olasi calisan yol bu. evIpUrl (bilinen calisan ev IP'si)
        // apFallbackUrl'den (192.168.4.1 - sadece cihazin kendi AP'sine
        // baglaniyken anlamli) once denenir: ev agindayken 4.1'e baglanma
        // denemesi cevap alamayip uzun sure zaman asimina ugrayabilir.
        val cachedIp = prefs.getString("cached_ip_$deviceId", null)
        val ilkUrl = if (cachedIp != null) "http://$cachedIp" else evIpUrl
        fallbackQueue = if (cachedIp != null) {
            mutableListOf(evIpUrl, url, apFallbackUrl)
        } else {
            mutableListOf(url, apFallbackUrl)
        }

        loadWithTimeout(ilkUrl, Uri.parse(ilkUrl).host ?: "")
    }

    // Tum sabit adresler (.local, AP IP, bilinen ev IP'si) basarisiz olunca son
    // care olarak yerel /24 agini tarar (bkz CihazBulucu - WLED'in yaklasimi).
    // Bulunursa IP cache'lenir, bir sonraki acilista dogrudan denenir.
    private fun taramaBaslat() {
        val subnet = CihazBulucu.kendiSubnetPrefix(this)
        if (subnet == null) {
            showBaglantiHatasi()
            return
        }
        val hedefDevice = pendingDevice
        CihazBulucu.tara(subnet, hedefDevice) { bulunanIp ->
            runOnUiThread {
                if (bulunanIp != null && pendingDevice == hedefDevice) {
                    prefs.edit().putString("cached_ip_$hedefDevice", bulunanIp).apply()
                    fallbackQueue = mutableListOf()
                    pendingHost = bulunanIp
                    webView.loadUrl("http://$bulunanIp")
                } else if (pendingDevice == hedefDevice) {
                    showBaglantiHatasi()
                }
            }
        }
    }

    private fun showBaglantiHatasi() {
        progressBar.visibility = android.view.View.GONE
        swipeRefresh.isRefreshing = false
        val html = """
            <html><body style="background:#0d1117;color:#fff;font-family:sans-serif;
            display:flex;align-items:center;justify-content:center;height:90vh;
            text-align:center;padding:24px;box-sizing:border-box;">
            <div><p style="font-size:18px;">⚠ Bağlanılamadı</p>
            <p style="color:#9aa5b1;">Ev ağına bağlı değilsiniz ya da cihazın kendi
            WiFi ağına (AP) bağlı değilsiniz. Aşağı kaydırarak tekrar deneyin.</p></div>
            </body></html>
        """.trimIndent()
        webView.loadDataWithBaseURL(null, html, "text/html", "UTF-8", null)
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK && webView.canGoBack()) {
            webView.goBack()
            return true
        }
        return super.onKeyDown(keyCode, event)
    }
}
