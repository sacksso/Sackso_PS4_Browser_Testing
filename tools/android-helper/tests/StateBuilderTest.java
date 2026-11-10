package io.github.sacksso.psbhelper;

import java.io.*;
import java.net.*;
import java.util.*;

/** Prueba en el PC (sin Android). Ver tools/android-helper/README.md. */
public class StateBuilderTest {
    static int fails = 0;
    static void check(boolean c, String what) { if (!c) { System.out.println("  FALLO: " + what); fails++; } }

    static String get(int port, String path) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL("http://127.0.0.1:" + port + path).openConnection();
        int code = c.getResponseCode();
        InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        byte[] buf = new byte[4096]; int n;
        while ((n = in.read(buf)) > 0) b.write(buf, 0, n);
        return code + "|" + new String(b.toByteArray(), "UTF-8");
    }

    public static void main(String[] a) throws Exception {
        System.out.println("[1] host y URLs");
        check(StateBuilder.hostOf("https://User:p@WWW.Mediafire.com:8443/file/x?y#z").equals("www.mediafire.com"), "hostOf");
        check(StateBuilder.hostOf("ftp://x/").isEmpty(), "hostOf ftp");
        check(StateBuilder.looksLikeDownloadUrl("https://d1.example.com/a/b/Juego.PKG?token=1"), "pkg");
        check(!StateBuilder.looksLikeDownloadUrl("https://example.com/page.html"), "html");
        check(!StateBuilder.looksLikeDownloadUrl("https://example.com/x?f=a.zip"), "zip en query");

        System.out.println("[2] User-Agent");
        String wv = "Mozilla/5.0 (Linux; Android 13; SM-A750F Build/TQ3A; wv) AppleWebKit/537.36 (KHTML, like Gecko) Version/4.0 Chrome/120.0.0.0 Mobile Safari/537.36";
        String cl = StateBuilder.cleanUa(wv);
        check(!cl.contains("wv") && !cl.contains("Version/4.0") && cl.contains("Chrome/120.0.0.0"), "cleanUa: " + cl);

        System.out.println("[3] clientes permitidos");
        check(StateBuilder.clientAllowed("192.168.1.50", null), "192.168");
        check(StateBuilder.clientAllowed("10.0.0.7", new HashSet<String>()), "10.x");
        check(StateBuilder.clientAllowed("172.20.1.1", null), "172.16/12");
        check(StateBuilder.clientAllowed("127.0.0.1", null), "loopback");
        check(StateBuilder.clientAllowed("fe80::1%wlan0", null), "ipv6 enlace local");
        check(StateBuilder.clientAllowed("fd12:3456::1", null), "ipv6 ULA");
        check(!StateBuilder.clientAllowed("8.8.8.8", null), "internet");
        check(!StateBuilder.clientAllowed("172.32.0.1", null), "fuera de 172.16/12");
        check(!StateBuilder.clientAllowed("ejemplo.com", null), "nombres no se resuelven");
        check(!StateBuilder.clientAllowed(null, null), "null");
        check(StateBuilder.clientAllowed("203.0.113.9", new HashSet<String>(Arrays.asList("203.0.113.9"))), "lista allow permite");
        check(!StateBuilder.clientAllowed("192.168.1.50", new HashSet<String>(Arrays.asList("192.168.1.99"))), "lista allow excluye");

        System.out.println("[4] formato /state (igual que psb_solver.py)");
        Map<String, String> m = new LinkedHashMap<String, String>();
        m.put("www.mediafire.com", "cf_clearance=ABC123; session=x=y; ; bad");
        m.put("download1.mediafire.com", "");
        List<StateBuilder.Dl> dls = new ArrayList<StateBuilder.Dl>();
        dls.add(new StateBuilder.Dl("https://download1.mediafire.com/a/b.pkg", "Juego\tX.pkg", 123456L));
        String st = StateBuilder.build("UA/1", m, dls);
        String[] L = st.split("\n");
        check(L[0].equals("#UA\twww.mediafire.com\tUA/1"), "UA1: " + L[0]);
        check(L[1].equals("#UA\tdownload1.mediafire.com\tUA/1"), "UA2");
        check(L[2].equals("#DL\thttps://download1.mediafire.com/a/b.pkg\tJuego X.pkg\t123456"), "DL: " + L[2]);
        check(L[3].equals("# Netscape HTTP Cookie File"), "cabecera");
        check(L[4].equals(".www.mediafire.com\tTRUE\t/\tFALSE\t0\tcf_clearance\tABC123"), "cookie1: " + L[4]);
        check(L[5].equals(".www.mediafire.com\tTRUE\t/\tFALSE\t0\tsession\tx=y"), "cookie2 (valor con '='): " + L[5]);
        check(L.length == 6, "sin lineas de mas: " + L.length);

        System.out.println("[5] servidor HTTP en bucle local");
        final int[] calls = {0};
        HttpServerLite srv = new HttpServerLite(0, new HttpServerLite.Handler() {
            public HttpServerLite.Resp handle(String ip, String path, Map<String, String> q) {
                calls[0]++;
                if (!StateBuilder.clientAllowed(ip, null)) return new HttpServerLite.Resp(403, "no permitido");
                if (path.equals("/ping")) return new HttpServerLite.Resp(200, "psb-solver 2");
                if (path.equals("/open")) return new HttpServerLite.Resp(200, "url=" + q.get("url") + " auto=" + q.get("auto"));
                if (path.equals("/boom")) throw new IllegalStateException("x");
                return new HttpServerLite.Resp(404, "desconocido");
            }
        });
        srv.start();
        int port = srv.localPort();
        check(get(port, "/ping").equals("200|psb-solver 2"), "ping");
        check(get(port, "/open?auto=1&url=" + URLEncoder.encode("https://a.b/c?d=1&e=2#f", "UTF-8")).equals("200|url=https://a.b/c?d=1&e=2#f auto=1"), "open con url codificada");
        check(get(port, "/nada").startsWith("404|"), "404");
        check(get(port, "/boom").startsWith("500|error:"), "excepcion del handler -> 500");
        // peticion mal formada y POST
        Socket s = new Socket("127.0.0.1", port);
        s.getOutputStream().write("POST /ping HTTP/1.1\r\n\r\n".getBytes("ISO-8859-1"));
        BufferedReader br = new BufferedReader(new InputStreamReader(s.getInputStream()));
        check(br.readLine().startsWith("HTTP/1.1 405"), "POST -> 405");
        s.close();
        Socket s2 = new Socket("127.0.0.1", port);
        s2.getOutputStream().write("basura\r\n\r\n".getBytes("ISO-8859-1"));
        check(new BufferedReader(new InputStreamReader(s2.getInputStream())).readLine().startsWith("HTTP/1.1 400"), "basura -> 400");
        s2.close();
        // varias peticiones a la vez
        Thread[] ts = new Thread[12]; final boolean[] ok = new boolean[12];
        for (int i = 0; i < ts.length; i++) { final int k = i; ts[i] = new Thread(new Runnable() { public void run() { try { ok[k] = get(port0(srv0), "/ping").equals("200|psb-solver 2"); } catch (Exception e) { ok[k] = false; } } }); }
        srv0 = srv; for (Thread t : ts) t.start(); for (Thread t : ts) t.join();
        boolean all = true; for (boolean b : ok) all &= b; check(all, "12 peticiones simultaneas");
        srv.stop();
        System.out.println(fails == 0 ? "\nTODO OK" : "\n" + fails + " FALLOS");
        System.exit(fails == 0 ? 0 : 1);
    }
    static HttpServerLite srv0;
    static int port0(HttpServerLite s) { return s.localPort(); }
}
