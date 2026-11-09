#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psb_solver - ayudante para resolver captchas / verificaciones EN EL PC y pasar el resultado a psbrowser (PS4).

NO resuelve nada automaticamente: abre la pagina en un navegador real en tu PC, TU resuelves el captcha / pulsas el boton, y este
programa le pasa a la PS4 las cookies que resultaron (p. ej. cf_clearance), el User-Agent con el que se resolvieron y las descargas
que el navegador del PC intento iniciar. Despues la PS4 descarga ella misma, con su motor de descargas.

Uso (PC y PS4 en la misma red):
    pip install playwright
    python psb_solver.py                     # abre Chromium; muestra la direccion IP:puerto
    python psb_solver.py --channel chrome    # usar tu Google Chrome instalado (suele pasar mejor las verificaciones)
En la PS4: barra de direcciones -> !solver IP:puerto  (una sola vez), y F7 en la pagina que pide la verificacion.

Protocolo (texto plano, HTTP GET):
    /ping                -> "psb-solver 1"
    /open?url=<url>      -> abre la url en el navegador del PC
    /state               -> lineas "#UA<TAB>dominio<TAB>User-Agent", "#DL<TAB>url<TAB>nombre<TAB>tamano" y cookies en formato Netscape
"""
import argparse
import json
import os
import shutil
import subprocess
import queue
import socket
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTOCOL = "psb-solver 2"

# v0.9.2: modo ayuda (/open?...&auto=1). Cuando la pagina ya NO muestra un captcha pendiente, pulsa por ti el boton de descarga
# ("Descargar", "Download", "Continuar"...) y captura el enlace real. El captcha lo resuelves TU; el ayudante nunca lo toca.
import re
CLICK_WORDS = re.compile(r"^\s*(free\s+)?(slow\s+)?(descargar|download|descarga|get\s+link|obtener\s+enlace|continuar|continue|generate|generar|"
                         r"click\s+here|haz\s+clic|start\s+download|iniciar\s+descarga|create\s+download\s+link|proceed)\b", re.I)
CAPTCHA_SELECTORS = ["iframe[src*='challenges.cloudflare.com']", "iframe[src*='hcaptcha.com']", "iframe[src*='recaptcha']",
                     ".cf-turnstile", ".h-captcha", ".g-recaptcha", "#cf-challenge-running"]
AD_WORDS = ("adsterra", "popads", "propeller", "exoclick", "clickadu", "doubleclick", "juicyads", "trafficjunky")


def looks_like_download_button(text):
    """Funcion pura (se prueba sin navegador): el texto visible de un boton/enlace parece uno de descarga?"""
    t = (text or "").strip()
    return 0 < len(t) <= 30 and len(t.split()) <= 5 and bool(CLICK_WORDS.match(t))


def pick_media(info, max_h=720):
    """Elige UN formato reproducible por el AvPlayer de la PS4 (un solo archivo con video+audio) a partir del JSON de yt-dlp.
    Devuelve (url, titulo, referer, tipo) o None. Funcion pura (se prueba sin yt-dlp ni red).
    Orden: MP4 progresivo con audio hasta max_h -> HLS (directos) -> cualquier progresivo con audio."""
    fmts = list(info.get("formats") or [])
    if not fmts and info.get("url"):
        fmts = [info]
    hdr = info.get("http_headers") or {}
    ref = hdr.get("Referer") or info.get("webpage_url") or ""

    def has_av(f):
        return (f.get("vcodec") not in (None, "none")) and (f.get("acodec") not in (None, "none"))

    def h(f):
        return int(f.get("height") or 0)

    prog = [f for f in fmts if f.get("url") and has_av(f) and str(f.get("protocol", "")).startswith(("http", "https")) and f.get("ext") == "mp4"]
    ok = [f for f in prog if 0 < h(f) <= max_h] or prog
    if ok:
        best = sorted(ok, key=lambda f: (h(f) <= max_h, h(f), f.get("tbr") or 0))[-1]
        return best["url"], info.get("title") or "video", ref, "mp4"
    hls = [f for f in fmts if f.get("url") and "m3u8" in str(f.get("protocol", "")) and has_av(f)]
    if hls:
        best = sorted(hls, key=lambda f: (h(f) <= max_h, h(f)))[-1]
        return best["url"], info.get("title") or "directo", ref, "hls"
    anyp = [f for f in fmts if f.get("url") and has_av(f)]
    if anyp:
        best = anyp[-1]
        return best["url"], info.get("title") or "video", ref, "otro"
    return None


def resolve_media(url, max_h=720):
    """Pide a yt-dlp (instalado en el PC) la URL directa de un video/directo (YouTube, Twitch, Stripchat...). Devuelve una linea de texto."""
    exe = shutil.which("yt-dlp") or shutil.which("yt_dlp")
    if not exe:
        return "ERR\tyt-dlp no esta instalado en el PC (pip install -U yt-dlp)"
    try:
        p = subprocess.run([exe, "-J", "--no-playlist", "--no-warnings", url], capture_output=True, text=True, timeout=50)
    except subprocess.TimeoutExpired:
        return "ERR\tyt-dlp tardo demasiado"
    if p.returncode != 0 or not p.stdout.strip():
        msg = (p.stderr or "error desconocido").strip().splitlines()[-1][:200] if (p.stderr or "").strip() else "error desconocido"
        return "ERR\t" + msg
    try:
        info = json.loads(p.stdout)
    except ValueError:
        return "ERR\trespuesta de yt-dlp ilegible"
    r = pick_media(info, max_h)
    if not r:
        return "ERR\tno hay un formato con video y audio juntos que la PS4 pueda reproducir"
    return "OK\t%s\t%s\t%s\t%s" % (r[0], r[1].replace("\t", " ").replace("\n", " "), r[2], r[3])


def is_ad_host(host):
    h = (host or "").lower()
    return any(w in h for w in AD_WORDS)


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def netscape_line(c):
    """Cookie de Playwright -> linea Netscape (con el prefijo #HttpOnly_ que entiende la PS4)."""
    dom = c.get("domain", "")
    sub = "TRUE" if dom.startswith(".") else "FALSE"
    exp = c.get("expires", -1)
    exp = 0 if exp is None or exp < 0 else int(exp)
    pre = "#HttpOnly_" if c.get("httpOnly") else ""
    return "%s%s\t%s\t%s\t%s\t%d\t%s\t%s" % (pre, dom, sub, c.get("path", "/") or "/", "TRUE" if c.get("secure") else "FALSE", exp, c.get("name", ""), c.get("value", ""))


def build_state(cookies, ua, downloads):
    """Texto que recibe la PS4. Funcion pura (se prueba sin navegador)."""
    out = []
    seen = set()
    for c in cookies:
        d = c.get("domain", "").lstrip(".")
        if d and d not in seen and ua:
            seen.add(d)
            out.append("#UA\t%s\t%s" % (d, ua))
    for d in downloads:
        out.append("#DL\t%s\t%s\t%s" % (d["url"], d["name"].replace("\t", " "), d.get("size", -1)))
    out.append("# Netscape HTTP Cookie File")
    for c in cookies:
        if c.get("name"):
            out.append(netscape_line(c))
    return "\n".join(out) + "\n"


class Backend:
    """Navegador real (Playwright). Todas las llamadas a Playwright ocurren en el hilo principal."""

    def __init__(self, args):
        self.args = args
        self.downloads = []
        self.ua = ""
        self.ctx = None
        self.pw = None
        self.auto_pages = {}     # page -> {"t": ultimo intento, "n": clics hechos}
        self.last_msg = "sin actividad"

    def start(self):
        from playwright.sync_api import sync_playwright  # import tardio: --fake no lo necesita
        self.pw = sync_playwright().start()
        kw = dict(headless=False, accept_downloads=True, viewport={"width": 1280, "height": 800})
        if self.args.channel:
            kw["channel"] = self.args.channel
        profile = os.path.abspath(os.path.expanduser(self.args.profile))
        self.ctx = self.pw.chromium.launch_persistent_context(profile, **kw)
        self.ctx.on("response", self._on_response)
        self.ctx.on("page", self._watch_page)
        for p in self.ctx.pages:
            self._watch_page(p)
        page = self.ctx.pages[0] if self.ctx.pages else self.ctx.new_page()
        try:
            self.ua = page.evaluate("navigator.userAgent").replace("HeadlessChrome", "Chrome")
        except Exception:
            self.ua = ""

    def _watch_page(self, page):
        page.on("download", self._on_download)
        page.on("popup", self._on_popup)

    def _on_popup(self, popup):
        """Las ventanas emergentes de publicidad se cierran solas; las demas se vigilan igual que la principal."""
        try:
            popup.wait_for_load_state("domcontentloaded", timeout=8000)
            host = urllib.parse.urlparse(popup.url).netloc
            if is_ad_host(host):
                print("  [ayuda] ventana de publicidad cerrada (%s)" % host)
                popup.close()
        except Exception:
            pass

    def _remember(self, url, name, size):
        if any(d["url"] == url for d in self.downloads):
            return
        self.downloads.append({"url": url, "name": name or url.rsplit("/", 1)[-1].split("?")[0] or "archivo", "size": size})
        print("  [descarga capturada] %s" % (name or url))

    def _on_download(self, dl):
        self._remember(dl.url, dl.suggested_filename, -1)
        if not self.args.keep_downloads:
            try:
                dl.cancel()   # que la descarga la haga la PS4
            except Exception:
                pass

    def _on_response(self, resp):
        try:
            h = {k.lower(): v for k, v in resp.headers.items()}
            cd = h.get("content-disposition", "")
            ct = h.get("content-type", "")
            if "attachment" in cd.lower() or ct.startswith("application/octet-stream") or ct.startswith("application/x-"):
                name = ""
                if "filename=" in cd:
                    name = cd.split("filename=", 1)[1].strip().strip('";')
                self._remember(resp.url, name, int(h.get("content-length", "-1") or -1))
        except Exception:
            pass

    def open(self, url, auto=False):
        page = self.ctx.new_page()
        page.goto(url, wait_until="domcontentloaded", timeout=45000)
        page.bring_to_front()
        if auto:
            self.auto_pages[page] = {"t": 0.0, "n": 0}
            self.last_msg = "abierta en modo ayuda: resuelve el captcha; luego pulsare 'Descargar' por ti"
        return "ok"

    def _auto_step(self, page, st):
        """Un paso del modo ayuda, solo con la pagina quieta (se llama desde el hilo principal)."""
        if page.is_closed():
            return False
        if st["n"] >= 6 or time.time() - st["t"] < 2.5:
            return True
        st["t"] = time.time()
        for sel in CAPTCHA_SELECTORS:               # hay un captcha a la vista: esperar a que lo resuelvas
            try:
                el = page.query_selector(sel)
                if el and el.is_visible():
                    self.last_msg = "esperando a que resuelvas el captcha en el PC"
                    return True
            except Exception:
                pass
        try:
            cands = page.query_selector_all("a, button, input[type=submit], input[type=button], [role=button]")
        except Exception:
            return True
        for el in cands:
            try:
                if not el.is_visible() or not el.is_enabled():
                    continue
                txt = (el.inner_text() or el.get_attribute("value") or "").strip()
                if not looks_like_download_button(txt):
                    continue
                href = el.get_attribute("href") or ""
                if href.startswith("http") and is_ad_host(urllib.parse.urlparse(href).netloc):
                    continue
                st["n"] += 1
                self.last_msg = "pulsado '%s' (%d)" % (txt[:30], st["n"])
                print("  [ayuda] " + self.last_msg)
                el.click(timeout=3000)
                return True
            except Exception:
                continue
        return True

    def state(self):
        return build_state(self.ctx.cookies(), self.ua, self.downloads)

    def pump(self, ms):
        pages = self.ctx.pages
        for pg in list(self.auto_pages):
            try:
                if not self._auto_step(pg, self.auto_pages[pg]):
                    del self.auto_pages[pg]
            except Exception:
                self.auto_pages.pop(pg, None)
        if pages:
            pages[0].wait_for_timeout(ms)   # mantiene vivos los eventos de Playwright
        else:
            time.sleep(ms / 1000.0)

    def status(self):
        return "%s | descargas capturadas: %d" % (self.last_msg, len(self.downloads))


class FakeBackend:
    """Sin navegador: para comprobar la conexion PS4 <-> PC (--fake)."""

    def __init__(self, args):
        self.downloads = [{"url": "http://%s/prueba.bin" % lan_ip(), "name": "prueba.bin", "size": 1048576}]

    def start(self):
        pass

    def open(self, url, auto=False):
        print("  [fake] abriria", url, "(modo ayuda)" if auto else "")
        return "ok (fake)"

    def status(self):
        return "fake | descargas capturadas: %d" % len(self.downloads)

    def state(self):
        cookies = [{"domain": ".ejemplo.test", "path": "/", "secure": True, "httpOnly": True, "expires": time.time() + 86400, "name": "cf_clearance", "value": "FAKE"}]
        return build_state(cookies, "Mozilla/5.0 (fake) psb_solver", self.downloads)

    def pump(self, ms):
        time.sleep(ms / 1000.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--bind", default="", help="IP a la que escuchar (por defecto la IP de la red local)")
    ap.add_argument("--allow", default="", help="IPs permitidas separadas por comas (p. ej. la de la PS4). Muy recomendable")
    ap.add_argument("--channel", default="", help="chrome | msedge: usar el navegador instalado en vez de Chromium")
    ap.add_argument("--profile", default="~/.psb_solver_profile", help="carpeta del perfil (las verificaciones resueltas se conservan entre usos)")
    ap.add_argument("--keep-downloads", action="store_true", help="no cancelar en el PC las descargas capturadas")
    ap.add_argument("--fake", action="store_true", help="sin navegador (prueba de conexion)")
    args = ap.parse_args()

    backend = FakeBackend(args) if args.fake else Backend(args)
    jobs = queue.Queue()
    allow = {a.strip() for a in args.allow.split(",") if a.strip()}

    def call(fn, *a):
        ev, box = threading.Event(), {}
        jobs.put((fn, a, ev, box))
        if not ev.wait(60):
            raise TimeoutError("el navegador no respondio")
        if "err" in box:
            raise box["err"]
        return box["ok"]

    class H(BaseHTTPRequestHandler):
        def log_message(self, fmt, *a):
            print("  %s %s" % (self.client_address[0], fmt % a))

        def _send(self, code, text):
            data = text.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if allow and self.client_address[0] not in allow:
                return self._send(403, "no permitido")
            u = urllib.parse.urlparse(self.path)
            q = urllib.parse.parse_qs(u.query)
            try:
                if u.path == "/ping":
                    return self._send(200, PROTOCOL)
                if u.path == "/open":
                    url = (q.get("url") or [""])[0]
                    if not url.startswith(("http://", "https://")):
                        return self._send(400, "url no valida")
                    return self._send(200, call(backend.open, url, (q.get("auto") or ["0"])[0] == "1"))
                if u.path == "/media":
                    url = (q.get("url") or [""])[0]
                    if not url.startswith(("http://", "https://")):
                        return self._send(400, "ERR\turl no valida")
                    return self._send(200, resolve_media(url, int((q.get("h") or ["720"])[0] or 720)))
                if u.path == "/status":
                    return self._send(200, call(backend.status))
                if u.path == "/state":
                    return self._send(200, call(backend.state))
                return self._send(404, "desconocido")
            except Exception as e:  # noqa: BLE001
                return self._send(500, "error: %s" % e)

    ip = args.bind or lan_ip()
    srv = ThreadingHTTPServer((ip, args.port), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    backend.start()
    print("psb_solver listo.  En la PS4, barra de direcciones:  !solver %s:%d" % (ip, args.port))
    if not allow:
        print("Aviso: cualquiera de tu red puede pedir las cookies de este perfil. Usa --allow <IP de la PS4>.")
    try:
        while True:
            try:
                fn, a, ev, box = jobs.get(timeout=0.1)
                try:
                    box["ok"] = fn(*a)
                except Exception as e:  # noqa: BLE001
                    box["err"] = e
                ev.set()
            except queue.Empty:
                backend.pump(100)
    except KeyboardInterrupt:
        print("adios")
    finally:
        srv.shutdown()


if __name__ == "__main__":
    sys.exit(main())
