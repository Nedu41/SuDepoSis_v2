package com.sudepo.monitor

import android.content.Context
import android.net.wifi.WifiManager
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

// mDNS (.local) bircok Android cihaz/agda guvenilir calismiyor (bkz proje
// hafizasi, Note5'te dogrulandi). WLED'in kendi mobil app'inde kullandigi
// yontemle ayni mantik: yerel /24 alt agi tarayip her IP'nin /api/discover
// endpoint'ine sorup cevaba gore cihazi (sudepo/kalburum) buluyoruz.
object CihazBulucu {
    private const val TIMEOUT_MS = 400
    private const val THREAD_SAYISI = 48
    private const val AWAIT_SANIYE = 6L

    // Telefonun baglandigi WiFi'nin IP'sinden alt ag onekini ("192.168.1.")
    // cikarir. WiFi'a bagli degilse veya bilgi alinamazsa null doner.
    fun kendiSubnetPrefix(context: Context): String? {
        val wifiManager = context.applicationContext
            .getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return null
        val ip = wifiManager.connectionInfo?.ipAddress ?: 0
        if (ip == 0) return null
        return String.format(
            "%d.%d.%d.",
            ip and 0xff,
            (ip shr 8) and 0xff,
            (ip shr 16) and 0xff
        )
    }

    // subnetPrefix + 1..254 araligini paralel tarar, /api/discover cevabinda
    // {"device":"<hedefDevice>"} iceren ilk IP'yi bulur. onSonuc arka plan
    // thread'inden cagrilir (UI guncellemesi cagiran taraf runOnUiThread ile
    // yapmali). Bulunamazsa null.
    fun tara(subnetPrefix: String, hedefDevice: String, onSonuc: (String?) -> Unit) {
        val executor = Executors.newFixedThreadPool(THREAD_SAYISI)
        val bulunanIp = AtomicReference<String?>(null)
        val latch = CountDownLatch(254)
        val imza = "\"device\":\"$hedefDevice\""

        for (i in 1..254) {
            executor.execute {
                if (bulunanIp.get() == null) {
                    val ip = "$subnetPrefix$i"
                    var conn: HttpURLConnection? = null
                    try {
                        conn = URL("http://$ip/api/discover").openConnection() as HttpURLConnection
                        conn.connectTimeout = TIMEOUT_MS
                        conn.readTimeout = TIMEOUT_MS
                        conn.requestMethod = "GET"
                        val cevap = conn.inputStream.bufferedReader().readText()
                        if (cevap.contains(imza)) {
                            bulunanIp.compareAndSet(null, ip)
                        }
                    } catch (e: Exception) {
                        // Erisilemeyen/ilgisiz host - beklenen durum, yoksay
                    } finally {
                        conn?.disconnect()
                    }
                }
                latch.countDown()
            }
        }

        Thread {
            latch.await(AWAIT_SANIYE, TimeUnit.SECONDS)
            executor.shutdownNow()
            onSonuc(bulunanIp.get())
        }.start()
    }
}
