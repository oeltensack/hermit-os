// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 D.Koch – Pagurus OS
package de.pagurus.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

val BG = Color(0xFF0F1419); val CARD = Color(0xFF1A2129); val MUTED = Color(0xFF8A96A3)
val ON = Color(0xFF3FB950); val ACCENT = Color(0xFF2F9BE0); val ERR = Color(0xFFF85149); val WARN = Color(0xFFD29922)

class MainActivity : ComponentActivity() {
    private var startTab by mutableIntStateOf(0)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        startTab = intent.getIntExtra("tab", if (Prefs(this).cloudUrl.isEmpty() && Prefs(this).portalUrl.isEmpty()) 3 else 0)
        PushService.start(this)
        setContent {
            MaterialTheme(colorScheme = darkColorScheme(primary = ACCENT, background = BG, surface = CARD,
                surfaceVariant = CARD, onSurface = Color(0xFFE6E9EC))) {
                App(startTab) { startTab = it }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.hasExtra("tab")) startTab = intent.getIntExtra("tab", 0)
    }

    override fun onResume() { super.onResume(); Conn.invalidate() }
}

@Composable
fun App(tab: Int, setTab: (Int) -> Unit) {
    val tabs = listOf("Geräte" to Icons.Filled.Home, "Meldungen" to Icons.Filled.Notifications,
        "Cloud" to Icons.Filled.List, "Einstellungen" to Icons.Filled.Settings)
    Scaffold(containerColor = BG, bottomBar = {
        NavigationBar(containerColor = CARD) {
            tabs.forEachIndexed { i, (t, ic) ->
                NavigationBarItem(selected = tab == i, onClick = { setTab(i) }, icon = { Icon(ic, t) }, label = { Text(t) })
            }
        }
    }) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            when (tab) {
                0 -> DevicesScreen()
                1 -> AlertsScreen()
                2 -> CloudWeb()
                else -> SettingsScreen()
            }
        }
    }
}

@Composable
fun ModeLine() {
    val ctx = LocalContext.current
    var m by remember { mutableStateOf(Conn.mode) }
    LaunchedEffect(Unit) { while (true) { m = Conn.mode(ctx); delay(5000) } }
    Text(when (m) { Conn.Mode.LOCAL -> "Zuhause · direkt"; Conn.Mode.PORTAL -> "Unterwegs · über Portal"; else -> "keine Verbindung" },
        color = if (m == Conn.Mode.NONE) ERR else MUTED, fontSize = 12.sp)
}

fun fmt(v: Double?, d: Int, u: String) = if (v == null) "–" else String.format(Locale.GERMANY, "%.${d}f %s", v, u)

fun ago(ts: Double): String {
    if (ts <= 0) return "nie"
    val s = System.currentTimeMillis() / 1000.0 - ts
    return when { s < 90 -> "gerade eben"; s < 3600 -> "vor ${(s / 60).toInt()} min"; s < 86400 -> "vor ${(s / 3600).toInt()} h"; else -> "vor ${(s / 86400).toInt()} Tg" }
}

// ---------------------------------------------------------------- Geraete
@Composable
fun DevicesScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var devs by remember { mutableStateOf<List<Device>?>(null) }
    var scenes by remember { mutableStateOf<List<Scene>>(emptyList()) }
    var err by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(setOf<String>()) }

    suspend fun load() {
        try {
            devs = Api.devices(ctx); err = null
            scenes = Api.scenes(ctx)
            val o = JSONObject(); devs!!.forEach { if (it.hasOutput) o.put(it.id, it.on) }
            Prefs(ctx).states = o
        } catch (e: Exception) { err = e.message }
    }
    fun act(key: String, f: suspend () -> Unit) = scope.launch {
        busy = busy + key
        try { f() } catch (e: Exception) { Toast.makeText(ctx, e.message, Toast.LENGTH_LONG).show() }
        delay(500); load(); busy = busy - key
        Actions.updateAll(ctx)
    }
    LaunchedEffect(Unit) { while (true) { load(); delay(3000) } }

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item { Row(Modifier.padding(top = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("Pagurus", fontSize = 22.sp, fontWeight = FontWeight.Bold); Spacer(Modifier.width(12.dp)); ModeLine()
        } }
        err?.let { item { Text(it, color = ERR) } }
        val d = devs
        if (d != null) {
            val pw = d.sumOf { it.apower ?: 0.0 }; val wh = d.sumOf { it.todayWh ?: 0.0 }
            item { Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                Value(fmt(pw, 0, "W"), "Leistung jetzt"); Value(fmt(wh / 1000, 2, "kWh"), "heute")
            } }
        }
        if (scenes.isNotEmpty()) item {
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(scenes) { s ->
                    FilledTonalButton(onClick = { act("scene${s.id}") {
                        val e = Api.runScene(ctx, s.id)
                        Toast.makeText(ctx, if (e.isEmpty()) "${s.name} ausgeführt" else e.joinToString("; "), Toast.LENGTH_SHORT).show()
                    } }, enabled = "scene${s.id}" !in busy) { Text(s.name) }
                }
            }
        }
        if (d == null && err == null) item { CircularProgressIndicator() }
        d?.groupBy { it.room }?.toSortedMap(compareBy<String> { it.isEmpty() }.thenBy { it.lowercase() })?.forEach { (room, list) ->
            item { Text(if (room.isEmpty()) "Ohne Raum" else room, fontWeight = FontWeight.SemiBold, fontSize = 16.sp,
                modifier = Modifier.padding(top = 8.dp)) }
            items(list, key = { it.id }) { dv -> DeviceCard(dv, dv.id in busy) { key, f -> act(key, f) } }
        }
        item { Spacer(Modifier.height(20.dp)) }
    }
}

@Composable
fun Value(v: String, l: String, color: Color = Color.Unspecified) {
    Column { Text(v, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, color = color); Text(l, fontSize = 11.sp, color = MUTED) }
}

@Composable
fun DeviceCard(d: Device, busy: Boolean, act: (String, suspend () -> Unit) -> Unit) {
    val ctx = LocalContext.current
    Card(colors = CardDefaults.cardColors(containerColor = CARD), shape = RoundedCornerShape(10.dp),
        modifier = Modifier.fillMaxWidth().then(if (!d.online && !d.isSensor) Modifier.background(BG) else Modifier)) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(d.name, fontWeight = FontWeight.SemiBold, fontSize = 16.sp, color = if (d.online || d.isSensor) Color.Unspecified else MUTED)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(8.dp).clip(RoundedCornerShape(4.dp)).background(if (d.online) ON else MUTED))
                        Spacer(Modifier.width(6.dp))
                        Text((if (d.isSensor) "Fühler · ${ago(d.lastSeen)}" else if (d.online) "online" else "offline") +
                            (if (d.model.isNotEmpty()) " · ${d.model}" else ""), fontSize = 12.sp, color = MUTED)
                    }
                }
                if (d.hasOutput) {
                    if (d.meterOnly) Text("Messung", color = MUTED, fontSize = 12.sp)
                    else if (busy) CircularProgressIndicator(Modifier.size(28.dp))
                    else Switch(checked = d.on, enabled = d.switchable, onCheckedChange = { v ->
                        act(d.id) { Api.switch(ctx, d.id, JSONObject().put("on", v)) }
                    }, colors = SwitchDefaults.colors(checkedTrackColor = ON))
                }
            }
            val vals = buildList<Pair<String, String>> {
                d.apower?.let { add(fmt(it, 1, "W") to "Leistung") }
                if (!d.isSensor) d.todayWh?.let { if (d.apower != null) add(fmt(it, 0, "Wh") to "heute") }
                d.tempC?.let { add(fmt(it, 1, "°C") to "Temperatur") }
                d.humidity?.let { add(fmt(it, 0, "%") to "Feuchte") }
                d.num("lux")?.let { add(fmt(it, 0, "lx") to "Helligkeit") }
                d.num("battery")?.let { add(fmt(it, 0, "%") to "Batterie") }
                if (d.st.has("contact")) add((if (d.st.optBoolean("contact")) "offen" else "zu") to "Kontakt")
                if (d.st.has("motion")) add((if (d.st.optBoolean("motion")) "Bewegung" else "ruhig") to "Bewegung")
                d.st.optString("button").takeIf { it.isNotEmpty() }?.let { add(it to "Taste") }
            }
            if (vals.isNotEmpty()) Row(horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                vals.take(4).forEach { (v, l) -> Value(v, l, if ((l == "Kontakt" && v == "offen") || (l == "Bewegung" && v == "Bewegung")) WARN else Color.Unspecified) }
            }
            if (d.isCover) {
                val stTxt = mapOf("open" to "offen", "closed" to "geschlossen", "opening" to "öffnet", "closing" to "schließt", "stopped" to "gestoppt")[d.coverState] ?: d.coverState
                Text(stTxt + (d.pos?.let { " · $it %" } ?: ""), fontWeight = FontWeight.SemiBold)
                if (d.online) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf("open" to "▲ Auf", "stop" to "■ Stopp", "close" to "▼ Zu").forEach { (a, l) ->
                            FilledTonalButton(onClick = { act(d.id) { Api.switch(ctx, d.id, JSONObject().put("cover", a)) } }, enabled = !busy) { Text(l) }
                        }
                    }
                    if (d.posControl) {
                        var pos by remember(d.pos) { mutableFloatStateOf((d.pos ?: 0).toFloat()) }
                        Slider(value = pos, onValueChange = { pos = it }, valueRange = 0f..100f, onValueChangeFinished = {
                            act(d.id) { Api.switch(ctx, d.id, JSONObject().put("pos", pos.toInt())) }
                        })
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- Meldungen
@Composable
fun AlertsScreen() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    var list by remember { mutableStateOf<List<Alert>?>(null) }
    var err by remember { mutableStateOf<String?>(null) }
    suspend fun load() { try { list = Api.alerts(ctx); err = null } catch (e: Exception) { err = e.message } }
    LaunchedEffect(Unit) { while (true) { load(); delay(10_000) } }
    val df = remember { SimpleDateFormat("dd.MM. HH:mm", Locale.GERMANY) }
    Column(Modifier.fillMaxSize().padding(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Meldungen", fontSize = 22.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            TextButton(onClick = { scope.launch { try { Api.ackAll(ctx); load() } catch (e: Exception) { err = e.message } } }) { Text("Alle quittieren") }
            TextButton(onClick = { scope.launch { try { Api.clearAlerts(ctx); load() } catch (e: Exception) { err = e.message } } }) { Text("Löschen", color = ERR) }
        }
        err?.let { Text(it, color = ERR) }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(list ?: emptyList(), key = { it.id }) { a ->
                Card(colors = CardDefaults.cardColors(containerColor = CARD)) {
                    Column(Modifier.padding(12.dp)) {
                        Row {
                            Text(KIND[a.kind] ?: a.kind, fontSize = 12.sp, color = if (a.kind in QUIET || a.kind == "finished") ON else if (a.ack) MUTED else ERR)
                            Spacer(Modifier.weight(1f))
                            Text(df.format(Date((a.ts * 1000).toLong())), fontSize = 12.sp, color = MUTED)
                        }
                        Text(a.name, fontWeight = FontWeight.SemiBold, color = if (a.ack) MUTED else Color.Unspecified)
                        Text(a.text, color = if (a.ack) MUTED else Color.Unspecified)
                    }
                }
            }
            if (list?.isEmpty() == true) item { Text("Keine Meldungen", color = MUTED, modifier = Modifier.padding(20.dp)) }
        }
    }
}

// ---------------------------------------------------------------- Einstellungen
@Composable
fun SettingsScreen() {
    val ctx = LocalContext.current
    val p = remember { Prefs(ctx) }
    val scope = rememberCoroutineScope()
    var cloud by remember { mutableStateOf(p.cloudUrl) }
    var user by remember { mutableStateOf(p.cloudUser) }
    var pass by remember { mutableStateOf(p.cloudPass) }
    var portal by remember { mutableStateOf(p.portalUrl) }
    var ppass by remember { mutableStateOf(p.portalPass) }
    var push by remember { mutableStateOf(p.push) }
    var status by remember { mutableStateOf("") }
    var favs by remember { mutableStateOf(p.favs) }
    var choices by remember { mutableStateOf<List<Fav>>(emptyList()) }
    var tiles by remember { mutableStateOf((1..3).map { p.tile(it) }) }

    val permLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestPermission()) { }

    suspend fun loadChoices() = try {
        choices = Api.scenes(ctx).map { Fav("scene", it.id.toString(), it.name) } +
            Api.devices(ctx).filter { it.hasOutput && !it.meterOnly }.map { Fav("dev", it.id, it.name) }
    } catch (e: Exception) { }
    LaunchedEffect(Unit) { loadChoices() }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Einstellungen", fontSize = 22.sp, fontWeight = FontWeight.Bold)
        Section("Cloud zu Hause")
        OutlinedTextField(cloud, { cloud = it }, label = { Text("Adresse, z. B. 192.168.0.50:8080") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(user, { user = it }, label = { Text("Benutzer") }, singleLine = true, modifier = Modifier.weight(1f))
            OutlinedTextField(pass, { pass = it }, label = { Text("Passwort") }, singleLine = true, modifier = Modifier.weight(1f),
                visualTransformation = PasswordVisualTransformation())
        }
        Section("Portal für unterwegs")
        OutlinedTextField(portal, { portal = it }, label = { Text("z. B. https://deine-domain.de/pagurus/") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri))
        OutlinedTextField(ppass, { ppass = it }, label = { Text("Portal-Passwort") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            visualTransformation = PasswordVisualTransformation())
        Button(onClick = {
            p.cloudUrl = cloud; p.cloudUser = user; p.cloudPass = pass; p.portalUrl = portal; p.portalPass = ppass
            p.cloudToken = ""; p.portalCookie = ""; Conn.invalidate()
            status = "Prüfe …"
            scope.launch {
                status = try {
                    val m = Conn.mode(ctx)
                    when (m) {
                        Conn.Mode.LOCAL -> { kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { Conn.loginLocal(p) }; "Zuhause: Cloud erreichbar, angemeldet" }
                        Conn.Mode.PORTAL -> { Api.devices(ctx); "Unterwegs: Portal erreichbar, Cloud antwortet" }
                        else -> "Weder Cloud noch Portal erreichbar"
                    }
                } catch (e: Exception) { "Fehler: ${e.message}" }
                loadChoices()
            }
        }) { Text("Speichern und prüfen") }
        if (status.isNotEmpty()) Text(status, color = if (status.startsWith("Fehler") || status.startsWith("Weder")) ERR else ON)

        Section("Push-Meldungen")
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Meldungen als Benachrichtigung", modifier = Modifier.weight(1f))
            Switch(push, { v ->
                push = v; p.push = v
                if (v) {
                    if (Build.VERSION.SDK_INT >= 33 && ctx.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
                        permLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                    PushService.start(ctx)
                } else PushService.stop(ctx)
            })
        }
        Text("Läuft im Hintergrund mit einer stillen Daueranzeige – ohne Google-Dienste. Damit Android den Dienst nicht beendet, die Akku-Optimierung für Pagurus ausschalten.",
            fontSize = 12.sp, color = MUTED)
        TextButton(onClick = { ctx.startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)) }) { Text("Akku-Optimierung öffnen") }

        Section("Favoriten für das Widget (max. 6)")
        if (choices.isEmpty()) Text("Erst Verbindung herstellen.", color = MUTED, fontSize = 12.sp)
        choices.forEach { c ->
            val sel = favs.any { it.key == c.key }
            Row(Modifier.fillMaxWidth().clickable {
                favs = if (sel) favs.filter { it.key != c.key } else if (favs.size < 6) favs + c else favs
                p.favs = favs; FavWidget.render(ctx)
            }, verticalAlignment = Alignment.CenterVertically) {
                Checkbox(sel, null); Text((if (c.type == "scene") "Szene: " else "") + c.name)
            }
        }
        Section("Schnellkacheln")
        Text("In den Schnelleinstellungen (zweimal von oben wischen → Stift) „Pagurus 1–3“ hinzufügen.", fontSize = 12.sp, color = MUTED)
        (1..3).forEach { n ->
            var open by remember { mutableStateOf(false) }
            Box {
                OutlinedButton(onClick = { open = true }, modifier = Modifier.fillMaxWidth()) {
                    Text("Kachel $n: " + (tiles[n - 1]?.name ?: "– nicht belegt –"))
                }
                DropdownMenu(open, { open = false }) {
                    DropdownMenuItem(text = { Text("– nicht belegt –") }, onClick = {
                        p.setTile(n, null); tiles = (1..3).map { p.tile(it) }; open = false; Actions.updateAll(ctx)
                    })
                    choices.forEach { c ->
                        DropdownMenuItem(text = { Text((if (c.type == "scene") "Szene: " else "") + c.name) }, onClick = {
                            p.setTile(n, c); tiles = (1..3).map { p.tile(it) }; open = false; Actions.updateAll(ctx)
                        })
                    }
                }
            }
        }
        Spacer(Modifier.height(10.dp))
        Text("Pagurus App ${BuildConfigCompat.version(ctx)}", fontSize = 11.sp, color = MUTED)
    }
}

@Composable
fun Section(t: String) = Text(t.uppercase(), fontSize = 12.sp, color = MUTED, fontWeight = FontWeight.SemiBold,
    modifier = Modifier.padding(top = 10.dp))

object BuildConfigCompat {
    fun version(ctx: android.content.Context): String = try {
        ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName ?: ""
    } catch (e: Exception) { "" }
}
