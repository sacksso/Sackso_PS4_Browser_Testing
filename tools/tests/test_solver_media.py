#!/usr/bin/env python3
"""Pruebas de las funciones puras de tools/solver/psb_solver.py (sin navegador ni red): python3 tools/tests/test_solver_media.py"""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("s", os.path.join(here, "..", "solver", "psb_solver.py"))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

def F(fid, h, ext="mp4", v="avc1", a="mp4a", proto="https", tbr=500, url=None):
    return {"format_id": fid, "height": h, "ext": ext, "vcodec": v, "acodec": a, "protocol": proto, "tbr": tbr, "url": url or "http://x/%s" % fid}

# YouTube tipico: 18 (360p mp4 con audio), 22 (720p) y formatos DASH sueltos que NO deben elegirse
info = {"title": "T", "webpage_url": "https://youtu.be/x", "formats": [
    F("137", 1080, v="avc1", a="none"), F("140", 0, ext="m4a", v="none", a="mp4a"), F("18", 360), F("22", 720, tbr=900)]}
r = m.pick_media(info); assert r[0].endswith("/22") and r[3] == "mp4", r
r = m.pick_media(info, 480); assert r[0].endswith("/18"), r
# solo DASH separado -> sin resultado
assert m.pick_media({"formats": [F("137", 1080, a="none"), F("140", 0, ext="m4a", v="none")]}) is None
# directo HLS (como Twitch/Stripchat)
live = {"title": "L", "formats": [F("720p", 720, ext="mp4", proto="m3u8_native", url="http://x/p.m3u8"), F("480p", 480, ext="mp4", proto="m3u8_native", url="http://x/q.m3u8")]}
r = m.pick_media(live); assert r[3] == "hls" and r[0].endswith("p.m3u8"), r
# un solo formato en la raiz (enlace directo)
r = m.pick_media({"url": "http://x/a.mp4", "ext": "mp4", "vcodec": "h264", "acodec": "aac", "protocol": "https", "height": 480, "title": "D"}); assert r and r[3] == "mp4", r
# botones
for t in ["Download", "Descargar", "Free Download", "Click here to download", "Continuar"]: assert m.looks_like_download_button(t), t
for t in ["", "Home", "Login", "Download Android app now please come on", "x" * 60]: assert not m.looks_like_download_button(t), t
assert m.is_ad_host("ads.popads.net") and not m.is_ad_host("vikingfile.com")
# resolve_media sin yt-dlp instalado -> ERR legible
os_path = os.environ.get("PATH", ""); os.environ["PATH"] = ""
try:
    assert m.resolve_media("https://youtu.be/x").startswith("ERR\t")
finally:
    os.environ["PATH"] = os_path
print("test_solver_media OK")
