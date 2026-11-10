package io.github.sacksso.psbhelper;

import android.content.Context;
import android.webkit.CookieManager;
import android.webkit.WebSettings;

import java.net.NetworkInterface;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CopyOnWriteArrayList;

/** Estado compartido entre el servicio (servidor HTTP) y la actividad (WebView). Protocolo = tools/solver/psb_solver.py. */
final class Helper {
    private Helper() {}

    static final String PROTOCOL = "psb-solver 2";
    static final int PORT = 8765;

    static final Set<String> hosts = Collections.synchronizedSet(new LinkedHashSet<String>());   // sitios visitados: solo sus cookies se entregan
    static final List<StateBuilder.Dl> dls = new CopyOnWriteArrayList<StateBuilder.Dl>();
    static final Set<String> allow = Collections.synchronizedSet(new HashSet<String>());          // vacio = cualquier equipo de la red local
    static volatile String lastMsg = "sin actividad";
    static volatile String ua = "";
    static volatile String pendingUrl = null;
    static volatile boolean resumed = false;
    static volatile Context appCtx;
    static volatile MainActivity activity;
    static volatile SolverService service;
    static volatile String serverError = "";

    static void trackHost(String url) {
        String h = StateBuilder.hostOf(url);
        if (!h.isEmpty()) hosts.add(h);
    }

    static void addDownload(String url, String name, long size) {
        if (!StateBuilder.isHttpUrl(url)) return;
        for (StateBuilder.Dl d : dls) if (d.url.equals(url)) return;
        dls.add(new StateBuilder.Dl(url, name == null || name.isEmpty() ? "archivo" : name, size));
        trackHost(url);
        lastMsg = "descarga capturada: " + name;
    }

    static String currentUa() {
        String u = ua;
        if (!u.isEmpty()) return u;
        Context c = appCtx;
        if (c == null) return "";
        try { return StateBuilder.cleanUa(WebSettings.getDefaultUserAgent(c)); } catch (Throwable t) { return ""; }
    }

    static String statusLine() {
        String e = serverError.isEmpty() ? "" : "  |  ERROR: " + serverError;
        return "Sitios: " + hosts.size() + "  |  Descargas capturadas: " + dls.size() + "\n" + lastMsg + e;
    }

    /** IPv4 de la red local de este movil (la que se escribe en la PS4). */
    static List<String> lanIps() {
        List<String> out = new ArrayList<String>();
        try {
            Enumeration<NetworkInterface> en = NetworkInterface.getNetworkInterfaces();
            while (en != null && en.hasMoreElements()) {
                NetworkInterface ni = en.nextElement();
                if (!ni.isUp() || ni.isLoopback()) continue;
                Enumeration<java.net.InetAddress> ad = ni.getInetAddresses();
                while (ad.hasMoreElements()) {
                    java.net.InetAddress a = ad.nextElement();
                    if (a instanceof java.net.Inet4Address && a.isSiteLocalAddress()) out.add(a.getHostAddress());
                }
            }
        } catch (Exception ignored) {}
        return out;
    }

    static HttpServerLite.Resp handle(String clientIp, String path, Map<String, String> q) {
        if (!StateBuilder.clientAllowed(clientIp, allow)) return new HttpServerLite.Resp(403, "no permitido");
        if (path.equals("/ping")) return new HttpServerLite.Resp(200, PROTOCOL);
        if (path.equals("/status")) return new HttpServerLite.Resp(200, lastMsg + " | descargas capturadas: " + dls.size());
        if (path.equals("/media")) return new HttpServerLite.Resp(200, "ERR\t/media no esta disponible en el ayudante de Android (usa el del PC con yt-dlp)");
        if (path.equals("/open")) {
            String url = q.get("url");
            if (!StateBuilder.isHttpUrl(url)) return new HttpServerLite.Resp(400, "url no valida");
            trackHost(url);
            MainActivity a = activity;
            if (a != null) a.openFromHelper(url);
            else pendingUrl = url;
            if (a == null || !resumed) {
                SolverService s = service;
                if (s != null) s.notifyOpen(url);
                pendingUrl = a == null ? url : null;
                lastMsg = "pagina pendiente: abre PSB Ayudante en el movil";
                return new HttpServerLite.Resp(200, "ok (Android): toca la notificacion de PSB Ayudante en el movil para ver la pagina y resolver el captcha");
            }
            lastMsg = "abierta en el movil: " + url;
            return new HttpServerLite.Resp(200, "ok (Android): resuelve el captcha en el movil; despues pulsa 'Traer cookies y descargas' en la PS4");
        }
        if (path.equals("/state")) {
            Map<String, String> m = new LinkedHashMap<String, String>();
            CookieManager cm = CookieManager.getInstance();
            synchronized (hosts) {
                for (String h : hosts) {
                    String c = cm.getCookie("https://" + h + "/");
                    m.put(h, c == null ? "" : c);
                }
            }
            return new HttpServerLite.Resp(200, StateBuilder.build(currentUa(), m, new ArrayList<StateBuilder.Dl>(dls)));
        }
        return new HttpServerLite.Resp(404, "desconocido");
    }
}
