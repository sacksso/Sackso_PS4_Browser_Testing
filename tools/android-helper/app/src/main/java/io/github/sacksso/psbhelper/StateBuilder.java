package io.github.sacksso.psbhelper;

import java.net.InetAddress;
import java.net.URLDecoder;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/** Logica pura (sin Android): se prueba en el PC con tests/StateBuilderTest.java. Formato = el de tools/solver/psb_solver.py. */
final class StateBuilder {
    private StateBuilder() {}

    static final class Dl {
        final String url, name;
        final long size;
        Dl(String url, String name, long size) { this.url = url; this.name = name; this.size = size; }
    }

    static boolean isHttpUrl(String u) {
        return u != null && (u.startsWith("http://") || u.startsWith("https://"));
    }

    /** Host en minusculas, sin puerto ni usuario; "" si no es http(s). */
    static String hostOf(String url) {
        if (!isHttpUrl(url)) return "";
        int a = url.startsWith("https://") ? 8 : 7;
        int e = a;
        while (e < url.length() && "/?#".indexOf(url.charAt(e)) < 0) e++;
        String h = url.substring(a, e);
        int at = h.lastIndexOf('@');
        if (at >= 0) h = h.substring(at + 1);
        if (!h.startsWith("[")) { int c = h.indexOf(':'); if (c >= 0) h = h.substring(0, c); }
        return h.toLowerCase(Locale.ROOT);
    }

    /** User-Agent del WebView -> User-Agent "de Chrome": quita "; wv" y "Version/4.0" (algunas verificaciones desconfian del WebView). */
    static String cleanUa(String ua) {
        if (ua == null) return "";
        return ua.replace("; wv)", ")").replace(" Version/4.0", "").trim();
    }

    static String urlDecode(String s) {
        try { return URLDecoder.decode(s, "UTF-8"); } catch (Exception e) { return s; }
    }

    static Map<String, String> parseQuery(String q) {
        Map<String, String> m = new LinkedHashMap<>();
        if (q == null || q.isEmpty()) return m;
        for (String part : q.split("&")) {
            if (part.isEmpty()) continue;
            int eq = part.indexOf('=');
            String k = urlDecode(eq < 0 ? part : part.substring(0, eq));
            String v = eq < 0 ? "" : urlDecode(part.substring(eq + 1));
            if (!m.containsKey(k)) m.put(k, v);
        }
        return m;
    }

    /** Sin lista 'allow' solo se aceptan equipos de la red local (privados, loopback, enlace local), nunca Internet. */
    static boolean clientAllowed(String ip, Set<String> allow) {
        if (ip == null) return false;
        int pct = ip.indexOf('%');
        if (pct >= 0) ip = ip.substring(0, pct);
        if (allow != null && !allow.isEmpty()) return allow.contains(ip);
        try {
            if (!ip.matches("[0-9a-fA-F:.]+")) return false;   // solo literales: nunca resolver nombres (DNS)
            InetAddress a = InetAddress.getByName(ip);
            if (a.isLoopbackAddress() || a.isSiteLocalAddress() || a.isLinkLocalAddress()) return true;
            byte[] b = a.getAddress();
            return b.length == 16 && (b[0] & 0xFE) == 0xFC;   // IPv6 ULA fc00::/7
        } catch (Exception e) {
            return false;
        }
    }

    static boolean looksLikeDownloadUrl(String url) {
        if (!isHttpUrl(url)) return false;
        int q = url.indexOf('?'); int h = url.indexOf('#');
        int end = url.length();
        if (q >= 0) end = Math.min(end, q);
        if (h >= 0) end = Math.min(end, h);
        String p = url.substring(0, end).toLowerCase(Locale.ROOT);
        for (String e : new String[]{".pkg", ".zip", ".7z", ".rar", ".iso", ".apk", ".exe", ".bin", ".tar", ".gz", ".xz", ".rpk"})
            if (p.endsWith(e)) return true;
        return false;
    }

    private static String clean(String s) { return s.replace('\t', ' ').replace('\r', ' ').replace('\n', ' '); }

    /**
     * Texto que recibe la PS4 (igual que build_state de psb_solver.py):
     * "#UA\tdominio\tUser-Agent" por sitio, "#DL\turl\tnombre\ttamano", y cookies en formato Netscape.
     * cookiesByHost: host -> cabecera "a=b; c=d" de CookieManager (puede ser ""). Las cookies se entregan como ".host" (valen para sus subdominios).
     */
    static String build(String ua, Map<String, String> cookiesByHost, List<Dl> dls) {
        StringBuilder sb = new StringBuilder();
        if (ua != null && !ua.isEmpty())
            for (String h : cookiesByHost.keySet()) sb.append("#UA\t").append(clean(h)).append('\t').append(clean(ua)).append('\n');
        for (Dl d : dls)
            sb.append("#DL\t").append(clean(d.url)).append('\t').append(clean(d.name)).append('\t').append(d.size).append('\n');
        sb.append("# Netscape HTTP Cookie File\n");
        for (Map.Entry<String, String> e : cookiesByHost.entrySet()) {
            String c = e.getValue();
            if (c == null || c.isEmpty()) continue;
            for (String pair : c.split(";")) {
                String t = pair.trim();
                int eq = t.indexOf('=');
                if (eq <= 0) continue;
                sb.append('.').append(clean(e.getKey())).append("\tTRUE\t/\tFALSE\t0\t")
                  .append(clean(t.substring(0, eq))).append('\t').append(clean(t.substring(eq + 1))).append('\n');
            }
        }
        return sb.toString();
    }
}
