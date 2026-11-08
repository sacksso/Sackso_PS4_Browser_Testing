# Qué probar en la PS4 (fase 0) y qué enviar

1. Instala el PKG `IV0000-PSBR00001_00-PSBROWSER0000000.pkg` y ábrelo.
2. Comprueba en pantalla, para cada URL (cualquier botón = siguiente):
   - `http://example.com/` → debería dar `HTTP 200`.
   - `https://example.com/` → HTTPS.
   - `https://info.cern.ch/`, la página de Wikipedia de PS4 (grande, se trunca a 512 KB) y `https://www.wikipedia.org/` (redirecciones).
3. Prueba el desplazamiento (cruceta/stick).
4. Sube `/data/psbrowser/log.txt` (y `last.html` si hace falta). Cosas que me interesan:
   - Líneas `[net] load ... = 0x...` (carga de módulos) y `[net] listo`.
   - Líneas `[tls] verifyErr=0x...` por cada HTTPS (0 = certificado validado por la consola).
   - Cualquier `[http] ... fallo 0x...` con el paso que falló.
   - Salida del mando (`boton N`).
5. Si no hay red: comprueba que la consola tiene internet desde el menú del sistema y que no hay otro homebrew con red abierto.

## Si algo falla

| Síntoma | Probable causa | Siguiente paso |
|--------|----------------|----------------|
| `net_init` falla | Módulos NET/HTTP/SSL no cargan o pools | Mirar los códigos `[net]` del log |
| `createConnection` / `sendRequest` con error | DNS, conectividad o TLS | Mirar `[tls]` y el código 0x8043xxxx |
| HTTPS falla pero HTTP va | Certificados | Probar con el modo permisivo (por defecto) y revisar `verifyErr` |
| Pantalla negra | Render/SDL | Mirar `SDL_Init...` y `sin renderer` en el log |

## Prueba 2 (v0.1.1): diagnosticar `0x80431073`

En la prueba 1 solo falló `https://en.wikipedia.org/wiki/PlayStation_4` (en `sendRequest`, tras aceptar el TLS); `https://www.wikipedia.org/`
funcionó (122 KB). Esta versión:
- reintenta una vez en modo compat (sin keep-alive y con `Accept` / `Accept-Encoding: identity`) y lo anota en el log;
- añade tres URLs: `en.wikipedia.org/wiki/Main_Page`, la versión móvil `en.m.wikipedia.org` y la misma página en texto plano (`action=raw`).

Qué miro en el log: si el reintento (`[http] reintento: OK`) arregla la página, si falla solo la grande o todas las de `en.wikipedia.org`,
y el `verifyErr` en modo estricto (debería seguir en 0).

## Prueba 3 (v0.1.2): hipótesis del tamaño de cabeceras

Resultado de la prueba 2: fallan **todas** las páginas `/wiki/...` de `en.wikipedia.org` y `en.m.wikipedia.org` (también con el reintento
compat); funcionan `www.wikipedia.org` y `en.wikipedia.org/w/index.php?...&action=raw` (173 KB). Con keep-alive, `Accept-Encoding` y TLS ya
descartados, la diferencia más probable son las **cabeceras de la respuesta** (las páginas de artículo traen muchas y grandes).
Es una hipótesis, no está confirmada. La v0.1.2:
- pide a `libSceHttp` un límite de cabeceras de respuesta de 128 KB con `sceHttpSetResponseHeaderMaxSize` y registra su código de retorno;
- registra las cabeceras de respuesta de cada página que funciona (líneas `[hdr]`), para comparar tamaños y contenido;
- añade `https://news.ycombinator.com/` como prueba extra.

Qué miro: la línea `sceHttpSetResponseHeaderMaxSize(...) = 0x...` (0 = aceptada), si `/wiki/PlayStation_4` pasa a dar 200 y, si no, el tamaño
de las cabeceras de las páginas que sí funcionan.

## Prueba 4 (v0.2.0, fase 1a): visor con enlaces y entrada

Esta versión **no se ha probado en la consola**. Cosas a comprobar y a reportar (el log `/data/psbrowser/log.txt` lo registra todo):

1. Arranca y muestra la página de inicio (`psbrowser - inicio`) con enlaces. ¿Se ve bien a 1080p?
2. **Teclado USB:** Tab / Shift+Tab seleccionan enlaces, Enter los abre, F6 abre la barra de URL, escribir y Enter navega, Esc cancela, Backspace vuelve atrás.
   Mira en el log las líneas `tecla: codigo=... char=...` (se guardan las 60 primeras).
3. **Ratón USB:** ¿aparece el cursor? ¿se mueve a velocidad razonable? ¿clic izquierdo abre el enlace bajo el cursor, clic derecho vuelve atrás?
   **Rueda:** la dirección es una suposición (valor positivo = subir). Si va al revés, dímelo (línea `rueda: N` en el log).
4. **Mando:** los índices de botón son una suposición del DS4 (0 abrir, 1 atrás, 2 recargar, 3 inicio, 4/5 página). El log anota `boton N` y `hat N`;
   dime qué botón físico produjo cada número si no coincide.
5. **Búsqueda:** F6, escribir `ps4 homebrew`, Enter (usa `html.duckduckgo.com/html/`, no probado).
6. Páginas largas: Wikipedia, Hacker News. ¿Es fluido el desplazamiento? (El dibujo es por software a 1080p; si va lento, dímelo.)

Limitaciones conocidas de la v0.2.0: sin imágenes, sin JavaScript, sin formularios, sin enlaces internos (`#ancla`), los caracteres no ASCII
salen como `?` salvo vocales acentuadas/ñ, que se convierten a su letra base.

## Prueba 5 (v0.3.0)
1. Abrir `C` (barra) o F10: `about:settings`. Subir la sensibilidad con `[ + ]`/`[ ++ ]` y comprobar que el cursor se mueve más. Cada cambio se guarda en `/data/psbrowser/config.txt`.
2. Probar la rueda; si va al revés, activar "Rueda invertida".
3. Marcar una página con `+` o Ctrl+D; verla en `B` (`about:bookmarks`).
4. Visitar una página con `<video>`/`<iframe>`: debe salir `[video]` o `[video incrustado]` con la URL en la barra de estado (aún no se reproduce).
5. Devolver `log.txt` (buscar líneas `rueda:` y `[refresh]`).

## Prueba v0.6.0: descargas (sin probar en PS4)
Instala el PKG y devuelve `/data/psbrowser/log.txt` (busca lineas `[dl]`, `[http] ... (solo cabeceras)`), el zip `logs` del CI y, si hay, el contenido de `/data/psbrowser/downloads.txt`.
1. **Archivo directo:** abre un enlace a un `.zip`/`.pdf`/`.pkg` pequeno (p. ej. algun release de GitHub). Debe salir "Descargar archivo" con nombre, tipo y tamano. X sobre **[ Descargar ]** (ya viene seleccionado) -> recuadro "Descargando" con barra, velocidad y tiempo restante -> `about:downloads`.
2. **Cancelar:** durante una descarga grande pulsa **O (boton 1)**: debe volver con "Descarga cancelada" y sin dejar archivo `.part`. (Si el servidor no envia datos durante unos segundos, la cancelacion tarda: no se atiende hasta el siguiente trozo.)
3. **Imagen:** abre la URL directa de una imagen PNG/JPEG (o `Imagenes de la ultima pagina` desde `about:downloads` tras visitar una pagina con imagenes): vista previa + Descargar. Con WebP/GIF/SVG no hay vista previa (aun no se decodifican) pero se descargan igual.
4. **Video directo:** un enlace `.mp4`/`.webm`. Descarga (no reproduce). `.m3u8`/`.mpd` muestra el aviso de streaming.
5. **Archivo grande (>100 MB):** comprueba que la barra avanza, que la velocidad es razonable y que la interfaz no se queda "colgada" (se redibuja cada ~300 ms).
6. **Errores:** enlace roto (404), descarga detras de Cloudflare/captcha (403/503 o una pagina HTML en vez del archivo): debe salir "Descarga fallida" con la causa y [ Reintentar ].
7. `about:downloads` (o Ctrl+J con teclado): `[ borrar ]` borra el archivo de verdad.
Cosas que no se pueden saber sin consola: velocidad real, disco lleno, si `rename` funciona en `/data`, si el servidor corta sin `Content-Length`.

## Prueba v0.7.0: menu contextual, portapapeles, atajos, descargas con hilo de disco, ICO (sin probar en PS4)
Instala el PKG y devuelve `/data/psbrowser/log.txt` + el zip `logs` del CI (ahora incluye `quickjs_probe.txt`, la sonda de JavaScript).
1. **Clic derecho (raton):** sobre un enlace -> "Abrir enlace / Abrir en pestana nueva / Copiar direccion del enlace / Descargar enlace" + Atras/Adelante/Recargar... Sobre una imagen -> "Abrir imagen / Copiar direccion de la imagen / Guardar imagen". Sobre una pestana -> duplicar/cerrar/cerrar las demas/reabrir. Sobre la barra de direcciones -> copiar/pegar y buscar. Flechas o raton para elegir; Enter/clic ejecuta; Esc/clic fuera cierra. (**Cambio:** el clic derecho ya no es "atras"; Atras esta en el menu, y los botones laterales del raton hacen atras/adelante si existen.)
2. **Copiar/pegar:** copia un enlace, abre pestana nueva (Ctrl+T) y pega con Ctrl+V en la barra. Ctrl+C sin editar copia el enlace seleccionado (Tab) o la URL de la pagina. El portapapeles es **interno** (la PS4 no da acceso al del sistema); tambien queda en `/data/psbrowser/clipboard.txt`.
3. **Atajos:** Ctrl+R, Ctrl+Mayus+T (reabrir pestana cerrada), Ctrl+1..9, Alt+Inicio, tecla Menu o Mayus+F10 (menu del elemento con foco), Ctrl+clic (pestana nueva), Alt+clic (descargar enlace).
4. **Mando:** el boton **11** abre el menu del elemento con foco (cruceta mueve, X elige, O cierra). **Dime si el boton 11 es R3/L3 o el que sea**: era el unico sin funcion y asi lo elegimos; si molesta (se pulsa sin querer al mover el stick), lo cambio.
5. **Descargas (velocidad):** descarga un archivo grande (p. ej. el .pkg de archive.org). En `log.txt` busca `[dl]`: cada 5 s sale `X MB de Y, N MB/s (media M MB/s), cola de disco K KB` y al final `[dl] red: ... MB/s | disco tras la red: ... ms`.
   - Si "cola de disco" crece hasta ~12 MB y la velocidad es baja -> el cuello es el **disco**.
   - Si la cola esta casi vacia -> el cuello es **red/TLS/servidor** (archive.org limita por conexion; ahi la solucion seria varias conexiones en paralelo).
6. **Favicons .ico:** en el log ya no deben salir `no se pudo decodificar (image/x-icon...)`.
7. **Imagenes perezosas:** en sitios como serviporno.com antes salian `0 imagenes`; ahora se usan `data-src`/`data-original`/`srcset`. Mira la linea `[page] ... N imagenes`.
8. El log ya no se llena de `sceMouseRead devuelve` (antes ~10 000 lineas).

## Prueba v0.8.0: video integrado y boton de salir (sin probar en PS4)
Instala el PKG y manda `/data/psbrowser/log.txt` (las lineas `[vp]` son las importantes), `config.txt` y, si la imagen sale rota, `/data/psbrowser/video_frame0.bin`.
1. **Salir:** boton de apagado de la barra (derecha del todo), Ctrl+Q, `psb:quit` (enlace "Salir" en Inicio/Configuracion) o menu contextual (boton 11 del mando) > "Salir del navegador". Dialogo: X o Intro = salir, O o Esc = cancelar. Anota si la consola vuelve al inicio sin mensaje de error.
2. **MP4 por streaming:** abre un enlace directo a un `.mp4` (H.264/AAC). Sale la pagina de confirmacion con **[ Reproducir sin descargar ]** (ya seleccionado: X). Mira si aparece imagen y sonido, el texto central ("Almacenando en bufer N%") y el log `[vp]`.
3. **Descargar y reproducir:** `[ Descargar y reproducir ]`; despues, en `about:downloads` cada video tiene `[ Reproducir ]`.
4. **Videos de la pagina:** F8 o menu > "Videos de la pagina" (aviso "N videos detectados" al cargar). Prueba en una pagina con `<video>` y en una que guarde el .mp4 dentro de JavaScript.
5. **Controles:** mando X pausa, O cierra, cruceta izq/der -+10 s, arriba/abajo volumen, botones 4/5 -+30 s, 3 informacion, 2 calidad. Teclado: Espacio/K, flechas, J/L (-+30 s), Inicio, H (calidad), I (informacion), Esc/Q cierran. Raton: clic en la barra = saltar, botones, rueda = volumen.
6. Si no hay imagen: Configuracion > Decodificador (sistema / software / software 2) y repite; manda `video_frame0.bin` y las lineas `[vp] PRIMER FOTOGRAMA`. Anota los fps de `I` (el dibujo completo a 1920x1080 limita a ~15 fps; `H` = media resolucion ayuda).

## Prueba v0.8.1: sincronizacion de video, cierre y F8 (sin probar en PS4)
Instala el PKG y manda `/data/psbrowser/log.txt` (lineas `[vp]`, `[perfv]`, `[quit]`, `[video]`) y `config.txt`.
1. **Cámara lenta:** reproduce un clip corto sin audio (una vista previa de F8 vale). Mira en el log `[vp] … fps mostrados | velocidad Nx …`: ~1.0x = bien (con `descartados` > 0 es normal: se ven menos fps pero a ritmo real); ~0.5x = el arreglo no bastó. Tecla **I** (o boton 3) muestra lo mismo en pantalla.
2. **Video con audio:** abre un `.mp4` completo con sonido (p. ej. uno de `about:downloads` con [ Reproducir ]). Anota si hay camara lenta, si el sonido va a ritmo y si la imagen y el audio se desincronizan; en el log, `reloj=audio`.
3. **Cierre:** (a) tras ver un video, sal con el **boton Salir de la app** (apagado de la barra, Ctrl+Q) y comprueba que el log termina con `[vp] cerrado limpio`, `[quit] adios`; (b) otra sesion: sal desde el menu de PS (boton PS > Cerrar aplicacion) **sin haber abierto ningun video**, y (c) tras ver un video. Anota el codigo **CE-xxxxx-x** si la PS4 muestra un error y en cuales de los casos sale.
4. **F8 en un listado:** abre una pagina de listado/playlist y pulsa F8: deberia avisar de que son vistas previas y dejarlas al final; abre la pagina de UN video y pulsa F8: deberia aparecer primero el de mayor calidad.

## Prueba v0.8.2: via rapida de video (sin probar en PS4)
Antes de nada: la **primera linea del log debe decir `== psbrowser v0.8.2 ==`** (el ultimo log recibido era de v0.8.0).
1. Reproduce el mismo `14057500-480p.mp4` (con audio). Con el overlay oculto (espera 3 s sin tocar nada) deberia verse mas fluido; en el log mira `[perfv] RAPIDO … total N ms` (objetivo < 40 ms) y los fps de `[vp]`.
2. Mueve el raton / pulsa un boton: aparece el overlay (via normal, mas lenta); al ocultarse vuelve la rapida. Comprueba que no quedan restos del cursor ni del overlay en las bandas negras.
3. Pausa, +-10 s, volumen y cerrar con O deben seguir funcionando; anota si hay parpadeos al cambiar entre las dos vias.
4. Cierre (Salir de la app y menu de PS) y codigo CE si falla, como en v0.8.1.


## Prueba v0.8.3: benchmark de presentacion y opciones de video (sin probar en PS4)
La primera linea del log debe decir `== psbrowser v0.8.3 ==`.
1. Con el navegador en una pagina normal, pulsa **F9** (o Configuracion > "Medir velocidad de presentacion"). Se bloquea ~20 s y muestra `about:bench`. Devuelve `log.txt` (lineas `[bench]`).
2. Interpretacion: si la prueba 6 con rectangulos pequenos es mucho mas rapida que la 3, la actualizacion parcial sirve.
3. Configuracion > Video: probar "Tamano del video" (ajustar / nativo 1:1 / x2) y "Actualizacion parcial" (solo sin raton conectado). Reproducir un video 20 s con cada combinacion y devolver el log (`[perfv] RAPIDO ... presentacion=`, `[vp] ... fps`).
4. Si el video parpadea o se ve mal con la parcial, desactivarla; si `SDL_UpdateWindowSurfaceRects` falla se desactiva sola (linea `[perfv]`).

### v0.8.3 pulido (sin probar en PS4)
- Icono: debe verse un globo azul en el menu de la PS4 (si sigue el generico, mirar el paso "Compilar" del CI: "icono propio copiado").
- Ctrl+F en una pagina larga: escribir, ver resaltado y contador "n de m"; Intro/F3 avanza; Esc cierra. F3 reabre la ultima busqueda.
- Configuracion > Navegador: cambiar buscador (buscar texto en la barra) y User-Agent (recargar una pagina; probar un sitio de video con Chrome/Smart TV).
- F1 (atajos), Ctrl+H (historial; reiniciar el navegador y comprobar que persiste), pagina que antes salia vacia por `<noscript>`.


## Prueba v0.9.0: presentacion parcial, DOM/CSS/JS, descargas por trozos y medidor (sin probar en PS4)
1. Instalar y mirar la 1.ª linea del log: `== psbrowser v0.9.0 (DOM+CSS+JS) ==` y `[jsx] motor JS: QuickJS` (si dice «no incluido»: el build con JS fallo y cayo a `-DPSB_NO_JS`; devolver `logs/build_js_failed.log`).
2. Configuracion: probar `js` = no / DOM+CSS / DOM+CSS+JS con la misma pagina; comparar. Log: `[jsx] DOM ok, N scripts…`, `[js] ERROR…`, `[css]`.
3. Interfaz: desplazar una pagina larga y mover el cursor. Si hay restos o parpadeo: «Interfaz: repintar solo lo que cambia» = no. Log `[perfui]` si el repintado parcial falla.
4. Video: `vscale=1` y `vpart=1`, con y sin raton (`[perfv]`, `[vp]`).
5. Descargas en WiFi: `about:dlbench` con la ultima descarga (1 conexion primero; probar 2 y 4 solo si no se cuelga). Descargar un archivo grande, cortar el WiFi a mitad y volver a abrir el enlace (o Descargas > Descargas incompletas > continuar). Log: `[dlbench]`, `[dleng]`, `[dl] motor`.
6. Devolver `/data/psbrowser/log.txt`, `config.txt`, `downloads.txt`, `last.html` y el zip `logs` del CI.
