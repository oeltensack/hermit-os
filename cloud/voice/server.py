# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Pagurus Sprachdienst – laeuft als eigener Container neben der Cloud.

POST /stt    Audio -> Text. WAV oder rohes PCM (16 bit, mono, ?rate=16000). ?prompt= Woerter, die vorkommen koennen
             (Geraete-, Raum- und Szenennamen) – Whisper erkennt sie dann deutlich sicherer.
POST /tts    {"text": "...", "rate": 22050} -> WAV (16 bit, mono, gewuenschte Abtastrate)
GET  /health Zustand und geladene Modelle

Modelle werden beim ersten Gebrauch geladen bzw. heruntergeladen (Ordner /data):
WHISPER_MODEL (Standard small – base ist schneller, medium genauer), PIPER_VOICE (de_DE-thorsten-medium, CC0).
"""
import io
import os
import threading
import time
import wave
from pathlib import Path

import numpy as np
from fastapi import FastAPI, HTTPException, Request, Response

DATA = Path(os.environ.get("VOICE_DATA", "/data"))
WHISPER_MODEL = os.environ.get("WHISPER_MODEL", "small")
WHISPER_COMPUTE = os.environ.get("WHISPER_COMPUTE", "int8")
PIPER_VOICE = os.environ.get("PIPER_VOICE", "de_DE-thorsten-medium")
THREADS = int(os.environ.get("VOICE_THREADS", "0")) or max(1, (os.cpu_count() or 2))

app = FastAPI(title="Pagurus Sprachdienst")
_stt = _tts = None
_stt_lock, _tts_lock, _load_lock = threading.Lock(), threading.Lock(), threading.Lock()
_err = {"stt": "", "tts": ""}


def stt_model():
    global _stt
    with _load_lock:
        if _stt is None:
            from faster_whisper import WhisperModel
            _stt = WhisperModel(WHISPER_MODEL, device="cpu", compute_type=WHISPER_COMPUTE, cpu_threads=THREADS,
                                download_root=str(DATA / "whisper"))
    return _stt


def tts_voice():
    global _tts
    with _load_lock:
        if _tts is None:
            from piper import PiperVoice
            from piper.download_voices import download_voice
            d = DATA / "piper"
            d.mkdir(parents=True, exist_ok=True)
            onnx = d / f"{PIPER_VOICE}.onnx"
            if not onnx.exists():
                download_voice(PIPER_VOICE, d)
            _tts = PiperVoice.load(str(onnx))
    return _tts


def resample(x, src, dst):
    """Einfache lineare Umrechnung der Abtastrate – fuer Sprache ausreichend."""
    if src == dst or not len(x):
        return x.astype(np.float32)
    n = int(round(len(x) * dst / src))
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x).astype(np.float32)


def read_audio(body, ctype, rate):
    """-> float32 mono 16 kHz"""
    if body[:4] == b"RIFF" or "wav" in (ctype or ""):
        with wave.open(io.BytesIO(body)) as w:
            sw, ch, sr = w.getsampwidth(), w.getnchannels(), w.getframerate()
            raw = w.readframes(w.getnframes())
        if sw == 2:
            a = np.frombuffer(raw, "<i2").astype(np.float32) / 32768
        elif sw == 4:
            a = np.frombuffer(raw, "<i4").astype(np.float32) / 2147483648
        else:
            raise HTTPException(400, "WAV mit 16 oder 32 bit erwartet")
        if ch > 1:
            a = a.reshape(-1, ch)[:, 0]
        return resample(a, sr, 16000)
    a = np.frombuffer(body[: len(body) // 2 * 2], "<i2").astype(np.float32) / 32768
    return resample(a, rate, 16000)


def wav_bytes(a16, rate):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(a16.tobytes())
    return b.getvalue()


@app.post("/stt")
async def stt(request: Request, rate: int = 16000, prompt: str = "", lang: str = "de"):
    body = await request.body()
    if len(body) < 3200:
        raise HTTPException(400, "Zu wenig Audio")
    a = read_audio(body, request.headers.get("content-type", ""), rate)
    t0 = time.time()
    try:
        m = stt_model()
        with _stt_lock:
            seg, info = m.transcribe(a, language=lang or None, beam_size=1, vad_filter=True,
                                     initial_prompt=prompt[:600] or None, condition_on_previous_text=False)
            text = " ".join(s.text.strip() for s in seg).strip()
        _err["stt"] = ""
    except Exception as e:
        _err["stt"] = str(e)[:200]
        raise HTTPException(500, f"Spracherkennung: {e}")
    return {"text": text, "ms": int((time.time() - t0) * 1000), "secs": round(len(a) / 16000, 2)}


@app.post("/tts")
async def tts(request: Request):
    d = await request.json()
    text = str(d.get("text") or "").strip()[:600]
    if not text:
        raise HTTPException(400, "Text fehlt")
    rate = int(d.get("rate") or 22050)
    try:
        from piper import SynthesisConfig
        v = tts_voice()
        cfg = SynthesisConfig(length_scale=float(d.get("speed_scale") or 1.0), volume=float(d.get("volume") or 1.0))
        with _tts_lock:
            chunks = list(v.synthesize(text, syn_config=cfg))
        _err["tts"] = ""
    except Exception as e:
        _err["tts"] = str(e)[:200]
        raise HTTPException(500, f"Sprachausgabe: {e}")
    if not chunks:
        raise HTTPException(500, "Keine Ausgabe")
    sr = chunks[0].sample_rate
    a = np.concatenate([c.audio_float_array for c in chunks]).astype(np.float32)
    a = resample(a, sr, rate)
    a16 = (np.clip(a, -1, 1) * 32767).astype("<i2")
    return Response(wav_bytes(a16, rate), media_type="audio/wav")


@app.get("/health")
def health():
    return {"ok": True, "stt_model": WHISPER_MODEL, "stt_loaded": _stt is not None, "tts_voice": PIPER_VOICE,
            "tts_loaded": _tts is not None, "threads": THREADS, "errors": _err}


@app.on_event("startup")
def warm():
    def load():                                   # Modelle im Hintergrund laden, damit der erste Befehl schnell ist
        for f in (tts_voice, stt_model):
            try:
                f()
            except Exception as e:
                print("Laden fehlgeschlagen:", e, flush=True)
    threading.Thread(target=load, daemon=True).start()
