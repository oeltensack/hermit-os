// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
package de.pagurus.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.net.ConnectivityManager
import android.net.Network
import android.os.Build
import android.os.IBinder
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.widget.RemoteViews
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject

// ---------------------------------------------------------------- Favoriten ausfuehren
object Actions {
    /** Szene ausfuehren bzw. Geraet umschalten. Liefert Fehlertext oder null. */
    suspend fun run(ctx: Context, f: Fav): String? = try {
        if (f.type == "scene") {
            Api.runScene(ctx, f.id.toInt()).takeIf { it.isNotEmpty() }?.joinToString("; ")
        } else {
            val on = Prefs(ctx).states.optBoolean(f.id, false)
            Api.switch(ctx, f.id, JSONObject().put("on", !on))
            null
        }
    } catch (e: Exception) { e.message ?: "Fehler" }

    /** Zustaende aller Geraete holen und merken (fuer Widget und Kacheln). */
    suspend fun refreshStates(ctx: Context): JSONObject? = try {
        val o = JSONObject()
        Api.devices(ctx).forEach { d -> if (d.hasOutput) o.put(d.id, d.on) }
        Prefs(ctx).states = o
        o
    } catch (e: Exception) { null }

    fun updateAll(ctx: Context) {
        FavWidget.render(ctx)
        for (c in listOf(Tile1::class.java, Tile2::class.java, Tile3::class.java))
            TileService.requestListeningState(ctx, ComponentName(ctx, c))
    }
}

// ---------------------------------------------------------------- Push
/** Haelt eine Verbindung zur Cloud (zu Hause) bzw. zum Portal (unterwegs) und meldet neue Meldungen. */
class PushService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var netCb: ConnectivityManager.NetworkCallback? = null

    override fun onBind(i: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        channels(this)
        val n = Notification.Builder(this, CH_SVC).setSmallIcon(R.drawable.ic_notify)
            .setContentTitle("Pagurus").setContentText("Wartet auf Meldungen")
            .setContentIntent(openApp(this)).setOngoing(true).build()
        if (Build.VERSION.SDK_INT >= 34) startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        else startForeground(1, n)
        if (job?.isActive != true) job = scope.launch { loop() }
        if (netCb == null) {
            val cm = getSystemService(ConnectivityManager::class.java)
            netCb = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) { Conn.invalidate() }
                override fun onLost(network: Network) { Conn.invalidate() }
            }
            cm.registerDefaultNetworkCallback(netCb!!)
        }
        return START_STICKY
    }

    private suspend fun loop() {
        val p = Prefs(this)
        while (scope.isActive) {
            try {
                val r = Conn.alertsWait(this, maxOf(p.lastAlert, 0))
                val last = r.optLong("last")
                if (p.lastAlert < 0) { p.lastAlert = last; continue }      // beim ersten Start nichts Altes melden
                if (last < p.lastAlert) p.lastAlert = last                    // Meldungen wurden geloescht
                val a = r.optJSONArray("alerts")
                if (a != null) for (i in 0 until a.length()) {
                    val al = Api.alert(a.getJSONObject(i))
                    if (al.id <= p.lastAlert) continue
                    p.lastAlert = al.id
                    if (al.kind !in QUIET && !al.ack) notify(this, al)
                }
            } catch (e: Exception) {
                Conn.invalidate()
                delay(20_000)
            }
        }
    }

    override fun onDestroy() {
        scope.coroutineContext[Job]?.cancel()
        netCb?.let { getSystemService(ConnectivityManager::class.java).unregisterNetworkCallback(it) }
        super.onDestroy()
    }

    companion object {
        const val CH_SVC = "svc"
        const val CH_ALERT = "alerts"
        fun channels(ctx: Context) {
            val nm = ctx.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(CH_SVC, "Verbindung", NotificationManager.IMPORTANCE_MIN)
                .apply { description = "Dauerhafte Anzeige, solange Meldungen empfangen werden" })
            nm.createNotificationChannel(NotificationChannel(CH_ALERT, "Meldungen", NotificationManager.IMPORTANCE_HIGH)
                .apply { description = "Überlast, Fertig, Offline, Regeln …" })
        }
        fun openApp(ctx: Context, tab: Int = 0): PendingIntent = PendingIntent.getActivity(ctx, tab,
            Intent(ctx, MainActivity::class.java).putExtra("tab", tab).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        fun notify(ctx: Context, a: Alert) {
            val nm = ctx.getSystemService(NotificationManager::class.java)
            val n = Notification.Builder(ctx, CH_ALERT).setSmallIcon(R.drawable.ic_notify)
                .setContentTitle("${KIND[a.kind] ?: a.kind} · ${a.name}").setContentText(a.text)
                .setStyle(Notification.BigTextStyle().bigText(a.text))
                .setWhen((a.ts * 1000).toLong()).setShowWhen(true)
                .setContentIntent(openApp(ctx, 1)).setAutoCancel(true).build()
            nm.notify((a.id % 100000).toInt() + 10, n)
        }
        fun start(ctx: Context) {
            if (!Prefs(ctx).push) return
            ctx.startForegroundService(Intent(ctx, PushService::class.java))
        }
        fun stop(ctx: Context) = ctx.stopService(Intent(ctx, PushService::class.java))
    }
}

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, i: Intent) {
        if (i.action != Intent.ACTION_BOOT_COMPLETED && i.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        try { PushService.start(ctx) } catch (e: Exception) { }
    }
}

// ---------------------------------------------------------------- Widget
class FavWidget : AppWidgetProvider() {
    override fun onUpdate(ctx: Context, m: AppWidgetManager, ids: IntArray) {
        render(ctx)
        val pr = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try { Actions.refreshStates(ctx); render(ctx) } finally { pr.finish() }
        }
    }

    override fun onReceive(ctx: Context, i: Intent) {
        super.onReceive(ctx, i)
        if (i.action != ACTION) return
        val f = Prefs(ctx).favs.getOrNull(i.getIntExtra("i", -1)) ?: return
        val pr = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                if (f.type == "dev") Actions.refreshStates(ctx)
                val err = Actions.run(ctx, f)
                if (err != null) android.os.Handler(ctx.mainLooper).post {
                    android.widget.Toast.makeText(ctx, err, android.widget.Toast.LENGTH_LONG).show()
                }
                delay(700)
                Actions.refreshStates(ctx)
                Actions.updateAll(ctx)
            } finally { pr.finish() }
        }
    }

    companion object {
        const val ACTION = "de.pagurus.app.FAV"
        private val SLOTS = intArrayOf(R.id.f0, R.id.f1, R.id.f2, R.id.f3, R.id.f4, R.id.f5)
        fun render(ctx: Context) {
            val m = AppWidgetManager.getInstance(ctx)
            val ids = m.getAppWidgetIds(ComponentName(ctx, FavWidget::class.java))
            if (ids.isEmpty()) return
            val p = Prefs(ctx)
            val favs = p.favs
            val st = p.states
            val v = RemoteViews(ctx.packageName, R.layout.fav_widget)
            SLOTS.forEachIndexed { i, slot ->
                val f = favs.getOrNull(i)
                if (f == null) {
                    v.setViewVisibility(slot, if (i == 0 && favs.isEmpty()) android.view.View.VISIBLE else android.view.View.INVISIBLE)
                    if (i == 0 && favs.isEmpty()) {
                        v.setTextViewText(slot, "Favoriten in der App wählen")
                        v.setOnClickPendingIntent(slot, PushService.openApp(ctx, 3))
                    }
                    return@forEachIndexed
                }
                v.setViewVisibility(slot, android.view.View.VISIBLE)
                val on = f.type == "dev" && st.optBoolean(f.id, false)
                v.setTextViewText(slot, f.name)
                v.setInt(slot, "setBackgroundResource", if (on) R.drawable.btn_on else R.drawable.btn_off)
                val pi = PendingIntent.getBroadcast(ctx, 100 + i,
                    Intent(ctx, FavWidget::class.java).setAction(ACTION).putExtra("i", i),
                    PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
                v.setOnClickPendingIntent(slot, pi)
            }
            m.updateAppWidget(ids, v)
        }
    }
}

// ---------------------------------------------------------------- Schnellkacheln
abstract class BaseTile(private val n: Int) : TileService() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onStartListening() {
        show()
        scope.launch { if (Prefs(this@BaseTile).tile(n)?.type == "dev") { Actions.refreshStates(this@BaseTile); show() } }
    }

    private fun show() {
        val t = qsTile ?: return
        val f = Prefs(this).tile(n)
        t.icon = Icon.createWithResource(this, R.drawable.ic_tile)
        if (f == null) {
            t.label = "Pagurus $n"; t.subtitleCompat("in der App festlegen"); t.state = Tile.STATE_UNAVAILABLE
        } else {
            t.label = f.name
            if (f.type == "scene") { t.state = Tile.STATE_INACTIVE; t.subtitleCompat("Szene") }
            else {
                val on = Prefs(this).states.optBoolean(f.id, false)
                t.state = if (on) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
                t.subtitleCompat(if (on) "an" else "aus")
            }
        }
        t.updateTile()
    }

    private fun Tile.subtitleCompat(s: String) { if (Build.VERSION.SDK_INT >= 29) subtitle = s }

    override fun onClick() {
        val f = Prefs(this).tile(n) ?: return
        qsTile?.let { it.state = Tile.STATE_ACTIVE; it.updateTile() }
        scope.launch {
            if (f.type == "dev") Actions.refreshStates(this@BaseTile)
            Actions.run(this@BaseTile, f)
            delay(700)
            Actions.refreshStates(this@BaseTile)
            show()
            FavWidget.render(this@BaseTile)
        }
    }

    override fun onDestroy() { scope.coroutineContext[Job]?.cancel(); super.onDestroy() }
}

class Tile1 : BaseTile(1)
class Tile2 : BaseTile(2)
class Tile3 : BaseTile(3)
