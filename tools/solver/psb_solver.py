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
import os
import queue
import socket
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTOCOL = "psb-solver 1"


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

    def open(self, url):
        page = self.ctx.new_page()
        page.goto(url, wait_until="domcontentloaded", timeout=45000)
        page.bring_to_front()
        return "ok"

    def state(self):
        return build_state(self.ctx.cookies(), self.ua, self.downloads)

    def pump(self, ms):
        pages = self.ctx.pages
        if pages:
            pages[0].wait_for_timeout(ms)   # mantiene vivos los eventos de Playwright
        else:
            time.sleep(ms / 1000.0)


class FakeBackend:
    """Sin navegador: para comprobar la conexion PS4 <-> PC (--fake)."""

    def __init__(self, args):
        self.downloads = [{"url": "http://%s/prueba.bin" % lan_ip(), "name": "prueba.bin", "size": 1048576}]

    def start(self):
        pass

    def open(self, url):
        print("  [fake] abriria", url)
        return "ok (fake)"

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
                    return self._send(200, call(backend.open, url))
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
