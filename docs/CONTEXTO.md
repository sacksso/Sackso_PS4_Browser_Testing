# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva junto con el zip del repo (y, si los tienes, el último `log.txt` y el zip `logs` del CI). Idioma de trabajo: **español**.
> Última actualización: 7 de noviembre de 2026 (**v0.8.0 = código completo: vídeo integrado + botón Salir + buscador de vídeos, SIN PROBAR en consola y sin compilar con el toolchain real en la última sesión**; v0.7.0 sigue sin probar en consola; v0.6.0 probada: descargas OK). **JavaScript: aún no existe** (`docs/JS.md`).


---
# >>> ESTADO v0.8.0 (VIDEO + SALIR) - CODIGO COMPLETO, SIN PROBAR - leer primero <<<
> Actualizado el 7 de noviembre de 2026. El usuario pidio **"focus total" en reproducir video dentro del navegador** (streaming sin descargar, o descargando y viendolo desde el propio navegador) **y un boton de salir** con mando, raton y teclado. v0.7.0 tampoco se ha probado en consola.

## A. Que hay (todo integrado en `app/main.cpp`, ~2480 lineas)
1. **Reproductor** (`vplayer.*` sobre libSceAvPlayer + libSceAudioOut, `vsource.*` fuente por rangos HTTP con cache de bloques y lectura anticipada; ver §A-bis). **Interfaz** (`drawVideoUi`): pantalla completa negra con letterbox, titulo arriba, barra inferior (progreso clicable, `mm:ss / mm:ss`, botones -10 s / Pausa-Seguir / +10 s / Vol- / Vol+ / Cerrar, volumen), texto central de estado ("Almacenando en bufer N%", errores en rojo, "Sin imagen..." si pasan 8 s sin fotogramas), panel de estadisticas (I / boton 3), overlay que se oculta a los 3 s de inactividad. El bucle llama `vp_update()` y marca redibujo con fotograma nuevo, cambio de estado o reloj (250 ms). Mientras hay video: se pausa el hilo de imagenes y no se cargan paginas.
2. **Controles de video:** teclado Espacio/K pausa, izq/der -+10 s, J/L -+30 s, arriba/abajo volumen, Inicio al principio, H calidad (mitad/completa), I informacion, Esc/Backspace/Q cierran; raton: clic en barra = saltar, botones, clic en el video = pausa, rueda = volumen, mover = mostrar controles; mando: 0 pausa, 1 cierra, 2 calidad, 3 informacion, 4/5 -+30 s, cruceta izq/der -+10 s y arriba/abajo volumen. Todo captura la entrada (como `g_ctx`/`g_osk`).
3. **Puntos de entrada:** (a) pagina de confirmacion de descarga: si es video (`video/*` o `.mp4/.m4v/.mov/.webm/.mkv`, no `.m3u8`) aparecen **[ Reproducir sin descargar ]** (`psb:play:<url>`, ya seleccionado) y **[ Descargar y reproducir ]** (`psb:dlplay:<url>`), con aviso de que WebM/MKV probablemente falle; (b) `about:downloads`: `[ Reproducir ]` (`psb:playfile:N`) en cada video; (c) **`about:videos` (F8)**: `scanVideos()` busca a mano `.mp4 .m3u8 .webm .mov .m4v` en el HTML CRUDO (incluye URLs dentro de JavaScript/JSON), des-escapa `\/`, `\u0026`, `&amp;`, resuelve con `urlResolve`, deduplica (max 60) y avisa con un toast al cargar la pagina; (d) menu contextual: "Reproducir video" sobre enlaces con pinta de video, "Videos de la pagina (F8)". El Referer del video es la pagina desde la que se llego (`g_navReferer`/`g_pendRef`) o `g_lastVidsPage`.
4. **Salir:** boton de apagado en la barra (`UI_POWER`, derecha del todo; el omnibox se estrecho 46 px), **Ctrl+Q**, enlace `psb:quit` (Inicio y Configuracion) y "Salir del navegador" en el menu contextual (el mando lo abre con el boton 11). Dialogo modal: **X o Intro/Y = salir, O o Esc/N = cancelar**, o clic en los botones. `hardExit()`: log, `vp_close()` si hay video, `imgw_pause(true)` y **`_exit(0)`**; un hilo vigilante fuerza `_exit(0)` a los 2,5 s si algo se atasca. No se usa `SDL_Quit` ni `net_shutdown` a proposito (riesgo de bloqueo con hilos vivos). **No verificado en consola**: puede que la PS4 muestre un aviso al salir asi; alternativa por probar: `sceSystemServiceLoadExec("exit")`.
5. **Configuracion** (`config.txt`: `vdec=0|2|3`, `vhalf=1`, `volume=80`; enlaces `psb:vdec`, `psb:vhalf`, `psb:vol-/+` en `about:settings`, seccion "Video").
6. CI: el `sed` de LIBS anade `-lSceAvPlayer -lSceAudioOut -lSceUserService`. `tools/build_local.sh` replica el CI con el toolchain real (`OO_PS4_TOOLCHAIN=... bash tools/build_local.sh`).

## A-bis. Notas del reproductor (de la sesion anterior)
`vplayer.cpp` (nunca ejecutado en consola) declara dentro las firmas/structs reales de AvPlayer (la cabecera del toolchain solo trae `void f();`; tomados de shadPS4, con `static_assert` de tamanos). Flujo: `VSource::open` -> `sceSysmoduleLoadModule(0xA5)` -> `sceUserServiceInitialize` + `sceAudioOutInit/Open(48000, S16 estereo)` -> `sceAvPlayerInit` (memoria directa WB_ONION para texturas, con fallback a malloc) -> opcional `sceAvPlayerPostInit` (decodificador software) -> `AddSource("psbrowser-video.mp4")` -> espera streams (hasta 40 s) -> PLAYING + hilo de audio. Todo se registra con prefijo **`[vp]`** y el primer fotograma se vuelca a `/data/psbrowser/video_frame0.bin`. `netclient`: `HttpExtra{rangeStart,rangeEnd,referer}`, `http_range()`; el mutex global serializa descargas, paginas, imagenes y video.

## B. Verificacion hecha en la ultima sesion (SIN red: no se pudo bajar el toolchain)
- `main.cpp` compila a objeto con `g++ -std=c++17 -c -O2 -Wall -Wextra` contra `tools/stubs` **sin avisos**; `tests/test_vsource` OK (ASan/UBSan). **No** se compilo ni enlazo con el toolchain real de OpenOrbis (el de la sesion anterior solo cubria `vplayer/vsource/netclient`, antes de integrar `main.cpp`): **el CI es la comprobacion**. Si falla, pasar `build.log`. Puntos con riesgo de compilacion: `std::thread` + `_exit` (`<unistd.h>` y `<thread>` incluidos), `snprintf` con `%lu` para `frames`.
- `scanVideos()` se probo en PC (`sh tools/tests/run_scanvideos.sh`, ASan/UBSan): se corrigieron 3 defectos (descartaba URLs de <8 caracteres como `/a.mp4`; cortaba en el `;` de `&amp;`; ignoraba `src="video.mp4"` sin barra aunque estuviera entre comillas). Pagina de 2 MB sin videos: ~6 ms.
- La interfaz de video, el dialogo de salida y las entradas **no se han ejecutado** (ni en PC ni en consola).

## C. Riesgos e incognitas del video (decirlo SIEMPRE al usuario; nada probado)
- Que `libSceAvPlayer` funcione desde un homebrew OpenOrbis con GoldHEN (modulo 0xA5, hilos internos, memoria). Si `sceAvPlayerInit` da NULL o la app se cierra: mirar el ultimo `[vp]` del `log.txt`.
- **Formato de fotogramas**: se asume NV12 lineal con el `pitch` de `GetVideoDataEx`; podria venir en tiles de GPU o con alto alineado. Si la imagen sale rota: pedir `video_frame0.bin` + lineas `[vp] PRIMER FOTOGRAMA` y deducir el formato.
- Que el decodificador por hardware escriba en memoria directa (`[vp] textura:`) y si hace falta `vdec=2/3` (parametros de PostInit adivinados).
- **Rendimiento**: conversion en CPU + presentar 8 MB por fotograma; la presentacion completa (~55-60 ms medidos en 0.5.0) limita a **~15 fps** aunque el video sea de 30. `vhalf=1` ayuda al dibujo pero no a la presentacion; mejora posible: renderizar el video a 1280x720 y escalar con SDL, o rectangulos sucios.
- Streaming: depende de `Range` en el CDN y de que no exija cookies/UA; las URL con token `secure=...` caducan. HLS (`.m3u8`) no se soporta. WebM/VP9 probablemente no.
- Si el AvPlayer fuera inviable: `libSceVideodec` + demuxer MP4 propio, o servidor intermedio que transcodifique (camino C de `docs/JS.md`).

## D. Que pedir al usuario tras instalar el PKG
`log.txt` (lineas `[vp]`, `[video]`, `[quit]`, `[dl]`), `config.txt`, `video_frame0.bin` si hay imagen rota, y el zip `logs` del CI (`quickjs_probe.txt`). Guion en `docs/PRUEBAS.md` («Prueba v0.8.0»).
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party .github README.md
git commit -m "v0.8.0: video integrado (AvPlayer + streaming por rangos), boton Salir, buscador de videos"
git push
```
---

## 1. Qué es
Navegador homebrew para **PS4 con GoldHEN**, hecho con el toolchain **OpenOrbis** (LLVM, `x86_64-pc-freebsd12-elf`).
No usa WebKit ni JavaScript: es un visor propio de HTML estático (texto, enlaces, imágenes, formularios) con interfaz de "navegador de escritorio" (pestañas, barra, tema claro/oscuro, scroll suave, teclado en pantalla para el mando).
Objetivo a largo plazo: más compatibilidad, vídeo; plan B/C: portar NetSurf (`docs/NETSURF.md`) o un servidor intermedio para sitios con JavaScript. Proyecto personal/aprendizaje.

## 2. Cómo trabajamos (reglas del usuario)
- La IA escribe código y workflows; el usuario sube a GitHub, ejecuta el CI (GitHub Actions, workflow manual `build-pkg.yml`), instala el PKG en su PS4 real y devuelve `/data/psbrowser/log.txt` (a veces `last.html`, `config.txt`, `bookmarks.txt` y el zip `logs` del CI).
- **En cada avance que entregue archivos, la IA entrega también este `docs/CONTEXTO.md` actualizado** (como `CONTEXTO.md` suelto además de dentro del zip), para que otro chat u otra IA pueda continuar.
- Si cambia un workflow, dar los comandos `git` exactos. No añadir al commit carpetas de desarrollo sin seguimiento.
- Ser honesto sobre lo que **no** se ha probado en consola. Pedir el log antes de cambiar mapeos de entrada.
- Repo del usuario: `Sackso_PS4_Browser_Testing` (GitHub). Proyecto anterior reutilizado: `sacksso/Sackso_Ren.py_ps4-testing`.
- Consejos prácticos para la IA:
  - Los archivos de chat/log que sube el usuario pueden venir en **UTF-16** (BOM `ff fe`): decodificar con `utf-16`, no con `cat`.
  - Si el entorno tiene red: descargar el toolchain (`https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz`, 158 MB), `apt install clang lld` y **compilar de verdad** replicando el CI (ver §8). Si no hay red (lo normal hasta ahora), decirlo: se comprueba solo con **stubs** de SDL/orbis (`g++ -fsyntax-only -I<stubs>`) y pruebas de lógica en PC; el CI es la comprobación real. Desde v0.6.0 los stubs mínimos están en el repo: `tools/stubs/` (SDL2, orbis, keyboard, stb_image) y las pruebas en `tools/tests/` (ver `tools/README.md`): `-fsyntax-only` de `main.cpp`, tests de `download.cpp` y de `netclient.cpp` contra un `sceHttp` simulado.
  - `/bin/sh` es dash (sin brace expansion).
  - El workspace de la IA es efímero (`/tmp` se pierde al cerrar el chat): todo lo importante va al zip que se entrega.

## 3. Estado por versiones
| Versión | Contenido | Estado |
|---|---|---|
| 0.1.2–0.3.0 | Red HTTP/HTTPS con `sceHttp`, visor, enlaces, marcadores, ajustes | Probadas |
| 0.4.0 | Rediseño de escritorio (TTF, tema, pestañas, scroll suave, imágenes, formularios GET) | Se colgó en Wikipedia (GIF/hilo) |
| 0.4.1 | Arreglo del cuelgue (GIF desactivado, pausa/cancelación del hilo de imágenes) | Probada: bien, pero ~10 fps |
| **0.5.0** | 0.4.1 + **`-O2` en el CI** + **teclado en pantalla** con el mando (`osk.*`) | **Probada en PS4: "el teclado funciona excelente, el rendimiento se siente excelente"** |
| 0.5.1 | 0.5.0 + gzip/deflate, charsets ISO-8859-1/Windows-1252, POST real, cabeceras Accept*, inventario del toolchain en el CI | **Probada en PS4:** compila y arranca; POST funciona; el sistema ya descomprime gzip |
| 0.5.2 | 0.5.1 + reintento con espera ante HTTP 429/503 en imágenes (Wikimedia) y ritmo suave con `wikimedia.org` | **Probada en PS4** (log de 2692 líneas, ver abajo). El 429 no se pudo re-probar: ese log no visitó Wikipedia |
| 0.6.0 | 0.5.2 + **descargas** (archivos, imágenes y vídeos directos) en primer plano con progreso y cancelación, `about:downloads`, `about:images`, Ctrl+J | **Probada en PS4:** descarga un .mp4 de 17,9 MB completo; un .pkg de 1,31 GB se empezó y se canceló con ○ (cancelación OK). Velocidad no medida (el log no la registraba) |
| **0.7.0** | 0.6.0 + **menú contextual** (clic derecho), portapapeles interno, atajos, **escritura a disco en hilo aparte** + log de velocidad `[dl]`, **decodificador .ico**, imágenes perezosas (`data-src`…), log del ratón sin spam, **sonda de QuickJS en el CI** | **Sin probar en PS4.** Compila con stubs; `AsyncWriter` e `icodec` pasan pruebas en PC (ASan/UBSan). Es el siguiente PKG a instalar |
| **0.8.0** | 0.7.0 + **reproductor de video** (libSceAvPlayer, streaming por rangos o archivo descargado), **boton/dialogo Salir**, `about:videos` (F8), ajustes de video | **Sin probar en PS4** ni compilada con el toolchain real en la ultima sesion (solo stubs). Es el siguiente PKG a instalar |

## 4. Resultados de las pruebas en PS4 real
### Prueba v0.5.0 (log de 1092 líneas)
- Páginas: DuckDuckGo HTML (`html.duckduckgo.com/html/?q=hola`), hola.com (554 KB, 504 líneas, 267 enlaces, 60 imágenes, alto 57 306 px), una noticia de hola.com, assets de realmadrid.com. Sin cuelgues.
- **Rendimiento tras `-O2`:** dibujo **6–7 ms** en pantallas simples y **25–26 ms** en páginas con muchas imágenes (antes ~45–50 ms). Subida+presentación (`SDL_UpdateTexture` + `RenderPresent`) sigue en **50–64 ms** → ~15 fps. Es hoy el único cuello de botella; al usuario le parece excelente. Mejora posible: rectángulos sucios / capa en caché / renderizar a 1280x720.
- **Teclado en pantalla:** funciona (reportado por el usuario).
- **Mando (DualShock 4 vía SDL):** 0 = ✕, 1 = ○, 13/14/15/16 = cruceta (con `hat`), usados también 2, 3, 4, 5 (confirmados como existentes, función exacta según el mapeo del código: 2 recargar, 3 inicio, 4/5 página arriba/abajo, y dentro del teclado 2 espacio, 3 mayúsculas, 4/5 cursor). Aparecen además los botones **18 y 19** (sin identificar: ¿touchpad/PS?). **Los botones 8 y 9 (abrir barra de direcciones y Enter del teclado) no aparecen en el log**: no se sabe si el DS4 los reporta con esos índices. Pedir al usuario que pulse Share/Options y mire `boton N`.
- **SVG falla** (`image/svg+xml`, muy frecuente: logos, iconos). Los `.ico` se piden (favicons) y probablemente no se decodifican.
- Ratón USB: `conectado=0` (no había ratón).
- `config.txt`: `mouseSens=3.50 mouseAccel=1 wheelInvert=0 wheelStep=3 home=`. Marcador: Kintsugi (es.wikipedia).
- Toolchain del CI: clang/lld 18.1.3; **v0.4.1 compilaba sin ninguna optimización** (solo `-DPSB_STRICT_TLS`); ahora `EXTRAFLAGS='-O2 -DPSB_STRICT_TLS'`.

### Prueba v0.5.1 (log de 2539 líneas)
- **POST real funciona:** `[form] POST https://html.duckduckgo.com/html/` → HTTP 200 y página normal.
- **gzip:** con `Accept-Encoding: gzip, deflate`, el servidor responde `Content-Encoding: gzip` pero el cuerpo **ya llega descomprimido**: `sceHttp` descomprime solo. `inflate.cpp` queda como red de seguridad (solo actúa si el cuerpo trae la marca `1f 8b`). Es decir: se ahorra ancho de banda sin trabajo propio.
- **HTTP 429 de upload.wikimedia.org** en 8 imágenes de en.wikipedia.org/wiki/Real_Madrid_CF (peticiones seguidas desde el hilo de imágenes). Arreglo en 0.5.2: reintentar hasta 2 veces con espera (1,5 s y 3,5 s) y 250 ms entre imágenes de wikimedia. (No se parsea `Retry-After`.)
- **Páginas probadas:** en/es.wikipedia (hasta 1,7 MB, 3004 enlaces), DuckDuckGo, xda-developers.com (965 KB, 378 líneas, 59 imágenes; la categoría también), **platinmods.com** (XenForo detrás de Cloudflare: carga, 469 líneas, 25 imágenes) y `/whats-new/posts/`. Cargan el contenido pero **sin el diseño real** (no hay CSS).
- **xdaforums.com/** devolvió 200 con 119 511 bytes pero **0 líneas, 0 enlaces, alto 24 px** (página en blanco). Causa sin diagnosticar (¿cascarón de JavaScript, contenido en `<noscript>`/`<template>`, o el parser descarta todo?). Pendiente: pedir `last.html` justo después de abrir esa URL. `last_1.html` que subió el usuario era Hacker News, no XDA.
- **Reddit no se probó directamente.** Búsqueda web (nov 2026): Reddit exige iniciar sesión para `old.reddit.com` desde julio de 2026, lo limita a cuentas activas en los últimos 6 meses, y a finales de octubre anunció migrar a una experiencia "principalmente con sesión" y retirar RSS/API pública; la portada logged-out seguirá abierta, el resto de reddit.com sin sesión es JS pesado. Conclusión: Reddit solo es viable con navegador real (ver §7, fidelidad visual).
- **Toolchain (inventario del CI):** existen `libSceAvPlayer.so`, `libSceAvPlayerStreaming.so`, `libSceVideodec*.so`, `libSceVdec*.so`, `libSceAudioOut.so`, `libSceImeDialog.so`, y las cabeceras `orbis/AvPlayer.h`, `AvPlayerStreaming.h`, `Videodec.h`, `Videodec2.h`, `AudioOut.h`, `ImeDialog.h`, `Ime.h`. Samples: `SDL2, audio-wav, dialogs, font, graphics, input, keyboard, net_http, networking, pngdec, system, threading…` (ningún sample de vídeo). `Http.h` confirma `sceHttpCreateRequestWithURL(int32_t connId, int32_t method, const char* url, uint64_t contentLength)` y `sceHttpSendRequest(int32_t, const void*, size_t)` (el POST con `decltype` compiló).
- **Botones del mando:** aparece el **11** (41 veces, siempre pegado a otra pulsación; sin función asignada, inofensivo) y el 19. Los botones 8/9 siguen sin verse en ningún log: la barra de direcciones por mando no se ha podido confirmar (el usuario navega con el buscador de DuckDuckGo y marcadores).

### Prueba v0.5.2 (log de 2692 líneas)
- Páginas OK (sin cuelgues): DuckDuckGo, realmadrid.com (1,8 MB, 830 líneas, 60 imágenes), wallpapers.com, gettyimages.com(.mx), wallpaperaccess.com.
- **Ningún HTTP 429** en este log (no se visitó Wikipedia): el reintento de 0.5.2 sigue sin comprobarse.
- **Imágenes que no se decodifican (102 en total):** **67 WebP**, 15 SVG, 17 ICO (`image/x-icon`, `image/vnd.microsoft.icon`: favicons que DuckDuckGo pide), 3 GIF. **WebP es hoy el formato que más falla** en sitios de imágenes: prioridad alta tras las descargas.
- **Reddit (`https://www.reddit.com/`)**: 200 con 8406 bytes, **0 líneas** (cascarón de JavaScript con CSP por `nonce`, `Retry-After: 0`, `Cache-Control: no-store`). Confirma que Reddit solo es viable con navegador real/servidor intermedio.
- `last.html` entregado = Hacker News (no sirve para diagnosticar XDA; `xdaforums.com` sigue con 0 líneas sin diagnosticar).
- **Botones del mando vistos:** 0 (✕), 1 (○), 2, 3, 11, 13–16 (cruceta), 17 (nuevo, sin identificar) y **9 (2 veces: ya aparece Options)**. El 8 sigue sin verse. Los botones 18/19 no salieron esta vez.

### Prueba v0.6.0 (log de 12 747 líneas, 626 KB)
- **Descargas:** `[dl] ok` con `b1b70bfd….mp4` (17 956 307 bytes, serviporno CDN, URL con token `secure=` temporal): completo y registrado en `downloads.txt` (dos .mp4 en total). Descarga de `Worms Battlegrounds … .pkg` (1,31 GB, `ia801401.us.archive.org`) → cancelada por el usuario con ○ (`[dl] fallo: Descarga cancelada.`). **La velocidad no se pudo medir** (no había log); v0.7.0 lo registra.
- **Queja del usuario: descargas lentas/limitadas.** Hipótesis (sin confirmar): (1) lectura de red y escritura a disco en el mismo hilo (el HDD bloqueaba el socket); (2) `present()` (~61 ms) cada 300 ms con la red parada; (3) trozos de 32 KB; (4) archive.org limita por conexión (si es esto, la solución es **varias conexiones con `Range`**, pendiente). v0.7.0 ataca 1–3 y registra `[dl] … MB/s … cola de disco … KB` cada 5 s para distinguir disco de red.
- **archive.org:** muchos `.pkg` de `ia800504…/ps4-fpkg-collection-english-d/` devuelven **HTTP 403** (página de 4868 bytes) y otros `archive.org/download/<nombre>.pkg` dan 404 (URL mal construida a mano). El de `ia801401…-w/` sí descargó. Probable restricción de acceso del propio ítem (login/préstamo), no un fallo del navegador; sin comprobar.
- **Imágenes que no se decodifican (otra vez):** **76 ICO** (`image/x-icon`; ahora decodificados), 15 SVG, 8 GIF, 6 WebP. Con DuckDuckGo hay 37+14+9+8 peticiones a `external-content.duckduckgo.com` (proxy de imágenes/favicons).
- **Sitios probados:** DuckDuckGo, serviporno.com (**0 imágenes** en todas las páginas: miniaturas perezosas con `data-src`; v0.7.0 usa `data-src/data-original/srcset`), webflow.com, datatas.com, enablejavascript.io, Wikipedia (Kintsugi), orbispatches.com, archive.org (listados de 900 líneas OK). **En blanco por ser 100 % JavaScript:** `paint.js.org` (1702 bytes, 0 líneas), `kleki.com` (1 línea), Reddit (0 líneas, 8479 bytes), `archive.org/search.php` (0 líneas). `ps4pkg.locker`: `sendRequest 0x80431068` (error de conexión/TLS, sin diagnosticar).
- **Ratón:** conectado; clic derecho usado como "atrás". El log tenía ~9850 líneas de `sceMouseRead devuelve …` (alterna 0/1): limitado a 12 en v0.7.0.
- **Mando:** solo aparece `boton 16` y `hat` en este log (poco uso del mando; casi todo con ratón y teclado). `config.txt`: `wheelStep=7 dark=1 zoom=100 pageW=1200 smooth=1 images=1`. Marcadores: Kintsugi, webflow nojavascript, índice de archive.org, serviporno.
- CI: compiló sin errores (clang 18.1.3, `-O2 -DPSB_STRICT_TLS`).

## 5. Hallazgos técnicos (probados en PS4 salvo que se diga)
- **Red/TLS con el sistema:** `sceHttp`/`sceSsl` funcionan: DNS, HTTP, HTTPS, chunked, redirecciones (manuales leyendo `Location`). Pools: net 16 KB, ssl 300 KB, http 1 MB. Módulos NET, HTTP, SSL vía `sceSysmoduleLoadModuleInternal`. Certificados reales validados (`verifyErr=0`) → TLS estricto por defecto (`-DPSB_STRICT_TLS`).
- `sceHttpSetResponseHeaderMaxSize(tpl, 128*1024)` evita `sendRequest 0x80431073` en Wikipedia.
- Cabeceras de OpenOrbis con firmas incompletas (`void f();`): llamar con *cast* a la firma real.
- **SDL 2.0.9 de PS4:** solo ve el mando; render por software; 1920x1080. Teclado (`libSceKeyboard`, clase `Keyboard` del sample `keyboard`) y ratón (`libSceMouse` cargada en runtime, `app/mouse.*`) van con APIs nativas. Códigos de tecla = USB HID (D=7, L=15, T=23, W=26, 0=39, Enter=40, Esc=41, Backspace=42, Tab=43, Space=44, F5=62, F6=63, Home=74, PgUp=75, End=77, PgDn=78, →=79, ←=80, ↓=81, ↑=82).
- Escribir en `/data/psbrowser/` con `fopen` funciona.
- `std::thread` y `std::mutex` **sí** enlazan y funcionan en OpenOrbis (hilo de imágenes probado). Pero la **pila de los hilos es pequeña**: evitar recursión profunda y buffers grandes en la pila dentro de hilos.
- El toolchain trae `stb_image.h`, `stb_truetype.h`, `stb_image_write.h` (header-only), fuentes de muestra (Gontserrat, VeraMono, SIL OFL) y FreeType del sistema. Se usan stb + fuentes incrustadas.
- libc++ del toolchain: `std::sqrt(float)` devuelve `double` y rompe `std::min(1.f, ...)` → en `canvas.h` se usa `sqrtf`.
- **Sin verificar aún:** `libSceAvPlayer` (vídeo), `sceImeDialog` (teclado del sistema), `scePthreadAttrSetstacksize`/`pthread_attr_setstacksize` para hilos con pila grande (no se sabe si enlazan con los stubs de OpenOrbis).

- **gzip:** `sceHttp` descomprime solo (confirmado en la prueba v0.5.1). `inflate.cpp` es una red de seguridad.

## 6. Arquitectura (base v0.4.1; cambios de 0.5.x al final de la sección)
- `main.cpp` (~1300 líneas): pestañas (`std::vector<Browser*> g_tabs; g_cur; #define B (*g_tabs[g_cur])`), barra de herramientas (atrás/adelante/recargar/inicio, omnibox con candado, estrella de marcadores, menú), scroll suave (`scrollTarget` → `scrollY` con suavizado exponencial), scrollbar arrastrable, foco con Tab/cruceta sobre `doc.focus`, envío de formularios GET (POST se envía como GET con aviso), anclas `#id`, `about:home`/`about:settings`/`about:bookmarks`, `<meta refresh>`, página "vídeo no soportado" para `.mp4/.webm/.m3u8`, re-maquetado al cambiar zoom/ancho, caché global de imágenes (~40 MB), log de rendimiento de los primeros 40 frames (`[perf]`). Clic central: enlace en pestaña nueva / cerrar pestaña. Atajos: Ctrl+T/W/PgUp/PgDn, F5, F6.
- `htmldoc.*`: HTML → `Document` maquetado en píxeles (títulos, listas, `<pre>`, tablas simples, imágenes, formularios, anclas, `display:none`, color en línea). `pagerender.*`: dibujo del documento + hit-test. `textengine.*`: TTF con stb_truetype y caché de glifos. `canvas.h`: lienzo ARGB con recorte y mezcla alfa. `theme.h`: temas.
- `imgworker.*`: hilo de descarga+decodificación (PNG/JPEG/BMP con stb_image; **GIF desactivado**; tope 12 Mpx). API: `imgw_start/submit/poll/pending/cancel/pause`. `load()` en main llama `imgw_cancel()` + `imgw_pause(true)` mientras descarga la página. Las URLs `.gif` no se piden.
- `netclient.*`: `http_get` sobre sceHttp/sceSsl con mutex global. `url.*`: resolver/normalizar URLs, percent-encode/decode, desenvolver enlaces de salida de DuckDuckGo. `mouse.*`, `log.h` (`psb_log()` → `/data/psbrowser/log.txt`).
- `fonts_data.cpp` (~600 KB): Gontserrat (sans) y VeraMono (mono) incrustadas. Copias y licencia OFL en `third_party/fonts/`.
- `tools/render_host.cpp`: arnés PC→PNG para ver el render sin consola.
- `.github/workflows/build-pkg.yml`: copia `app/*.cpp|*.h` y `keyboard.*` del sample, `sed` del Makefile (TITLE, TITLE_ID, CONTENT_ID, `COMMONFILES :=`, `LIBS += -lSceNet -lSceSsl -lSceHttp -lSceKeyboard`), `make EXTRAFLAGS=-DPSB_STRICT_TLS`. Título `PS Browser`, Title ID `PSBR00001`, Content ID `IV0000-PSBR00001_00-PSBROWSER0000000`.

### Cambios de arquitectura en v0.5.0 / v0.5.1
- `osk.h/.cpp`: teclado en pantalla (capas letras/símbolos/acentos, Mayús de un toque o bloqueo, cursor UTF-8, modo contraseña). Lógica y dibujo independientes de SDL. Integrado en `main.cpp` (`g_osk`, `g_oskTarget`, `oskOpenField/oskOpenUrl/oskApply`); con él abierto el mando y el ratón no actúan sobre la página. Se abre al activar con ✕ un campo de texto y con los botones 8/9 para la barra de direcciones (índices supuestos). Con el teclado abierto: 0 escribir, 1 borrar (vacío = cerrar), 2 espacio, 3 mayúsculas, 4/5 cursor, 9 Enter, cruceta mueve.
- `inflate.h/.cpp`: deflate/zlib/gzip propio (probado en PC contra `zlib` de Python, sin errores bajo ASan/UBSan; no verifica CRC/Adler). `content_decode(encoding, body, out, max)`.
- `charset.h/.cpp`: detecta charset en `Content-Type` o `<meta>` (2 KB) y convierte ISO-8859-1/-15 y Windows-1252 a UTF-8; sin charset y UTF-8 inválido → asume Windows-1252. Otros charsets (CJK, UTF-16) se muestran sin convertir.
- `netclient`: `http_request(url, const HttpPost*, ...)` (POST con `Content-Type`; las redirecciones posteriores usan GET); `http_get` lo envuelve. Método POST pasado como `static_cast<decltype(ORBIS_METHOD_GET)>(1)` (**no verificado contra el header real**: si no compila, mirar `build.log` y `logs/toolchain_inventory.txt`).
- `main.cpp`: `g_post/g_hasPost` consumidos por `load()` una sola vez; `submitForm` envía POST real (urlencoded; no multipart). Volver/recargar de una página POST usa la copia en memoria/GET.
- CI: `-O2`; paso "Inventario del toolchain" → `logs/toolchain_inventory.txt` (libs/cabeceras de AvPlayer, video, IME, imagen; firmas de Http.h; samples).

### Cambios de arquitectura en v0.6.0 (descargas)
- **`netclient`:** `http_request(url, post, out, maxBody, maxRedirects, HttpSink* sink = nullptr, HttpGate gate = nullptr)`. `HttpSink{begin(HttpResult&), write(data,n)}` recibe el cuerpo por trozos (32 KB) sin acumularlo; con sink la petición pide `Accept: */*` y `Accept-Encoding: identity`. `HttpGate` (puntero a función) se llama con las cabeceras de la respuesta final **antes de leer el cuerpo**; si devuelve `false`, `out.stopped=true` y no se lee nada (solo cabeceras). `HttpResult` gana `contentDisposition`, `received`, `stopped`. Probado en PC con un `sceHttp` simulado (`tools/tests/test_netclient_mock.cpp`).
- **`download.h/.cpp` (puro, probado en PC):** `dl_is_page_type` (qué muestra el visor), `dl_is_previewable_image` (PNG/JPEG/BMP), `dl_is_streaming` (m3u8/mpd), `dl_is_attachment`, `dl_filename` (Content-Disposition `filename*`/`filename`, si no último segmento de la URL; sanea rutas/caracteres raros, máx. 120 bytes sin romper UTF-8; añade extensión según Content-Type; quita `.php/.asp…` de scripts), `dl_unique_path` (`nombre (1).ext`), `dl_format_size`, `dl_type_label`.
- **`main.cpp`:**
  - `load()` pasa `pageGate` a `http_request`: si el estado es 2xx y el tipo no es mostrable (o hay `Content-Disposition: attachment`) no lee el cuerpo y llama a `showDownloadPrompt` → página HTML de confirmación (nombre, tipo, tamaño, `[Descargar]` = `psb:dlgo:<url>`, `[Cancelar]` = `psb:dlback`, vista previa si es PNG/JPEG/BMP; `[Descargar]` queda seleccionado). Listas `.m3u8/.mpd` muestran un aviso de streaming. Ya no existe `isMediaUrl`: los `.mp4/.webm` directos se descargan.
  - `runDownload(url, err)`: **primer plano**. Pausa/cancela el hilo de imágenes, abre `/data/psbrowser/downloads/<nombre>.part`, `FileSink::write` escribe con `fwrite` (búfer de 256 KB) y llama a `dlTick()` cada ~300 ms: velocidad (media móvil), `SDL_PollEvent` (**○ = botón 1 cancela**; `SDL_QUIT` cancela y reenvía el evento) y `present()` con el recuadro `drawDownload`. Al terminar renombra `.part`→definitivo y anota en `/data/psbrowser/downloads.txt` (`nombre\tbytes\turl`). Fallos: borra el `.part` y muestra "Descarga fallida" con la causa y `[Reintentar]`. Detecta: HTTP no 2xx (403/429/503 → pista de Cloudflare/captcha/sesión), página HTML en vez del archivo, descarga incompleta (`received < Content-Length`), error de disco.
  - Páginas internas `about:downloads` (lista, `[borrar]` = `psb:deldl:N` borra el archivo) y `about:images` (imágenes de la última página web con imágenes, guardadas por `captureImages` en `setPage`; cada enlace lleva a la URL de la imagen → confirmación → descarga). Enlaces en `about:home`. Atajo teclado **Ctrl+J** = descargas.
- **No hace** (a propósito o por limitación): segundo plano/cola, reanudar, Referer/cookies (algunos hosts exigen Referer o sesión), cancelar con teclado/ratón (solo mando/○), cancelar mientras el servidor no envía datos, comprobar espacio libre (si se llena el disco, falla la escritura y se avisa), **resolver Cloudflare/captcha** (necesita JavaScript; ver §7.4–7.5).

### Cambios de arquitectura en v0.7.0
- **`asyncwriter.h/.cpp`:** `AsyncWriter` (std::thread + cola con contrapresión de 12 MB, espera con `sceKernelUsleep`; sin `condition_variable` a propósito). `open/write/finish/abort/queued`. `FileSink` (main.cpp) lo usa; `runDownload` mide tiempos de red y de vaciado de disco. `dlTick` redibuja cada 600 ms y loguea velocidad cada 5 s. `netclient`: trozo de lectura de 256 KB cuando hay sink.
- **`icodec.h/.cpp`:** `ico_extract` (BMP 1/4/8/24/32 bits con máscara AND, o PNG interno → stb). `imgworker::decode` lo usa si `ico_looks_like`.
- **`htmldoc.cpp`:** `<img>` con `src` marcador (data:, blank/pixel/lazy…) usa `data-src`, `data-original`, `data-lazy-src`, `data-lazy`, `data-url`, `data-echo`, `data-thumb` o la primera URL de `data-srcset`/`srcset`.
- **`pagerender`:** `hitImage()` (imagen bajo el puntero aunque no sea enlace).
- **`main.cpp`:** portapapeles interno `g_clip` (+ `/data/psbrowser/clipboard.txt`); `g_closedTabs` (15); modificadores `g_modCtrl/Alt/Shift` por frame. **Menú contextual** `g_ctx` (`ctxOpen/ctxRun/ctxMove/ctxHit/drawCtx`): abre con clic derecho, tecla Menu (HID 101), Mayús+F10 o botón 11 del mando; según el destino (enlace, imagen, pestaña, omnibox, página). Con el menú abierto captura teclado, ratón y mando. Clic derecho ya **no** es "atrás" (está en el menú; botones laterales 8/16 del ratón = atrás/adelante, no verificados). Atajos nuevos: Ctrl+C/V/X/A (barra y campos), Ctrl+C sin editar, Ctrl+V = abrir lo copiado, Ctrl+R, Ctrl+Mayús+T, Ctrl+1..9, Alt+Inicio, Ctrl+clic, Alt+clic.
- **`mouse.cpp`:** el log de `sceMouseRead` se limita a 12 cambios y el volcado de botones a 120.
- **CI:** nuevo input `probe_js` (por defecto true) → paso "Sonda de JavaScript (QuickJS-ng)" (`continue-on-error`): clona la última release de `quickjs-ng/quickjs`, lista archivos/API y prueba a compilar cada `.c` con los flags de OpenOrbis → `logs/quickjs_probe.txt`. **No altera el PKG.**

## 7. Roadmap (el usuario quiere, en este orden de interés)
1. ~~Rendimiento~~ y ~~teclado en pantalla~~: hechos y aprobados. Opcional más adelante: rectángulos sucios para pasar de ~15 a 30+ fps.
2. **Compatibilidad con páginas (sin JS)** — en curso (v0.7.0 añadió ICO e imágenes perezosas):
   - Hecho en 0.5.1 (sin probar): gzip/deflate, charsets latinos, POST, cabeceras `Accept`.
   - Siguiente: **cookies** básicas (jar en memoria por dominio, `Set-Cookie`/`Cookie`; con `sceHttp` hay que leer `Set-Cookie` de `GetAllResponseHeaders` y añadir `Cookie` a mano); **SVG** (nanosvg header-only: que el CI lo descargue con `curl` fijando versión, y compilar con `-DPSB_NO_SVG` si falla la descarga); **WebP** (libwebp); **GIF** (reactivar cuando haya hilo con pila grande o parchear la recursión de stb); **CSS básico** (colores de fondo, `text-align`, anchos, `display`, `<style>` simple); tablas reales; User-Agent configurable en `config.txt` (`ua=`); `multipart/form-data`; `<select>` visible; favicons `.ico`.
3. **Vídeo (riesgo alto).** El toolchain **sí trae** `libSceAvPlayer` y su cabecera `orbis/AvPlayer.h` (ver §4). Siguiente: un PKG de prueba aparte que abra un `.mp4` directo (H.264/AAC) con `libSceAvPlayer`, registrando cada paso en el log. Se desconoce si convive con el render por software de SDL y cómo obtener los fotogramas. Si funciona, los enlaces `[video]` ya detectados lo abrirían. YouTube sin JS solo vía instancias Invidious/Piped (inestables). Alternativa: servidor intermedio que transcodifique.
4. **Fidelidad visual de sitios modernos (XDA, Platinmods, Reddit…): decisión pendiente entre tres caminos** (pueden combinarse):
   - **A. "CSS lite" propio:** descargar las hojas `<link rel=stylesheet>`/`<style>` y soportar un subconjunto (selectores simples, colores/fondos, tamaños, márgenes/padding, bordes, `text-align`, `display`, flex en fila sencillo). Mejora mucho sitios renderizados en servidor (Platinmods/XenForo, artículos de XDA) pero nunca será fiel; sin JS. Iterable en PC con `tools/render_host.cpp` si el usuario aporta HTML+CSS guardados.
   - **B. Portar NetSurf** (libcss + layout reales, sin JS): mejor fidelidad CSS 2.1, gran esfuerzo y riesgo en OpenOrbis; no probable de compilar sin red/toolchain local.
   - **C. Servidor de renderizado remoto** (Playwright/Chromium en PC/móvil/VPS del usuario): la PS4 pide `/render?url=…`, recibe teselas PNG de la página real (con JS) más un mapa JSON de zonas clicables/campos, y envía clics/teclas de vuelta. Única vía para Reddit y webs SPA y "como son"; también podría servir vídeo (transcodificado a H.264/MJPEG). Requiere un equipo encendido y conexión; latencia por página. Reutiliza `http_get` (HTTP plano en LAN) y el decodificador PNG/JPEG ya probados. Aún no escrito.
5. **JavaScript (lo que más pide el usuario): ver `docs/JS.md`.** Requiere primero un árbol DOM (hoy `htmldoc.cpp` maqueta en una pasada, sin DOM ni CSS), CSS lite y luego QuickJS-ng + DOM mínimo + temporizadores/eventos/fetch. Aun así **no** dará Reddit/YouTube/apps de canvas (paint.js.org, kleki.com): para eso, servidor de renderizado remoto (camino C). Siguiente paso: pasar `logs/quickjs_probe.txt` del CI de v0.7.0 y decidir A (DOM + CSS lite) o B (servidor remoto).
5b. **Descargas más rápidas:** si el log de v0.7.0 muestra la cola de disco vacía (cuello = red), implementar **descarga en N conexiones con `Range`** (archive.org y CDNs suelen limitar por conexión; requiere sacar el mutex global de `netclient` o una segunda instancia de `sceHttp`) y reanudar `.part`. Opcional: cola/segundo plano.
5c. **Interfaz:** pendiente de que el usuario concrete qué quiere cambiar (ideas: buscar en página Ctrl+F, marcadores en barra, favicons en pestañas, desbordamiento de pestañas, menú de ajustes dentro de la interfaz en vez de página `about:settings`, cursor con forma de mano sobre enlaces, historial persistente).
6. ~~Descargas~~: hechas en v0.6.0 (probadas en PS4: funcionan) y aceleradas en v0.7.0 (hilo de disco; **medir con el log**). Pendiente/idea: varias conexiones `Range`, cola o segundo plano, reanudar, Referer, comprobar espacio libre, **WebP/SVG/GIF** (ICO hecho en 0.7.0; 6 WebP, 15 SVG, 8 GIF fallan en el log de 0.6.0), caché en disco, historial persistente; NetSurf a largo plazo.
7. **Descargas tras Cloudflare/captcha:** no son posibles en la consola (hace falta JS y huella de navegador; `cf_clearance` copiada de un PC no es fiable: atada a IP/UA y a veces a la huella TLS). Vía realista = servidor intermedio (§7.4-C): el usuario resuelve el desafío a mano en un navegador real y el servidor sirve el archivo por la LAN. No se añadirá resolución automática de captchas.

## 8. Cómo compilar localmente (replicando el CI)
1. Descargar y descomprimir el toolchain; `export OO_PS4_TOOLCHAIN=<ruta>/OpenOrbis/PS4Toolchain`.
2. Copiar `samples/` a un temporal, crear `psbrowser/` a partir de `samples/SDL2/` (Makefile, `sce_sys`, `sce_module`), copiar `app/*.cpp app/*.h` y `samples/keyboard/keyboard/keyboard.*`, copiar `third_party/`.
3. `sed` del Makefile como en el workflow (ver arriba), `make eboot.bin EXTRAFLAGS=-DPSB_STRICT_TLS`.
4. Un banco de pruebas en PC (SDL2 + stubs de `orbis/libkernel.h` y `keyboard.h`) sirve para ejecutar la lógica sin consola; `tools/render_host.cpp` renderiza páginas a PNG.

## 9. Limitaciones conocidas
Sin JavaScript (**ni DOM ni CSS**; ver `docs/JS.md`), vídeo solo MP4 H.264/AAC con libSceAvPlayer (v0.8.0, experimental, sin probar; sin HLS/WebM), sin CSS (salvo color en línea y `display:none`), sin cookies ni Referer, sin caché en disco, imágenes solo PNG/JPEG/BMP (sin WebP, GIF, SVG ni ICO), carga de páginas bloqueante, ~15 fps, charsets no latinos sin soporte, formularios sin multipart, índice de botón 8 del mando sin confirmar (9 sí aparece), 17/18/19 sin identificar. Descargas: solo en primer plano, una a la vez, cancelar solo con ○ del mando.

## 10. Git (v0.7.0)
Archivos nuevos: `app/asyncwriter.h`, `app/asyncwriter.cpp`, `app/icodec.h`, `app/icodec.cpp`, `docs/JS.md`, `tools/tests/test_asyncwriter.cpp`, `tools/tests/test_ico.cpp`, `tools/tests/ico_test.py`. Cambiados: `app/main.cpp`, `app/htmldoc.cpp`, `app/imgworker.cpp`, `app/mouse.cpp`, `app/netclient.cpp`, `app/pagerender.cpp`, `app/pagerender.h`, `.github/workflows/build-pkg.yml` (**el workflow SÍ cambia**: input `probe_js` + paso de sonda), `README.md`, `docs/CONTEXTO.md`, `docs/PRUEBAS.md`, `tools/README.md`.
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party .github README.md
git commit -m "v0.7.0: menu contextual, portapapeles, atajos, descargas con hilo de disco, ICO, imagenes perezosas, sonda QuickJS"
git push
```
Después: Actions → **Build PKG** → *Run workflow* (deja `probe_js` marcado) → instalar el PKG → probar según `docs/PRUEBAS.md` («Prueba v0.7.0») → devolver `/data/psbrowser/log.txt`, el zip `logs` (con `quickjs_probe.txt`) y `downloads.txt`.

## 11. Prompt sugerido para una IA nueva
"[ESTADO ACTUAL: lee primero la seccion >>> ESTADO v0.8.0 <<< de docs/CONTEXTO.md: el video y el boton Salir ya estan integrados en main.cpp pero sin probar en consola; lo primero es compilar con el CI y probar con mi log] Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md` del zip adjunto. v0.6.0 está probada en consola (pestañas, imágenes PNG/JPEG, formularios GET/POST, teclado en pantalla, gzip transparente, descargas). v0.7.0 (menú contextual, portapapeles, atajos, descargas con hilo de disco, ICO, imágenes perezosas, sonda de QuickJS) está sin probar: lo primero es verificarla con mi `log.txt` (líneas `[dl]` con MB/s) y `logs/quickjs_probe.txt`. Después, según el roadmap: **JavaScript** (`docs/JS.md`: árbol DOM → CSS lite → QuickJS), descargas en varias conexiones si el cuello es la red, WebP/SVG/GIF, cookies y el experimento de vídeo con libSceAvPlayer. Responde en español, sé honesto sobre lo no probado o no compilado, pídeme `log.txt` antes de tocar mapeos de entrada, dame los comandos `git` exactos si cambias archivos y entrégame cada vez un `CONTEXTO.md` actualizado."
