package io.github.sacksso.psbhelper;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.NotificationManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.DownloadListener;
import android.webkit.URLUtil;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.util.List;

/** Pantalla unica: direccion para la PS4 arriba y un WebView debajo (aqui se resuelven los captchas con el dedo). */
public class MainActivity extends Activity {
    private WebView web;
    private TextView ipText, statusText;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable tick = new Runnable() {
        @Override public void run() {
            refreshHeader();
            ui.postDelayed(this, 1500);
        }
    };

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        Helper.appCtx = getApplicationContext();
        SharedPreferences sp = getSharedPreferences("psb", MODE_PRIVATE);
        String allowIp = sp.getString("allow", "");
        Helper.allow.clear();
        if (!allowIp.isEmpty()) Helper.allow.add(allowIp);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(8);
        root.setPadding(pad, pad, pad, 0);

        ipText = new TextView(this);
        ipText.setTextSize(20);
        ipText.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        root.addView(ipText);

        statusText = new TextView(this);
        statusText.setTextSize(12);
        root.addView(statusText);

        LinearLayout row1 = row();
        row1.addView(button("Copiar", new View.OnClickListener() { @Override public void onClick(View v) { copyAddress(); } }));
        row1.addView(button("URL", new View.OnClickListener() { @Override public void onClick(View v) { askUrl(); } }));
        row1.addView(button("Recargar", new View.OnClickListener() { @Override public void onClick(View v) { web.reload(); } }));
        root.addView(row1);
        LinearLayout row2 = row();
        row2.addView(button("Solo esta PS4", new View.OnClickListener() { @Override public void onClick(View v) { askAllow(); } }));
        row2.addView(button("Borrar todo", new View.OnClickListener() { @Override public void onClick(View v) { clearAll(); } }));
        root.addView(row2);

        web = new WebView(this);
        root.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);
        setupWebView();

        Helper.activity = this;
        requestNotifPermission();
        Intent svc = new Intent(this, SolverService.class);
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(svc); else startService(svc);
        consumePending();
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private LinearLayout row() {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        return r;
    }

    private Button button(String text, View.OnClickListener l) {
        Button bt = new Button(this);
        bt.setText(text);
        bt.setAllCaps(false);
        bt.setOnClickListener(l);
        bt.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        return bt;
    }

    private void setupWebView() {
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setSupportMultipleWindows(false);
        s.setMediaPlaybackRequiresUserGesture(true);
        s.setUserAgentString(StateBuilder.cleanUa(s.getUserAgentString()));   // la PS4 usara este mismo User-Agent
        Helper.ua = s.getUserAgentString();
        CookieManager cm = CookieManager.getInstance();
        cm.setAcceptCookie(true);
        cm.setAcceptThirdPartyCookies(web, true);   // necesario para Turnstile / reCAPTCHA
        web.setWebChromeClient(new WebChromeClient());
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                String url = r.getUrl().toString();
                if (!StateBuilder.isHttpUrl(url)) return true;                 // intent:, market:, etc.: no se abren
                if (r.isForMainFrame()) Helper.trackHost(url);
                if (StateBuilder.looksLikeDownloadUrl(url)) {                  // enlace directo a un archivo: se captura, no se descarga en el movil
                    capture(url, "", -1);
                    return true;
                }
                return false;
            }
            @Override public void onPageStarted(WebView v, String url, Bitmap icon) {
                Helper.trackHost(url);
            }
            @Override public void onPageFinished(WebView v, String url) {
                CookieManager.getInstance().flush();
                Helper.trackHost(url);
                Helper.lastMsg = "pagina cargada: " + url;
            }
        });
        web.setDownloadListener(new DownloadListener() {
            @Override public void onDownloadStart(String url, String userAgent, String contentDisposition, String mimetype, long contentLength) {
                capture(url, contentDisposition, contentLength, mimetype);
            }
        });
    }

    private void capture(String url, String cd, long len) { capture(url, cd, len, null); }

    private void capture(String url, String cd, long len, String mime) {
        String name = URLUtil.guessFileName(url, cd, mime);
        Helper.addDownload(url, name, len);
        Toast.makeText(this, "Descarga capturada para la PS4: " + name, Toast.LENGTH_LONG).show();
        refreshHeader();
    }

    /** Lo llama el servidor cuando la PS4 pide /open (hilo del servidor). */
    void openFromHelper(final String url) {
        ui.post(new Runnable() { @Override public void run() { web.loadUrl(url); } });
    }

    private void consumePending() {
        String u = Helper.pendingUrl;
        if (u != null) {
            Helper.pendingUrl = null;
            web.loadUrl(u);
            NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            nm.cancel(2);
        }
    }

    private void refreshHeader() {
        List<String> ips = Helper.lanIps();
        String addr = ips.isEmpty() ? "(sin Wi-Fi)" : ips.get(0) + ":" + Helper.PORT;
        ipText.setText("!solver " + addr);
        statusText.setText(Helper.statusLine());
    }

    private void copyAddress() {
        List<String> ips = Helper.lanIps();
        if (ips.isEmpty()) { Toast.makeText(this, "No hay Wi-Fi", Toast.LENGTH_SHORT).show(); return; }
        ClipboardManager cb = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        cb.setPrimaryClip(ClipData.newPlainText("psb", ips.get(0) + ":" + Helper.PORT));
        Toast.makeText(this, "Copiado: " + ips.get(0) + ":" + Helper.PORT, Toast.LENGTH_SHORT).show();
    }

    private void askUrl() {
        final EditText et = new EditText(this);
        et.setInputType(InputType.TYPE_TEXT_VARIATION_URI);
        et.setHint("https://...");
        new AlertDialog.Builder(this).setTitle("Abrir pagina").setView(et)
                .setPositiveButton("Abrir", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        String u = et.getText().toString().trim();
                        if (!u.startsWith("http")) u = "https://" + u;
                        Helper.trackHost(u);
                        web.loadUrl(u);
                    }
                }).setNegativeButton("Cancelar", null).show();
    }

    private void askAllow() {
        final EditText et = new EditText(this);
        et.setHint("IP de la PS4 (vacio = toda la red local)");
        et.setText(Helper.allow.isEmpty() ? "" : Helper.allow.iterator().next());
        new AlertDialog.Builder(this).setTitle("Aceptar solo esta IP").setView(et)
                .setPositiveButton("Guardar", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        String ip = et.getText().toString().trim();
                        Helper.allow.clear();
                        if (!ip.isEmpty()) Helper.allow.add(ip);
                        getSharedPreferences("psb", MODE_PRIVATE).edit().putString("allow", ip).apply();
                        Toast.makeText(MainActivity.this, ip.isEmpty() ? "Cualquier equipo de la red local" : "Solo " + ip, Toast.LENGTH_SHORT).show();
                    }
                }).setNegativeButton("Cancelar", null).show();
    }

    private void clearAll() {
        CookieManager.getInstance().removeAllCookies(null);
        CookieManager.getInstance().flush();
        web.clearCache(true);
        Helper.hosts.clear();
        Helper.dls.clear();
        Helper.lastMsg = "cookies, sitios y descargas borrados";
        refreshHeader();
    }

    private void requestNotifPermission() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != android.content.pm.PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"}, 1);
    }

    @Override protected void onNewIntent(Intent i) {
        super.onNewIntent(i);
        consumePending();
    }

    @Override protected void onResume() {
        super.onResume();
        Helper.resumed = true;
        consumePending();
        refreshHeader();
        ui.post(tick);
    }

    @Override protected void onPause() {
        Helper.resumed = false;
        ui.removeCallbacks(tick);
        CookieManager.getInstance().flush();
        super.onPause();
    }

    @Override protected void onDestroy() {
        if (Helper.activity == this) Helper.activity = null;
        ui.removeCallbacksAndMessages(null);
        if (web != null) { web.destroy(); web = null; }
        super.onDestroy();
    }

    @Override public void onBackPressed() {
        if (web != null && web.canGoBack()) web.goBack(); else super.onBackPressed();
    }
}
