# PSB Ayudante (Android)

Hace en el **movil** lo que `tools/solver/psb_solver.py` hace en el PC: abres la pagina de descarga, **resuelves tu mismo el captcha**, y la PS4 recibe las cookies, el User-Agent y las descargas capturadas. Mismo protocolo (`psb-solver 2`): en la PS4 no cambia nada (`!solver IP:8765`).

## Instalar
1. GitHub -> Actions -> "Build Android helper (APK)" -> Run workflow (o sube un cambio en `tools/android-helper/`).
2. Descarga el artefacto `psb-helper-apk`, instala `app-debug.apk` (permite "origenes desconocidos").
3. Abre la app: arriba sale `!solver 192.168.x.x:8765`. Escribelo en la barra de la PS4 (una vez). Movil y PS4 en el **mismo Wi-Fi**.
4. En la app: boton "Solo esta PS4" y pon la IP de la PS4 (recomendado). Sin eso acepta cualquier equipo de tu red local (nunca Internet).

## Uso
- En la PS4, en la pagina que pide verificacion: F7 -> "Abrir esta pagina en el PC" (ahora la abre en el movil; si la app no esta a la vista, toca la notificacion).
- Resuelve el captcha y pulsa tu mismo el boton de descarga en el movil. La app **no** descarga nada: captura el enlace y se lo deja a la PS4 ("Descarga capturada para la PS4").
- En la PS4: "Traer cookies y descargas del PC" y despues reintenta/descarga.
- Sin `auto=1`: en el movil no hay clics automaticos, los das tu.

## Limites (sin verificar en un movil real)
- `CookieManager` solo da `nombre=valor`: se pierden caducidad y banderas (HttpOnly/Secure). Se entregan como cookies de sesion de `.host`.
- Solo se entregan cookies de los sitios que visitaste en la app (y de los enlaces capturados).
- Las verificaciones de Cloudflare suelen atar `cf_clearance` a **IP + User-Agent**: la app quita `; wv` y `Version/4.0` del User-Agent del WebView y se lo manda a la PS4. Si una verificacion no pasa en el WebView, plan B con root: leer las cookies de Chrome (no implementado).
- `/media` (yt-dlp) no existe aqui: usa el ayudante del PC.
- Android puede cerrar la app: va como servicio en primer plano con bloqueo de Wi-Fi; en algunas ROM hay que quitarle la optimizacion de bateria.

## Pruebas en el PC
```
javac -d /tmp/t app/src/main/java/io/github/sacksso/psbhelper/StateBuilder.java app/src/main/java/io/github/sacksso/psbhelper/HttpServerLite.java tests/StateBuilderTest.java
java -cp /tmp/t io.github.sacksso.psbhelper.StateBuilderTest
```
