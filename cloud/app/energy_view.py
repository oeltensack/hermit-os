# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Verbrauch je Zeitraum (Tag/Woche/Monat/Jahr) mit Vergleich zum Zeitraum davor."""
import calendar
from datetime import date, datetime, timedelta

from core import TZ, now_local, price_eur_kwh, q

MONTHS = ["Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"]
MONTHS_L = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober",
            "November", "Dezember"]
DAYS = ["Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"]
DAYS_L = ["Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"]


def _devsql(dev, col="dev"):
    return (f" AND {col}=?", [dev]) if dev else ("", [])


def _days(dev, d0, d1):
    """Tageswerte [d0, d1] als dict 'YYYY-MM-DD' -> Wh."""
    w, a = _devsql(dev)
    rows = q(f"SELECT day, SUM(wh) AS wh FROM energy WHERE day>=? AND day<=?{w} GROUP BY day",
             [d0.isoformat(), d1.isoformat()] + a)
    return {r["day"]: r["wh"] for r in rows}


def _hours(dev, d):
    """Stundenwerte eines Tages (0..23). Aus energy_h; fehlt der Tag dort, aus dem Leistungsverlauf."""
    w, a = _devsql(dev)
    key = d.isoformat()
    rows = q(f"SELECT hour, SUM(wh) AS wh FROM energy_h WHERE hour>=? AND hour<=?{w} GROUP BY hour",
             [key + " 00", key + " 23"] + a)
    if rows:
        out = [0.0] * 24
        for r in rows:
            out[int(r["hour"][-2:])] = r["wh"]
        return out
    t0 = datetime(d.year, d.month, d.day, tzinfo=TZ).timestamp()
    rows = q(f"SELECT ts, w FROM power WHERE ts>=? AND ts<?{w}", [int(t0), int(t0 + 86400)] + a)
    if not rows:
        return [0.0] * 24
    out = [0.0] * 24
    for r in rows:
        h = datetime.fromtimestamp(r["ts"], TZ).hour
        out[h] += r["w"] / 60.0                      # Minutenwert -> Wh
    return out


def _months(dev, year):
    w, a = _devsql(dev)
    rows = q(f"SELECT substr(day,1,7) AS m, SUM(wh) AS wh FROM energy WHERE day>=? AND day<=?{w} GROUP BY m",
             [f"{year}-01-01", f"{year}-12-31"] + a)
    have = {r["m"]: r["wh"] for r in rows}
    return [have.get(f"{year}-{m:02d}", 0.0) for m in range(1, 13)]


def _series(dev, kind, d):
    """(Titel, Beschriftungen, Werte, Bucket-Enden als Zeitstempel, Start-Datum des Vorzeitraums)."""
    if kind == "day":
        vals = _hours(dev, d)
        t0 = datetime(d.year, d.month, d.day, tzinfo=TZ)
        ends = [(t0 + timedelta(hours=h + 1)).timestamp() for h in range(24)]
        return (f"{DAYS_L[d.weekday()]}, {d.strftime('%d.%m.%Y')}", [f"{h:02d}" for h in range(24)], vals, ends,
                d - timedelta(days=1))
    if kind == "week":
        mon = d - timedelta(days=d.weekday())
        days = [mon + timedelta(days=i) for i in range(7)]
        have = _days(dev, days[0], days[-1])
        vals = [have.get(x.isoformat(), 0.0) for x in days]
        ends = [datetime(x.year, x.month, x.day, tzinfo=TZ).timestamp() + 86400 for x in days]
        kw = mon.isocalendar()[1]
        return (f"KW {kw} · {mon.strftime('%d.%m.')}–{days[-1].strftime('%d.%m.%Y')}", DAYS, vals, ends,
                mon - timedelta(days=7))
    if kind == "month":
        n = calendar.monthrange(d.year, d.month)[1]
        days = [date(d.year, d.month, i + 1) for i in range(n)]
        have = _days(dev, days[0], days[-1])
        vals = [have.get(x.isoformat(), 0.0) for x in days]
        ends = [datetime(x.year, x.month, x.day, tzinfo=TZ).timestamp() + 86400 for x in days]
        pm = (d.replace(day=1) - timedelta(days=1)).replace(day=1)
        return (f"{MONTHS_L[d.month - 1]} {d.year}", [str(i + 1) for i in range(n)], vals, ends, pm)
    vals = _months(dev, d.year)
    ends = []
    for m in range(1, 13):
        nxt = date(d.year + (m == 12), 1 if m == 12 else m + 1, 1)
        ends.append(datetime(nxt.year, nxt.month, nxt.day, tzinfo=TZ).timestamp())
    return (str(d.year), MONTHS, vals, ends, date(d.year - 1, 1, 1))


def period(dev, kind, day):
    if kind not in ("day", "week", "month", "year"):
        kind = "month"
    try:
        d = date.fromisoformat(day) if day else now_local().date()
    except ValueError:
        d = now_local().date()
    now = now_local().timestamp()
    title, labels, cur, ends, pstart = _series(dev, kind, d)
    _, _, prev, _, _ = _series(dev, kind, pstart)
    starts = [ends[0] - (ends[1] - ends[0]) if len(ends) > 1 else ends[0] - 86400] + ends[:-1]
    state = ["done" if e <= now else ("now" if s <= now else "future") for s, e in zip(starts, ends)]
    shown = [None if st == "future" else round(v, 1) for v, st in zip(cur, state)]
    prev_shown = [round(prev[i], 1) if i < len(prev) else None for i in range(len(labels))]
    done = [i for i, st in enumerate(state) if st == "done"]
    total = sum(v for v, st in zip(cur, state) if st != "future")
    pr = price_eur_kwh()
    base = done or [i for i, st in enumerate(state) if st == "now"]
    vals = [cur[i] for i in base]
    cmp_cur = sum(cur[i] for i in done)
    cmp_prev = sum(prev[i] for i in done if i < len(prev))
    pct = round((cmp_cur / cmp_prev - 1) * 100, 1) if cmp_prev > 0 and done else None
    unit_s = {"day": "Stunde", "week": "Tag", "month": "Tag", "year": "Monat"}[kind]
    nxt = {"day": d + timedelta(days=1), "week": d + timedelta(days=7)}.get(kind)
    if kind == "month":
        nxt = (d.replace(day=28) + timedelta(days=4)).replace(day=1)
    if kind == "year":
        nxt = date(d.year + 1, 1, 1)
    prv = {"day": d - timedelta(days=1), "week": d - timedelta(days=7),
           "month": (d.replace(day=1) - timedelta(days=1)).replace(day=1), "year": date(d.year - 1, 1, 1)}[kind]
    return {
        "kind": kind, "date": d.isoformat(), "title": title, "labels": labels, "cur": shown, "prev": prev_shown,
        "state": state, "unit": unit_s,
        "total_wh": round(total, 1), "total_cost": round(total / 1000 * pr, 2),
        "prev_total_wh": round(sum(prev), 1),
        "avg_wh": round(sum(vals) / len(vals), 1) if vals else None,
        "max_wh": round(max(vals), 1) if vals else None, "min_wh": round(min(vals), 1) if vals else None,
        "max_label": labels[base[vals.index(max(vals))]] if vals else None,
        "min_label": labels[base[vals.index(min(vals))]] if vals else None,
        "cmp_pct": pct, "price": pr,
        "prev_date": prv.isoformat(), "next_date": nxt.isoformat() if nxt <= now_local().date() else None,
    }
