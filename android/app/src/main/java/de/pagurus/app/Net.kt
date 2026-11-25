// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
package de.pagurus.app

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import android.util.Base64
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

// ---------------------------------------------------------------- Einstellungen
class Prefs(ctx: Context) {
    val sp: SharedPreferences = ctx.applicationContext.getSharedPreferences("pagurus", Context.MODE_PRIVATE)
    var cloudUrl: String get() = sp.getString("cloud_url", "")!!; set(v) = sp.edit().putString("cloud_url", v.trim().trimEnd('/')).apply()
    var cloudUser: String get() = sp.getString("cloud_user", "admin")!!; set(v) = sp.edit().putString("cloud_user", v.trim()).apply()
    var cloudPass: String get() = sp.getString("cloud_pass", "")!!; set(v) = sp.edit().putString("cloud_pass", v).apply()
    var cloudToken: String get() = sp.getString("cloud_token", "")!!; set(v) = sp.edit().putString("cloud_token", v).apply()
    var portalUrl: String get() = sp.getString("portal_url", "")!!; set(v) = sp.edit().putString("portal_url", v.trim()).apply()
    var portalPass: String get() = sp.getString("portal_pass", "")!!; set(v) = sp.edit().putString("portal_pass", v).apply()
    var portalCookie: String get() = sp.getString("portal_cookie", "")!!; set(v) = sp.edit().putString("portal_cookie", v).apply()
    var push: Boolean get() = sp.getBoolean("push", false); set(v) = sp.edit().putBoolean("push", v).apply()
    var lastAlert: Long get() = sp.getLong("last_alert", -1); set(v) = sp.edit().putLong("last_alert", v).apply()
    var favs: List<Fav>
        get() = Fav.list(sp.getString("favs", "[]")!!)
        set(v) = sp.edit().putString("favs", Fav.json(v)).apply()
    fun tile(n: Int): Fav? = sp.getString("tile$n", null)?.let { Fav.one(it) }
    fun setTile(n: Int, f: Fav?) = sp.edit().putString("tile$n", f?.let { Fav.json(listOf(it)) }).apply()
    var states: JSONObject     // letzter bekannter Zustand (an/aus) fuer Widget und Kacheln
        get() = JSONObject(sp.getString("states", "{}")!!)
        set(v) = sp.edit().putString("states", v.toString()).apply()

    val portalApi: String get() {
        var u = portalUrl.trim()
        if (u.isEmpty()) return ""
        if (!u.startsWith("http")) u = "https://$u"
        return if (u.endsWith(".php")) u else u.trimEnd('/') + "/api.php"
    }
    val cloudBase: String get() {
        var u = cloudUrl.trim().trimEnd('/')
        if (u.isNotEmpty() && !u.startsWith("http")) u = "http://$u"
        if (u.isNotEmpty() && Uri.parse(u).port == -1 && u.startsWith("http://")) u += ":8080"
        return u
    }
}

/** Favorit fuer Widget und Kacheln: Szene oder Geraet. */
data class Fav(val type: String, val id: String, val name: String) {
    val key get() = "$type:$id"
    companion object {
        fun list(s: String): List<Fav> = try {
            val a = JSONArray(s); (0 until a.length()).map { a.getJSONObject(it).let { o -> Fav(o.getString("t"), o.getString("id"), o.optString("n")) } }
        } catch (e: Exception) { emptyList() }
        fun one(s: String) = list(s).firstOrNull()
        fun json(l: List<Fav>) = JSONArray(l.map { JSONObject().put("t", it.type).put("id", it.id).put("n", it.name) }).toString()
    }
}

// ---------------------------------------------------------------- Datenmodell
data class Device(val j: JSONObject) {
    val id: String = j.optString("id")
    val name: String = j.optString("name").ifEmpty { id }
    val room: String = j.optString("room")
    val online = j.optBoolean("online")
    val comp: String = j.optString("comp")
    val model: String = j.optString("model")
    val st: JSONObject = j.optJSONObject("status") ?: JSONObject()
    val lastSeen = j.optDouble("last_seen", 0.0)
    val isCover get() = comp.startsWith("cover:")
    val isSensor get() = comp == "sensor:0"
    val meterOnly get() = st.optBoolean("meter_only")
    val hasOutput get() = st.has("output") && !isCover && !isSensor
    val on get() = st.optBoolean("output")
    val switchable get() = hasOutput && !meterOnly && online
    fun num(k: String): Double? = if (st.has(k) && !st.isNull(k)) st.optDouble(k).takeIf { !it.isNaN() } else null
    fun sub(o: String, k: String): Double? = st.optJSONObject(o)?.let { if (it.has(k)) it.optDouble(k) else null }
    val apower get() = num("apower")
    val tempC get() = sub("temperature", "tC")
    val humidity get() = sub("humidity", "rh")
    val todayWh get() = if (j.has("today_wh")) j.optDouble("today_wh") else null
    val pos: Int? get() = if (st.has("current_pos") && !st.isNull("current_pos")) st.optInt("current_pos") else null
    val coverState: String get() = st.optString("state")
    val posControl get() = st.optBoolean("pos_control") || pos != null
}

data class Scene(val id: Int, val name: String)
data class Alert(val id: Long, val ts: Double, val kind: String, val name: String, val text: String, val ack: Boolean)

val KIND = mapOf("overpower" to "Überlast", "overtemp" to "Übertemperatur", "offline" to "Offline", "online" to "Online",
    "reset" to "Reset", "overvoltage" to "Überspannung", "update" to "Update", "finished" to "Fertig",
    "reminder" to "Erinnerung", "rule" to "Regel", "fridge" to "Kühlgerät")
val QUIET = setOf("online", "reset", "update")

class ApiError(msg: String, val code: Int = 0) : Exception(msg)

// ---------------------------------------------------------------- Verbindung
/** Zu Hause direkt zur Cloud, sonst ueber das Portal (Anfragen werden dort an die Cloud durchgereicht). */
object Conn {
    enum class Mode { LOCAL, PORTAL, NONE }

    private val JSON_T = "application/json".toMediaType()
    val http: OkHttpClient = OkHttpClient.Builder()
        .connectTimeout(8, TimeUnit.SECONDS).readTimeout(45, TimeUnit.SECONDS).build()
    private val probeHttp = http.newBuilder().connectTimeout(1500, TimeUnit.MILLISECONDS)
        .readTimeout(2500, TimeUnit.MILLISECONDS).build()
    private val lock = Mutex()
    @Volatile var mode = Mode.NONE; private set
    @Volatile private var checkedAt = 0L

    fun invalidate() { checkedAt = 0 }

    suspend fun mode(ctx: Context): Mode = lock.withLock {
        if (System.currentTimeMillis() - checkedAt < 30_000 && mode != Mode.NONE) return mode
        val p = Prefs(ctx)
        mode = withContext(Dispatchers.IO) {
            when {
                p.cloudBase.isNotEmpty() && probeLocal(p.cloudBase) -> Mode.LOCAL
                p.portalApi.isNotEmpty() -> Mode.PORTAL
                else -> Mode.NONE
            }
        }
        checkedAt = System.currentTimeMillis()
        mode
    }

    private fun probeLocal(base: String): Boolean = try {
        probeHttp.newCall(Request.Builder().url("$base/api/info/public").build()).execute().use { it.code < 500 }
    } catch (e: Exception) { false }

    data class Resp(val code: Int, val ctype: String, val bytes: ByteArray) {
        val text get() = String(bytes)
        fun json() = JSONObject(text)
        fun arr() = JSONArray(text)
    }

    /** Aufruf der Cloud-Schnittstelle, egal ob zu Hause oder unterwegs. */
    suspend fun call(ctx: Context, method: String, path: String, body: String? = null,
                     ctype: String = "application/json"): Resp = withContext(Dispatchers.IO) {
        val p = Prefs(ctx)
        when (mode(ctx)) {
            Mode.LOCAL -> try { local(p, method, path, body, ctype, true) } catch (e: java.io.IOException) {
                invalidate(); throw ApiError("Cloud nicht erreichbar: ${e.message}")
            }
            Mode.PORTAL -> try { relay(p, method, path, body, ctype, true) } catch (e: java.io.IOException) {
                invalidate(); throw ApiError("Portal nicht erreichbar: ${e.message}")
            }
            Mode.NONE -> throw ApiError("Keine Verbindung – Adressen in den Einstellungen prüfen")
        }
    }

    private fun local(p: Prefs, method: String, path: String, body: String?, ctype: String, retry: Boolean): Resp {
        val rb = body?.toRequestBody(ctype.toMediaType()) ?: if (method == "POST" || method == "PUT") "".toRequestBody(JSON_T) else null
        val rq = Request.Builder().url(p.cloudBase + path).method(method, rb)
        if (p.cloudToken.isNotEmpty()) rq.header("Authorization", "Bearer ${p.cloudToken}")
        http.newCall(rq.build()).execute().use { r ->
            val res = Resp(r.code, r.header("Content-Type") ?: "", r.body?.bytes() ?: ByteArray(0))
            if (r.code == 401 && retry && p.cloudPass.isNotEmpty() && path != "/api/login") {
                loginLocal(p); return local(p, method, path, body, ctype, false)
            }
            return res
        }
    }

    fun loginLocal(p: Prefs) {
        val b = JSONObject().put("name", p.cloudUser.ifEmpty { "admin" }).put("password", p.cloudPass).toString()
        http.newCall(Request.Builder().url(p.cloudBase + "/api/login").post(b.toRequestBody(JSON_T)).build()).execute().use { r ->
            val t = r.body?.string() ?: ""
            if (!r.isSuccessful) throw ApiError(errText(t, "Cloud-Anmeldung fehlgeschlagen"), r.code)
            p.cloudToken = JSONObject(t).optString("token")
        }
    }

    private fun portalReq(p: Prefs, url: String) = Request.Builder().url(url).header("X-Pagurus", "1").also {
        if (p.portalCookie.isNotEmpty()) it.header("Cookie", p.portalCookie)
    }

    fun loginPortal(p: Prefs) {
        val b = JSONObject().put("password", p.portalPass).toString()
        http.newCall(portalReq(p, p.portalApi + "?a=login").post(b.toRequestBody(JSON_T)).build()).execute().use { r ->
            val t = r.body?.string() ?: ""
            if (!r.isSuccessful) throw ApiError(errText(t, "Portal-Anmeldung fehlgeschlagen"), r.code)
            r.headers("Set-Cookie").firstOrNull { it.startsWith("pagurus=") }?.let { p.portalCookie = it.substringBefore(';') }
        }
    }

    private fun relay(p: Prefs, method: String, path: String, body: String?, ctype: String, retry: Boolean): Resp {
        val b = JSONObject().put("method", method).put("path", path).put("ctype", ctype)
        if (body != null) b.put("body", body)
        http.newCall(portalReq(p, p.portalApi + "?a=relay").post(b.toString().toRequestBody(JSON_T)).build()).execute().use { r ->
            val t = r.body?.string() ?: ""
            if (r.code == 401 && retry) { loginPortal(p); return relay(p, method, path, body, ctype, false) }
            if (!r.isSuccessful) throw ApiError(errText(t, "Portal: HTTP ${r.code}"), r.code)
            val o = JSONObject(t)
            return Resp(o.optInt("status", 502), o.optString("ctype"), Base64.decode(o.optString("body"), Base64.DEFAULT))
        }
    }

    /** Long-Polling auf neue Meldungen (fuer den Push-Dienst). */
    suspend fun alertsWait(ctx: Context, after: Long): JSONObject = withContext(Dispatchers.IO) {
        val p = Prefs(ctx)
        when (mode(ctx)) {
            Mode.LOCAL -> {
                val r = local(p, "GET", "/api/alerts/wait?after=$after&timeout=25", null, "application/json", true)
                if (r.code != 200) throw ApiError("HTTP ${r.code}")
                r.json()
            }
            Mode.PORTAL -> {
                fun get(): okhttp3.Response = http.newCall(portalReq(p, p.portalApi + "?a=alerts_wait&after=$after&timeout=25").build()).execute()
                var r = get()
                if (r.code == 401) { r.close(); loginPortal(p); r = get() }
                r.use { if (!it.isSuccessful) throw ApiError("Portal: HTTP ${it.code}"); JSONObject(it.body!!.string()) }
            }
            Mode.NONE -> throw ApiError("Keine Verbindung")
        }
    }

    fun errText(t: String, def: String) = try {
        JSONObject(t).let { it.optString("error").ifEmpty { it.optString("detail") }.ifEmpty { def } }
    } catch (e: Exception) { def }
}

// ---------------------------------------------------------------- Schnittstelle
object Api {
    private fun check(r: Conn.Resp): Conn.Resp {
        if (r.code !in 200..299) throw ApiError(Conn.errText(r.text, "Fehler ${r.code}"), r.code)
        return r
    }
    private fun enc(id: String) = Uri.encode(id)

    suspend fun devices(ctx: Context): List<Device> {
        val a = check(Conn.call(ctx, "GET", "/api/devices")).arr()
        return (0 until a.length()).map { Device(a.getJSONObject(it)) }
    }
    suspend fun scenes(ctx: Context): List<Scene> {
        val a = check(Conn.call(ctx, "GET", "/api/scenes")).arr()
        return (0 until a.length()).map { a.getJSONObject(it).let { o -> Scene(o.getInt("id"), o.getString("name")) } }
    }
    suspend fun switch(ctx: Context, id: String, body: JSONObject) {
        check(Conn.call(ctx, "POST", "/api/devices/${enc(id)}/switch", body.toString()))
    }
    suspend fun runScene(ctx: Context, id: Int): List<String> {
        val o = check(Conn.call(ctx, "POST", "/api/scenes/$id/run", "{}")).json()
        val e = o.optJSONArray("errors") ?: JSONArray()
        return (0 until e.length()).map { e.getString(it) }
    }
    suspend fun alerts(ctx: Context): List<Alert> {
        val a = check(Conn.call(ctx, "GET", "/api/alerts?limit=150")).arr()
        return (0 until a.length()).map { a.getJSONObject(it).let { o -> alert(o) } }
    }
    fun alert(o: JSONObject) = Alert(o.optLong("id"), o.optDouble("ts"), o.optString("kind"),
        o.optString("name").ifEmpty { o.optString("dev") }, o.optString("text"), o.optInt("ack") == 1)
    suspend fun ackAll(ctx: Context) { check(Conn.call(ctx, "POST", "/api/alerts/ack", "{}")) }
    suspend fun clearAlerts(ctx: Context) { check(Conn.call(ctx, "DELETE", "/api/alerts")) }
}
