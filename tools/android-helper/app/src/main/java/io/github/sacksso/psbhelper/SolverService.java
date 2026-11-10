package io.github.sacksso.psbhelper;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/** Servicio en primer plano: mantiene vivo el servidor HTTP (y el Wi-Fi) aunque apagues la pantalla o cambies de app. */
public class SolverService extends Service {
    private static final String CH = "psb_helper";
    private HttpServerLite server;
    private PowerManager.WakeLock wl;
    private WifiManager.WifiLock wfl;

    @Override public IBinder onBind(Intent i) { return null; }

    @Override public void onCreate() {
        super.onCreate();
        Helper.appCtx = getApplicationContext();
        Helper.service = this;
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        nm.createNotificationChannel(new NotificationChannel(CH, "PSB Ayudante", NotificationManager.IMPORTANCE_LOW));
        startForeground(1, build("Servidor activo en el puerto " + Helper.PORT, null));
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            wl = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "psbhelper:server");
            wl.setReferenceCounted(false);
            wl.acquire();
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            wfl = wm.createWifiLock(Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY : WifiManager.WIFI_MODE_FULL_HIGH_PERF, "psbhelper:wifi");
            wfl.setReferenceCounted(false);
            wfl.acquire();
        } catch (Exception ignored) { /* sin bloqueos: el servidor sigue funcionando, Android podria dormir el Wi-Fi */ }
        server = new HttpServerLite(Helper.PORT, new HttpServerLite.Handler() {
            @Override public HttpServerLite.Resp handle(String ip, String path, java.util.Map<String, String> q) { return Helper.handle(ip, path, q); }
        });
        try {
            server.start();
            Helper.serverError = "";
        } catch (Exception e) {
            Helper.serverError = "no se pudo abrir el puerto " + Helper.PORT + " (" + e.getMessage() + ")";
        }
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) { return START_STICKY; }

    @Override public void onDestroy() {
        if (server != null) server.stop();
        try { if (wl != null && wl.isHeld()) wl.release(); } catch (Exception ignored) {}
        try { if (wfl != null && wfl.isHeld()) wfl.release(); } catch (Exception ignored) {}
        if (Helper.service == this) Helper.service = null;
        super.onDestroy();
    }

    private Notification build(String text, PendingIntent tap) {
        Notification.Builder b = new Notification.Builder(this, CH)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle("PSB Ayudante")
                .setContentText(text)
                .setOngoing(true);
        if (tap != null) b.setContentIntent(tap);
        return b.build();
    }

    /** La PS4 pidio abrir una pagina y la app no esta a la vista: notificacion para abrirla con un toque. */
    void notifyOpen(String url) {
        Intent i = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pi = PendingIntent.getActivity(this, 0, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE);
        NotificationManager nm = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        Notification n = new Notification.Builder(this, CH)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentTitle("La PS4 pide una pagina")
                .setContentText("Toca para abrirla y resolver el captcha")
                .setAutoCancel(true)
                .setContentIntent(pi)
                .build();
        nm.notify(2, n);
    }
}
