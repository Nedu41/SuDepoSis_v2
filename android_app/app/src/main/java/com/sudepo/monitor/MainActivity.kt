package com.sudepo.monitor

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Bundle
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

    // mDNS (.local) cozumu Android'de guvenilir degil - basarisiz olunca WebView
    // eski sayfayi ekranda birakiyordu ("Sudepo'ya basiyorum ama Konteyner kaliyor"
    // sikayetinin sebebi buydu). Artik hata alinca once AP IP'si deneniyor, o da
    // olmazsa acik bir hata ekrani gosteriliyor - sessizce eski sayfada kalinmiyor.
    private var pendingHost = ""
    private var apFallbackDenendi = false
    private var sudepoAktif = true

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
                if (failedHost != pendingHost) return // eski/ilgisiz bir istek

                if (!apFallbackDenendi) {
                    apFallbackDenendi = true
                    pendingHost = Uri.parse(apFallbackUrl).host ?: ""
                    webView.loadUrl(apFallbackUrl)
                } else {
                    showBaglantiHatasi()
                }
            }
        }

        swipeRefresh.setOnRefreshListener {
            if (sudepoAktif) openTarget(urlSudepo, btnSudepo, btnKonteyner)
            else openTarget(urlKonteyner, btnKonteyner, btnSudepo)
        }

        btnSudepo.setOnClickListener {
            sudepoAktif = true
            openTarget(urlSudepo, btnSudepo, btnKonteyner)
        }
        btnKonteyner.setOnClickListener {
            sudepoAktif = false
            openTarget(urlKonteyner, btnKonteyner, btnSudepo)
        }

        val lastUrl = prefs.getString("last_url", urlSudepo) ?: urlSudepo
        if (lastUrl == urlKonteyner) {
            sudepoAktif = false
            openTarget(urlKonteyner, btnKonteyner, btnSudepo)
        } else {
            sudepoAktif = true
            openTarget(urlSudepo, btnSudepo, btnKonteyner)
        }
    }

    private fun openTarget(url: String, activeBtn: Button, inactiveBtn: Button) {
        prefs.edit().putString("last_url", url).apply()
        activeBtn.setBackgroundColor(0xFF00BCD4.toInt())
        activeBtn.setTextColor(0xFF0D1117.toInt())
        inactiveBtn.setBackgroundColor(0xFF263238.toInt())
        inactiveBtn.setTextColor(0xFFFFFFFF.toInt())

        pendingHost = Uri.parse(url).host ?: ""
        apFallbackDenendi = false
        webView.loadUrl(url)
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
