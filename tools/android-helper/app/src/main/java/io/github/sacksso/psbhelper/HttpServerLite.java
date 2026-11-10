package io.github.sacksso.psbhelper;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Servidor HTTP minimo (solo GET, texto plano). Sin dependencias: se prueba en el PC. */
final class HttpServerLite {
    interface Handler { Resp handle(String clientIp, String path, Map<String, String> query); }

    static final class Resp {
        final int code; final String body;
        Resp(int code, String body) { this.code = code; this.body = body; }
    }

    private final int port;
    private final Handler handler;
    private ServerSocket ss;
    private volatile boolean running;
    private ExecutorService pool;

    HttpServerLite(int port, Handler handler) { this.port = port; this.handler = handler; }

    synchronized void start() throws IOException {
        if (running) return;
        ss = new ServerSocket();
        ss.setReuseAddress(true);
        ss.bind(new InetSocketAddress(port));
        running = true;
        pool = Executors.newFixedThreadPool(6);
        Thread t = new Thread(new Runnable() { public void run() { acceptLoop(); } }, "psb-accept");
        t.setDaemon(true);
        t.start();
    }

    int localPort() { return ss == null ? -1 : ss.getLocalPort(); }

    synchronized void stop() {
        running = false;
        try { if (ss != null) ss.close(); } catch (IOException ignored) {}
        if (pool != null) pool.shutdownNow();
    }

    private void acceptLoop() {
        while (running) {
            try {
                final Socket s = ss.accept();
                pool.execute(new Runnable() { public void run() { serve(s); } });
            } catch (IOException e) {
                if (!running) return;
            } catch (RuntimeException e) {
                return;   // pool cerrado
            }
        }
    }

    private static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        int c;
        while ((c = in.read()) >= 0) {
            if (c == '\n') break;
            if (c != '\r') b.write(c);
            if (b.size() > 8192) return null;
        }
        if (c < 0 && b.size() == 0) return null;
        return new String(b.toByteArray(), StandardCharsets.ISO_8859_1);
    }

    private static void write(OutputStream out, int code, String body) throws IOException {
        byte[] data = body.getBytes(StandardCharsets.UTF_8);
        String reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Error";
        String head = "HTTP/1.1 " + code + " " + reason + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + data.length + "\r\nConnection: close\r\n\r\n";
        out.write(head.getBytes(StandardCharsets.ISO_8859_1));
        out.write(data);
        out.flush();
    }

    private void serve(Socket s) {
        try {
            s.setSoTimeout(8000);
            InputStream in = s.getInputStream();
            OutputStream out = s.getOutputStream();
            String first = readLine(in);
            if (first == null) return;
            String line; int n = 0;
            while ((line = readLine(in)) != null && !line.isEmpty() && n++ < 100) { /* cabeceras: se ignoran */ }
            String[] p = first.split(" ");
            if (p.length < 2) { write(out, 400, "peticion no valida"); return; }
            if (!p[0].equals("GET")) { write(out, 405, "solo GET"); return; }
            String target = p[1];
            int qi = target.indexOf('?');
            String path = qi < 0 ? target : target.substring(0, qi);
            String query = qi < 0 ? "" : target.substring(qi + 1);
            String ip = ((InetSocketAddress) s.getRemoteSocketAddress()).getAddress().getHostAddress();
            Resp r;
            try { r = handler.handle(ip, path, StateBuilder.parseQuery(query)); }
            catch (Throwable t) { r = new Resp(500, "error: " + t); }
            write(out, r.code, r.body);
        } catch (IOException ignored) {
        } finally {
            try { s.close(); } catch (IOException ignored) {}
        }
    }
}
