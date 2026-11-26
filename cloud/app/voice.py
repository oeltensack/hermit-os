# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Sprachbefehle: deutscher Satz -> Aktion -> Antwortsatz.

Erkannt werden u. a.:
  „Licht Küche an“, „Küchenlicht aus“, „schalte die Stehlampe ein“, „alle Lichter im Wohnzimmer aus“
  „Deckenlicht auf 40 Prozent“, „Wohnzimmer heller“
  „Rollo Terrasse runter“, „alle Rollos hoch“, „Rollo Schlafzimmer auf 30 Prozent“, „Rollo stopp“
  „Klima auf 22 Grad“, „Klimaanlage Flur aus“, „heizen“, „Lüfter Stufe 2“
  „Szene Fernsehen“, „Fernsehen starten“
  „Wie warm ist es im Bad?“, „Wie viel Strom verbrauchen wir?“, „Ist die Stehlampe an?“, „Wie spät ist es?“
Geraete-, Raum- und Szenennamen kommen aus der Cloud; Schreibweisen wie „Kueche_Arbeitsplatte“ und
zusammengesetzte Woerter („Küchenlicht“) werden unscharf zugeordnet.
"""
import json
import re
import time
import urllib.request
from collections import deque
from difflib import SequenceMatcher

from core import now_local, q

VOICE_URL = None                # wird von hermit_cloud gesetzt
LOG = deque(maxlen=60)

ON = ("an", "ein", "einschalten", "anschalten", "anmachen", "aktivieren", "starten", "start")
OFF = ("aus", "ausschalten", "ausmachen", "abschalten", "deaktivieren", "stoppen")
TOGGLE = ("um", "umschalten")
UP = ("hoch", "auf", "oeffnen", "oeffne", "rauf", "aufmachen", "hochfahren")
DOWN = ("runter", "zu", "schliessen", "schliesse", "herunter", "zumachen", "runterfahren", "ab")
STOP = ("stopp", "stop", "halt", "anhalten", "stehen")
LIGHT_W = ("licht", "lichter", "lampe", "lampen", "leuchte", "leuchten", "beleuchtung", "deckenlicht", "spot", "spots")
COVER_W = ("rollo", "rollos", "rollladen", "rolladen", "rolllaeden", "rollaeden", "jalousie", "jalousien", "markise",
           "rollo's", "beschattung")
CLIMA_W = ("klima", "klimaanlage", "klimageraet", "klimaanlagen", "klimas")
MODES = {"heizen": "heat", "heizung": "heat", "waerme": "heat", "kuehlen": "cool", "kuehlung": "cool", "entfeuchten": "dry",
         "trocknen": "dry", "lueften": "fan", "ventilator": "fan", "automatik": "auto", "automatisch": "auto"}
FILL = {"bitte", "mal", "doch", "jetzt", "gleich", "schnell", "das", "den", "die", "der", "dem", "des", "im", "in", "am",
        "beim", "vom", "von", "zum", "zur", "mir", "mach", "mache", "machen", "schalte", "schalt", "schalten", "stelle",
        "stell", "setze", "setz", "fahre", "fahr", "kannst", "du", "hey", "pagurus", "und", "ist", "es", "auch", "noch",
        "einmal", "dann", "sofort", "gerade", "wieder", "alle", "alles", "ganze", "ganzen", "eine", "einen", "ein"}
NUMW = {"null": 0, "eins": 1, "einen": 1, "zwei": 2, "drei": 3, "vier": 4, "fuenf": 5, "sechs": 6, "sieben": 7, "acht": 8,
        "neun": 9, "zehn": 10, "elf": 11, "zwoelf": 12, "dreizehn": 13, "vierzehn": 14, "fuenfzehn": 15, "sechzehn": 16,
        "siebzehn": 17, "achtzehn": 18, "neunzehn": 19, "zwanzig": 20, "dreissig": 30, "vierzig": 40, "fuenfzig": 50,
        "sechzig": 60, "siebzig": 70, "achtzig": 80, "neunzig": 90, "hundert": 100}


# ======================= Text aufbereiten =======================
def norm(s):
    s = str(s or "").lower()
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss"), ("_", " "), ("-", " "), ("/", " ")):
        s = s.replace(a, b)
    s = re.sub(r"(\d),(\d)", r"\1.\2", s)
    s = re.sub(r"[^a-z0-9. %]", " ", s)
    s = re.sub(r"(?<!\d)\.|\.(?!\d)", " ", s)
    return " ".join(s.split())


def _word_num(w):
    if w in NUMW:
        return NUMW[w]
    m = re.fullmatch(r"(ein|zwei|drei|vier|fuenf|sechs|sieben|acht|neun)und(zwanzig|dreissig|vierzig|fuenfzig|"
                     r"sechzig|siebzig|achtzig|neunzig)", w)
    if m:
        return NUMW["eins" if m.group(1) == "ein" else m.group(1)] + NUMW[m.group(2)]
    return None


def numbers(toks):
    """Zahlen aus Ziffern und Zahlwoertern; „22 komma 5“, „21 einhalb“ -> 22.5 / 21.5"""
    out = []
    i = 0
    while i < len(toks):
        t = toks[i]
        v = None
        if re.fullmatch(r"\d+(\.\d+)?", t):
            v = float(t)
        elif t.endswith("%") and re.fullmatch(r"\d+%", t):
            v = float(t[:-1])
        else:
            v = _word_num(t)
        if v is not None:
            if i + 2 < len(toks) and toks[i + 1] == "komma" and (toks[i + 2].isdigit() or _word_num(toks[i + 2]) is not None):
                d = toks[i + 2] if toks[i + 2].isdigit() else str(_word_num(toks[i + 2]))
                v = float(f"{int(v)}.{d}")
                i += 2
            elif i + 1 < len(toks) and toks[i + 1] == "einhalb":
                v += 0.5
                i += 1
            out.append(v)
        i += 1
    return out


def _stem(w):
    for suf in ("en", "n", "e", "s"):
        if len(w) > 4 and w.endswith(suf):
            return w[: -len(suf)]
    return w


def tok_match(a, b):
    """Wie gut passt Wort a (Name) auf Wort b (Satz)? 0..1 – auch Wortteile („kueche“ in „kuechenlicht“)."""
    if a == b:
        return 1.0
    if len(a) >= 4 and (a in b or _stem(a) in b):
        return 0.9
    if len(b) >= 4 and b in a:
        return 0.8
    r = SequenceMatcher(None, a, b).ratio()
    return r if r >= 0.8 else 0.0


def name_score(name_toks, utt):
    if not name_toks:
        return 0.0
    hits = sum(max((tok_match(t, u) for u in utt), default=0) for t in name_toks)
    return hits / len(name_toks)


# ======================= Wissen aus der Cloud =======================
def _kind(d):
    import panel
    return panel.kind(d)


def entities(devices):
    devs = []
    for d in devices:
        if d.get("combined_into"):
            continue
        for x in [d] + (d.get("channels") or []):
            k = _kind(x)
            if k == "info":
                continue
            devs.append({"id": x["id"], "name": x.get("name") or x["id"], "room": x.get("room") or "", "k": k,
                         "toks": [t for t in norm(x.get("name") or x["id"]).split() if not t.isdigit() or len(t) > 1],
                         "d": x})
    rooms = sorted({d["room"] for d in devs if d["room"]}, key=len, reverse=True)
    scenes = [{"id": r["id"], "name": r["name"], "toks": norm(r["name"]).split()} for r in q("SELECT id, name FROM scenes")]
    return devs, rooms, scenes


def prompt_words(devices):
    """Namen fuer Whisper als Hinweis – verbessert die Erkennung eigener Bezeichnungen deutlich."""
    devs, rooms, scenes = entities(devices)
    names = list(dict.fromkeys([d["name"].replace("_", " ") for d in devs] + rooms + [s["name"] for s in scenes]))
    return "Licht, Rollo, Klima, Szene. " + ", ".join(names)[:500]


def find_room(utt, rooms):
    best, score = None, 0
    for r in rooms:
        s = name_score(norm(r).split(), utt)
        if s > score:
            best, score = r, s
    return best if score >= 0.85 else None


def find_devices(utt, devs, kinds=None, room=None):
    """Bestes Geraet nach Namen; liefert (geraet, punktzahl, zweitbester)."""
    cand = [d for d in devs if not kinds or d["k"] in kinds]
    scored = []
    for d in cand:
        s = name_score(d["toks"], utt)
        if room and d["room"] == room:
            s += 0.25
        scored.append((s, d))
    scored.sort(key=lambda x: -x[0])
    if not scored:
        return None, 0, 0
    return scored[0][1], scored[0][0], scored[1][0] if len(scored) > 1 else 0


def is_light(d):
    return d["k"] == "light" or any(t in LIGHT_W or any(w in t for w in ("licht", "lampe", "leucht")) for t in d["toks"])


# ======================= Ausfuehren =======================
def _bus():
    from bus import BUS
    return BUS


def _sw(d, on=None, **kw):
    if d["d"].get("locked"):
        raise RuntimeError(f"{d['name']} ist gesperrt")
    if not d["d"].get("online"):
        raise RuntimeError(f"{d['name']} ist offline")
    _bus().switch(d["id"], on, kw.get("brightness"), kw.get("cover"), kw.get("pos"))


def _climate(d, p):
    from bus import set_status
    if d["d"].get("locked"):
        raise RuntimeError(f"{d['name']} ist gesperrt")
    b = _bus()
    b.rpc(d["id"], "Climate.Set", {"id": 0, **p})
    set_status(d["id"], "climate:0", b.rpc(d["id"], "Climate.GetStatus", {"id": 0}), merge=True)


def _say_list(names):
    names = list(names)
    return names[0] if len(names) == 1 else ", ".join(names[:-1]) + " und " + names[-1]


def _num_de(v, dec=1):
    return (f"{v:.{dec}f}".rstrip("0").rstrip(".") if dec else f"{v:.0f}").replace(".", ",")


# ======================= Befehl verstehen =======================
def handle(text, devices, room=None, execute=True):
    """-> {"ok", "reply", "intent", "done": [...]}. room = Raum des Satelliten (Vorgabe, wenn keiner genannt)."""
    t0 = time.time()
    n = norm(text)
    utt = n.split()
    devs, rooms, scenes = entities(devices)
    res = {"ok": False, "reply": "", "intent": "", "done": [], "text": text}

    def out(ok, reply, intent, done=()):
        res.update(ok=ok, reply=reply, intent=intent, done=list(done))
        return res

    if not utt:
        return out(False, "Ich habe nichts verstanden.", "leer")
    said_room = find_room(utt, rooms)
    ctx_room = said_room or room
    nums = numbers(utt)
    has = lambda words: any(w in utt for w in words)                    # noqa: E731
    has_part = lambda parts: any(any(p in u for p in parts) for u in utt)  # noqa: E731

    # --- Uhrzeit
    if has(("spaet", "uhrzeit")) or n.startswith("wie viel uhr"):
        t = now_local()
        return out(True, f"Es ist {t.hour} Uhr {t.minute}." if t.minute else f"Es ist {t.hour} Uhr.", "zeit")

    # --- Abfragen: Temperatur
    if has(("warm", "kalt", "temperatur", "grad")) and (n.startswith("wie") or "welche" in utt or "wieviel" in utt):
        vals = []
        for d in devs:
            if ctx_room and d["room"] != ctx_room:
                continue
            s = d["d"].get("status") or {}
            tc = None
            if d["k"] == "sensor" and isinstance(s.get("temperature"), dict):
                tc = s["temperature"].get("tC")
            elif d["k"] == "climate":
                tc = s.get("ext_temperature_C", s.get("current_C"))
            if isinstance(tc, (int, float)):
                vals.append(tc)
        if ("draussen" in utt or "aussen" in utt or not vals) and not said_room:
            import panel
            o = panel._outdoor()
            if o is not None:
                return out(True, f"Draußen sind es {_num_de(o)} Grad.", "temperatur")
        if vals:
            v = sum(vals) / len(vals)
            return out(True, f"Im {ctx_room} sind es {_num_de(v)} Grad." if ctx_room else f"Es sind {_num_de(v)} Grad.",
                       "temperatur")
        return out(False, "Dafür habe ich keinen Temperaturfühler.", "temperatur")

    # --- Abfragen: Strom
    if has(("strom", "verbrauch", "verbrauchen", "leistung", "watt")) and (n.startswith("wie") or "was" in utt):
        w = sum(float((d["d"].get("status") or {}).get("apower") or 0) for d in devs
                if d["d"].get("online") and (not said_room or d["room"] == said_room))
        wh = sum(float(d["d"].get("today_wh") or 0) for d in devs if not said_room or d["room"] == said_room)
        p = f"{_num_de(w / 1000, 2)} Kilowatt" if w >= 1000 else f"{_num_de(w, 0)} Watt"
        return out(True, f"{'Im ' + said_room + ' ' if said_room else ''}Gerade {p}, heute bisher "
                   f"{_num_de(wh / 1000, 1)} Kilowattstunden.", "strom")

    # --- Abfragen: Zustand
    if utt[0] in ("ist", "sind", "laeuft", "brennt") and len(utt) > 1:
        d, s, _ = find_devices(utt[1:], devs, room=ctx_room)
        if d and s >= 0.5:
            st = d["d"].get("status") or {}
            if d["k"] == "cover":
                pos = st.get("current_pos")
                txt = {"open": "offen", "closed": "zu", "opening": "am Öffnen", "closing": "am Schließen"}.get(
                    st.get("state"), f"auf {pos} Prozent" if pos is not None else "unbekannt")
                return out(True, f"{d['name']} ist {txt}.", "zustand")
            if d["k"] == "sensor" and st.get("contact") is not None:
                return out(True, f"{d['name']} ist {'offen' if st['contact'] else 'zu'}.", "zustand")
            if "output" in st:
                return out(True, f"{d['name']} ist {'an' if st['output'] else 'aus'}.", "zustand")
        return out(False, "Das Gerät kenne ich nicht.", "zustand")

    # --- Szenen
    best, bs = None, 0
    for sc in scenes:
        s = name_score(sc["toks"], utt)
        if s > bs:
            best, bs = sc, s
    scene_kw = has(("szene", "scene", "modus"))
    if best and (bs >= 0.99 or (scene_kw and bs >= 0.6)):
        if execute:
            import engine
            errs = engine.run_scene(best["id"])
            if errs:
                return out(False, f"Szene {best['name']}: {'; '.join(errs)[:120]}", "szene")
        return out(True, f"Szene {best['name']}.", "szene", [f"scene:{best['id']}"])

    # --- Klima
    if has_part(CLIMA_W) or (has(tuple(MODES)) and not has_part(COVER_W)) or (has(("luefter",)) and has(("stufe", "auto"))):
        cl = [d for d in devs if d["k"] == "climate"]
        if not cl:
            return out(False, "Ich kenne keine Klimaanlage.", "klima")
        d, s, s2 = find_devices(utt, cl, room=ctx_room)
        if len(cl) > 1 and s < 0.5 and not (ctx_room and d["room"] == ctx_room):
            return out(False, f"Welche Klimaanlage? {_say_list([c['name'] for c in cl])}?", "klima")
        p, say = {}, []
        temps = [v for v in nums if 10 <= v <= 35]
        if temps:
            p["target_C"] = round(temps[0] * 2) / 2
            say.append(f"auf {_num_de(p['target_C'])} Grad")
        mode = next((MODES[w] for w in utt if w in MODES), None)
        if mode:
            p["mode"], p["on"] = mode, True
            say.append({"heat": "heizt", "cool": "kühlt", "dry": "entfeuchtet", "fan": "lüftet", "auto": "regelt automatisch"}[mode])
        if has(("luefter",)):
            st = d["d"].get("status") or {}
            fans = [f for f in (st.get("fans") or ["auto", "low", "low_mid", "mid", "mid_high", "high"]) if f != "auto"]
            lv = [int(v) for v in nums if 1 <= v <= len(fans)]
            if "auto" in utt or "automatisch" in utt:
                p["fan"] = "auto"
                say.append("Lüfter automatisch")
            elif lv:
                p["fan"] = fans[lv[0] - 1]
                say.append(f"Lüfter Stufe {lv[0]}")
        if has(OFF) and "mode" not in p:
            p = {"on": False}
            say = ["aus"]
        elif has(ON) and not p:
            p["on"] = True
            say.append("an")
        if not p:
            return out(False, f"Was soll {d['name']} tun?", "klima")
        if execute:
            try:
                _climate(d, p)
            except Exception as e:
                return out(False, str(e), "klima")
        return out(True, f"{d['name']} {' '.join(say)}.", "klima", [d["id"]])

    # --- Rollladen
    if has_part(COVER_W):
        cov = [d for d in devs if d["k"] == "cover"]
        if not cov:
            return out(False, "Ich kenne keine Rollläden.", "rollo")
        pct = [v for v in nums if 0 <= v <= 100]
        act = "stop" if has(STOP) else ("pos" if pct and has(("prozent", "%")) else
                                        "open" if has(UP) else "close" if has(DOWN) else None)
        if not act:
            return out(False, "Soll der Rollladen hoch oder runter?", "rollo")
        many = has(("alle", "rollos", "rolllaeden", "rollaeden", "jalousien")) or (said_room and not _specific(utt, cov))
        if many:
            targets = [d for d in cov if not ctx_room or d["room"] == ctx_room] if said_room or "alle" in utt else \
                [d for d in cov if not room or d["room"] == room] or cov
        else:
            d, s, s2 = find_devices(utt, cov, room=ctx_room)
            if s < 0.5 and len(cov) > 1:
                if room:
                    targets = [c for c in cov if c["room"] == room]
                else:
                    return out(False, "Welcher Rollladen?", "rollo")
            else:
                targets = [d]
        targets = [d for d in targets if d]
        errs, done = [], []
        for d in targets:
            try:
                if execute:
                    if act == "pos":
                        _sw(d, pos=int(pct[0]))
                    else:
                        _sw(d, cover=act)
                done.append(d["id"])
            except Exception as e:
                errs.append(str(e))
        what = {"open": "fährt hoch", "close": "fährt runter", "stop": "hält an",
                "pos": f"fährt auf {int(pct[0]) if pct else 0} Prozent"}[act]
        subj = targets[0]["name"] if len(targets) == 1 else (f"Die Rollläden im {ctx_room}" if ctx_room else "Alle Rollläden")
        what = what if len(targets) == 1 else what.replace("fährt", "fahren").replace("hält", "halten")
        return out(bool(done), f"{subj} {what}." + (f" {'; '.join(errs)}" if errs else ""), "rollo", done)

    # --- Licht / Schalter / Dimmen
    want = "toggle" if has(TOGGLE) else ("on" if has(ON) else "off" if has(OFF) else None)
    pct = [v for v in nums if 0 <= v <= 100]
    dim = None
    if pct and has(("prozent", "%", "helligkeit")):
        dim = int(pct[0])
    elif has(("heller", "dunkler")):
        dim = "+" if "heller" in utt else "-"
    if not want and dim is None:
        return out(False, f"Das habe ich nicht verstanden: „{text.strip()}“", "unbekannt")
    sw = [d for d in devs if d["k"] in ("switch", "light")]
    light_kw = has_part(("licht", "lampe", "leucht", "beleuchtung"))
    group = has(("alle", "alles", "lichter", "lampen")) or (said_room and not _specific(utt, sw))
    if group:
        targets = [d for d in sw if (not ctx_room or d["room"] == ctx_room) and is_light(d)]
        if not targets and ctx_room:
            targets = [d for d in sw if d["room"] == ctx_room and light_kw is False]
        if not targets:
            return out(False, f"Im {ctx_room} kenne ich kein Licht." if ctx_room else "Ich kenne keine Lichter.", "licht")
    else:
        d, s, s2 = find_devices(utt, sw, room=ctx_room)
        if not d or s < 0.5:
            return out(False, f"Das Gerät habe ich nicht gefunden: „{text.strip()}“", "schalten")
        if s - s2 < 0.1 and s < 1.2:
            alts = [x for x in sw if abs(name_score(x["toks"], utt) + (0.25 if ctx_room and x["room"] == ctx_room else 0) - s) < 0.1]
            if len(alts) > 1 and not ctx_room:
                return out(False, f"Meinst du {_say_list([a['name'] for a in alts[:3]])}?", "schalten")
        targets = [d]
    errs, done = [], []
    for d in targets:
        try:
            if dim is not None and "brightness" in (d["d"].get("status") or {}):
                cur = int((d["d"].get("status") or {}).get("brightness") or 50)
                b = dim if isinstance(dim, int) else max(5, min(100, cur + (25 if dim == "+" else -25)))
                if execute:
                    _sw(d, brightness=b)
            elif dim is not None and want is None:
                raise RuntimeError(f"{d['name']} lässt sich nicht dimmen")
            elif execute:
                _sw(d, None if want == "toggle" else want == "on")
            done.append(d["id"])
        except Exception as e:
            errs.append(str(e))
    if dim is not None and done:
        what = f"auf {dim} Prozent" if isinstance(dim, int) else ("heller" if dim == "+" else "dunkler")
    else:
        what = {"on": "an", "off": "aus", "toggle": "umgeschaltet"}.get(want, "")
    if len(targets) == 1:
        reply = f"{targets[0]['name']} {what}." if done else (errs[0] if errs else "Hat nicht geklappt.")
    else:
        reply = f"{'Licht im ' + ctx_room if ctx_room else 'Alle Lichter'} {what}." + \
            (f" Nicht erreichbar: {len(errs)}." if errs else "")
    res["ms"] = int((time.time() - t0) * 1000)
    return out(bool(done), reply.replace("_", " "), "schalten", done)


def _specific(utt, devs):
    """Nennt der Satz ein bestimmtes Geraet (nicht nur Raum + „Licht“)?"""
    generic = set(LIGHT_W) | set(COVER_W) | FILL
    rest = [u for u in utt if u not in generic and u not in ON + OFF + UP + DOWN + STOP]
    for d in devs:
        toks = [t for t in d["toks"] if t not in generic]
        if toks and name_score(toks, rest) >= 0.9 and len(toks) >= 1:
            room_toks = norm(d["room"]).split()
            if not all(t in room_toks for t in toks):
                return True
    return False


# ======================= Sprachdienst =======================
def _post(path, data, ctype, timeout=30):
    req = urllib.request.Request(VOICE_URL.rstrip("/") + path, data=data, method="POST", headers={"Content-Type": ctype})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read(), r.headers.get("Content-Type", "")


def stt(audio, rate, prompt):
    import urllib.parse
    body, _ = _post(f"/stt?rate={int(rate)}&prompt={urllib.parse.quote(prompt)}", audio,
                    "audio/wav" if audio[:4] == b"RIFF" else "application/octet-stream", 60)
    return json.loads(body)


def tts(text, rate=22050):
    body, _ = _post("/tts", json.dumps({"text": text, "rate": int(rate)}).encode(), "application/json", 30)
    return body


def health():
    try:
        with urllib.request.urlopen(VOICE_URL.rstrip("/") + "/health", timeout=3) as r:
            return json.loads(r.read())
    except Exception as e:
        return {"ok": False, "err": str(e)[:160], "url": VOICE_URL}


def log(src, text, res, ms=None):
    LOG.appendleft({"ts": time.time(), "src": src, "text": text, "reply": res.get("reply"), "ok": res.get("ok"),
                    "intent": res.get("intent"), "ms": ms})
