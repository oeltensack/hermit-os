// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
package de.pagurus.app

import android.annotation.SuppressLint
import android.content.Context
import android.net.Uri
import android.util.Base64
import android.webkit.CookieManager
import android.webkit.JavascriptInterface
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import java.io.ByteArrayInputStream

/** Volle Cloud-Oberflaeche. Zu Hause direkt; unterwegs laufen alle Anfragen ueber das Portal. */
private const val RELAY_HOST = "cloud.pagurus.invalid"

// Ersetzt fetch() der Cloud-Seite: Anfragen gehen ueber die App (und von dort ueber das Portal)
private const val FETCH_JS = """<script>(function(){var P={},n=0;
window.__pgDone=function(id,st,ct,b64){var p=P[id];delete P[id];if(!p)return;var b=atob(b64),u=new Uint8Array(b.length);
for(var i=0;i<b.length;i++)u[i]=b.charCodeAt(i);p(new Response((st==204||st==304)?null:u,{status:st||502,headers:{'Content-Type':ct||'application/json'}}))};
var of=window.fetch;window.fetch=function(u,o){o=o||{};var url=typeof u==='string'?u:u.url;var a=new URL(url,location.href);
if(a.host!==location.host)return of(u,o);var h=o.headers||{},ct=h['Content-Type']||h['content-type']||'application/json';
return new Promise(function(res){var id=++n;P[id]=res;PagurusApp.req(id,o.method||'GET',a.pathname+a.search,o.body==null?null:String(o.body),ct)})}})();</script>"""

class WebBridge(private val ctx: Context, private val web: WebView) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    @JavascriptInterface
    fun req(id: Int, method: String, path: String, body: String?, ctype: String) {
        scope.launch {
            val (st, ct, b64) = try {
                val r = Conn.call(ctx, method, path, body, ctype)
                Triple(r.code, r.ctype, Base64.encodeToString(r.bytes, Base64.NO_WRAP))
            } catch (e: Exception) {
                Triple(502, "application/json", Base64.encodeToString(
                    org.json.JSONObject().put("detail", e.message ?: "Fehler").toString().toByteArray(), Base64.NO_WRAP))
            }
            withContext(Dispatchers.Main) { web.evaluateJavascript("window.__pgDone($id,$st,'${ct.replace("'", "")}','$b64')", null) }
        }
    }
}

@SuppressLint("SetJavaScriptEnabled")
@Composable
fun CloudWeb() {
    val ctx = LocalContext.current
    var mode by remember { mutableStateOf<Conn.Mode?>(null) }
    LaunchedEffect(Unit) {
        val m = Conn.mode(ctx)
        val p = Prefs(ctx)
        if (m == Conn.Mode.LOCAL) {               // Anmeldung der App an die Seite weitergeben
            try { if (p.cloudToken.isEmpty() && p.cloudPass.isNotEmpty()) withContext(Dispatchers.IO) { Conn.loginLocal(p) } } catch (e: Exception) { }
            if (p.cloudToken.isNotEmpty()) CookieManager.getInstance().setCookie(p.cloudBase, "hc=${p.cloudToken}")
        }
        mode = m
    }
    when (mode) {
        null -> CircularProgressIndicator()
        Conn.Mode.NONE -> Text("Keine Verbindung – Adressen in den Einstellungen prüfen.")
        else -> AndroidView(modifier = Modifier.fillMaxSize(), factory = { c ->
            WebView(c).apply {
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                setBackgroundColor(0xFF0F1419.toInt())
                val relay = mode == Conn.Mode.PORTAL
                if (relay) addJavascriptInterface(WebBridge(c.applicationContext, this), "PagurusApp")
                webViewClient = object : WebViewClient() {
                    override fun shouldInterceptRequest(view: WebView, req: WebResourceRequest): WebResourceResponse? {
                        if (!relay || req.url.host != RELAY_HOST) return null
                        val path = (req.url.encodedPath ?: "/") + (req.url.encodedQuery?.let { "?$it" } ?: "")
                        return try {
                            val r = runBlocking { Conn.call(c.applicationContext, "GET", path) }
                            val mime = r.ctype.substringBefore(';').trim().ifEmpty { "application/octet-stream" }
                            var bytes = r.bytes
                            if (mime == "text/html") bytes = String(bytes).replaceFirst("<head>", "<head>$FETCH_JS").toByteArray()
                            WebResourceResponse(mime, "utf-8", r.code, if (r.code < 400) "OK" else "Fehler",
                                mapOf("Cache-Control" to "no-store"), ByteArrayInputStream(bytes))
                        } catch (e: Exception) {
                            WebResourceResponse("text/plain", "utf-8", 502, "Fehler", emptyMap(),
                                ByteArrayInputStream("Cloud nicht erreichbar: ${e.message}".toByteArray()))
                        }
                    }
                    override fun shouldOverrideUrlLoading(view: WebView, req: WebResourceRequest): Boolean {
                        val h = req.url.host ?: return false
                        val own = h == RELAY_HOST || Uri.parse(Prefs(c).cloudBase).host == h
                        if (!own) c.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, req.url))
                        return !own
                    }
                }
                loadUrl(if (relay) "https://$RELAY_HOST/" else Prefs(c).cloudBase + "/")
            }
        })
    }
}
