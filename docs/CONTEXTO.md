# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva junto con el zip del repo. Idioma de trabajo: **español**.
> Última actualización: 6 de noviembre de 2026.

## 1. Qué es
Navegador homebrew para **PS4 con GoldHEN**, hecho con el toolchain **OpenOrbis** (LLVM, `x86_64-pc-freebsd12-elf`).
No usa WebKit ni JavaScript: es un visor propio de HTML estático (texto, enlaces, imágenes, formularios GET).
Objetivo a largo plazo: portar NetSurf; plan B: seguir haciendo crecer el visor propio. Proyecto personal/aprendizaje.

## 2. Cómo trabajamos (reglas del usuario)
- La IA escribe código y workflows; el usuario sube a GitHub, ejecuta el CI (GitHub Actions, workflow manual `build-pkg.yml`), instala el PKG en su PS4 real y devuelve `/data/psbrowser/log.txt` (a veces `last.html`, `config.txt`, `bookmarks.txt`, y el zip `logs` del CI).
- Si cambia un workflow, dar los comandos `git` exactos. No añadir al commit carpetas de desarrollo sin seguimiento.
- Ser honesto sobre lo que **no** se ha probado en consola. Pedir el log antes de cambiar mapeos de entrada.
- Repo del usuario: `Sackso_PS4_Browser_Testing` (GitHub). Proyecto anterior reutilizado: `sacksso/Sackso_Ren.py_ps4-testing`.
- Entorno de la IA: puede descargar el toolchain (`https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz`, 158 MB), instalar `clang lld` con apt y **compilar de verdad** con él. `/bin/sh` es dash (sin brace expansion). Salida de herramientas: filtrar con `tr -cd '\11\12\40-\176'` si hay bytes raros.

## 3. Estado por versiones
| Versión | Contenido | Estado |
|---|---|---|
| 0.1.2 | Red HTTP/HTTPS con `sceHttp`, visor de texto | Probada en PS4 |
| 0.2.0 | Enlaces, barra de URL, historial, teclado, ratón, mando | Probada: funciona (el ratón iba muy lento) |
| **0.3.0** | `about:settings` (sensibilidad ratón 3.5 por defecto, aceleración, rueda, inicio), marcadores (`about:bookmarks`), barra de botones, enlaces `[video]`/`[video incrustado]`, `<meta refresh>` | **Probada en PS4: "todo se ve bien".** Entregada como `Sackso_PS4_Browser_Testing-v0.3.0.zip` |
| **0.4.0** | Rediseño "navegador de escritorio": fuente TrueType proporcional con tildes, tema claro/oscuro, pestañas, scroll suave en píxeles, imágenes PNG/JPEG/GIF/BMP, formularios GET, anclas `#id`, `display:none`, color en línea | **EN CURSO. Ver §5.** |

### Resultados de las pruebas de v0.3.0 (logs del usuario)
- Ratón: lectura cruda de ±1 por muestra → por eso hace falta multiplicador; `config.txt` del usuario: `mouseSens=3.50 mouseAccel=1 wheelInvert=0 wheelStep=3 home=`.
- YouTube: 225 KB de HTML pero 0 líneas/0 enlaces (es app de JavaScript; imposible sin motor JS).
- `html.duckduckgo.com/html/` sin `?q=` da 1 línea (solo un formulario; antes no había formularios). Buscar desde la barra con `?q=` sí funciona.
- Hacker News y Wikipedia cargan bien. Bookmark guardado: Kintsugi (es.wikipedia).

## 4. Hallazgos técnicos (probados en PS4 salvo que se diga)
- **Red/TLS con el sistema:** `sceHttp`/`sceSsl` funcionan: DNS, HTTP, HTTPS, chunked, redirecciones (manuales leyendo `Location`). Pools: net 16 KB, ssl 300 KB, http 1 MB. Módulos: NET, HTTP, SSL vía `sceSysmoduleLoadModuleInternal`. Certificados reales validados (`verifyErr=0`) → TLS estricto por defecto.
- **Cabeceras grandes:** `sceHttpSetResponseHeaderMaxSize(tpl, 128*1024)` evita `sendRequest 0x80431073` en Wikipedia.
- **Cabeceras de OpenOrbis con firmas incompletas** (`void f();`): llamar con *cast* a la firma real.
- **SDL 2.0.9 de PS4:** solo ve el mando; render por software; 1920x1080. Teclado (`libSceKeyboard` vía clase `Keyboard` del sample `keyboard`) y ratón (`libSceMouse` cargada en runtime, `app/mouse.*`) van con APIs nativas. Códigos de tecla = USB HID (D=7, L=15, T=23, W=26, 0=39, Enter=40, Esc=41, Backspace=42, Tab=43, Space=44, '-'=45, '='=46, F5=62, F6=63, F10=67, Home=74, PgUp=75, End=77, PgDn=78, →=79, ←=80, ↓=81, ↑=82).
- Escribir en `/data/psbrowser/` con `fopen` funciona.
- **Toolchain OpenOrbis v0.5.4 trae** (verificado descargándolo): `include/stb/stb_truetype.h`, `stb_image.h`, `stb_image_write.h` (header-only, sin enlazar nada), `libSDL2_image.a`, FreeType del sistema (`-lSceFreeType`), fuentes de muestra en `samples/*/assets/fonts/` (Gontserrat-Regular.ttf —SIL OFL, cubre todo el latín—, VeraMono.ttf). **Por eso no hay que compilar FreeType/libpng/libjpeg**: se usa stb.
- **Sin verificar aún:** que `std::thread`/`std::mutex` enlacen y funcionen en OpenOrbis (el sample `threading` existe en el toolchain; usa `sceKernelUsleep` de `orbis/libkernel.h`); que stb_truetype/stb_image compilen con el clang de OpenOrbis (en PC con g++ sí).



## 5.00 Prueba 6 (v0.4.0 en PS4 real) y correcciones
- **Funciona:** compila en el CI, arranca, tema/pestañas/barra, info.cern.ch (3 páginas), Wikipedia EN (1,1 MB, 1149 líneas, 20 imágenes) y el hilo de imágenes baja una imagen por TLS.
- **Rendimiento:** dibujo ~45 ms + subida/presentación ~55 ms por frame = ~10 fps. Pendiente: capa de página en caché y redibujar solo lo que cambia.
- **Crash en Wikipedia:** el log termina tras descargar `donate.gif` (223 KB), sin ningún log posterior (ni botones). Hipótesis: `stb_image` decodifica GIF con recursión de hasta ~4000 niveles y la pila del hilo (PS4) es pequeña. NO confirmado.
- **Cuelgue al navegar después:** `http_get` retiene un mutex toda la petición; el hilo de imágenes podía acaparar la red. Corregido con `imgw_cancel()` + `imgw_pause()` en `load()`.
- **Cambios (sin compilar en esta sesión, sin red):** GIF desactivado (no se descarga ni se decodifica), tope de 12 Mpx por imagen, logs `[img] decodificando/ok`. Si la próxima prueba no se cuelga, la causa era el GIF; si se cuelga, el log dirá en qué imagen.
- Ratón: `conectado=0` en toda la prueba (no había ratón USB conectado).

## 5.0 Estado real (6-nov-2026, sesión de recuperación)
- `app/main.cpp` de v0.4.0 **ya está reescrito** (1292 líneas; pestañas, barra de herramientas, scroll suave, imágenes, formularios GET, anclas). Se recuperó del chat anterior, donde **compiló y enlazó con el toolchain OpenOrbis v0.5.4 (eboot.bin generado)**.
- Cambios aplicados al recuperar: `url.h/.cpp` con `urlPercentDecode` y `urlUnwrapSearchRedirect`; en `canvas.h` `std::sqrt(` -> `sqrtf(` (el toolchain devolvia double y rompia `std::min(1.f, ...)`); se eliminaron `htmltext.*` (obsoletos).
- **No se ha vuelto a compilar en esta sesión** (sin red para bajar el toolchain). No probado en PS4. Pendiente: CI, PKG, prueba en consola y banco de pruebas headless.
- Si falta algo de §5.1 (secciones marcadas arriba como "falta"), revisar `main.cpp` antes de reescribir: la mayoría ya existe.

## 5. v0.4.0 — qué está hecho y qué falta

### Hecho (compilado en PC con g++ y verificado visualmente con el arnés `tools/render_host.cpp`; **no** compilado aún con el toolchain de PS4)
Archivos nuevos/reescritos en `app/`:
- `fonts_data.cpp/.h` — Gontserrat (sans) y VeraMono (mono) incrustadas como arrays (`kFontSansData`, `kFontMonoData`). Copias en `third_party/fonts/` (+ `OFL.txt`).
- `textengine.h/.cpp` — `te_init`, `te_width`, `te_ascent/descent`, `te_draw(cv,x,baseY,utf8,font,px,bold,argb)`, `te_fit` (elipsis), `te_next` (UTF-8). stb_truetype, caché de glifos, negrita sintética solo <26 px.
- `canvas.h` — lienzo ARGB con recorte (`setClip`), mezcla alfa, `rect`, `rrect`, `rrectBorder`, `circle`, `line`, `blit`. (El texto 8x8 de `third_party/font8x8_basic.h` ya no se usa.)
- `htmldoc.h/.cpp` — **reescrito**: maquetación en píxeles. `Document{lines(y,h,baseline,spans), links, focus[], images[], forms[], fields[], anchors, refreshUrl, height, width}`. `parseHtml(html, widthPx, zoomPct)`, `parsePlain`. Soporta títulos con tamaños, listas anidadas/numeradas, `<pre>`, `<hr>`, negrita/código, `<font color>`/`style=color`, `display:none`/`hidden` (salta el subárbol), imágenes (`<img>` con caja reservada; ignora píxeles de seguimiento ≤3 px; máx 60), formularios (`input` text/password/hidden/submit/checkbox/radio, `button`, `textarea`, `select`→campo oculto con la opción seleccionada), anclas (`id`/`name`), `<video>/<iframe>/<embed>` como enlaces `[video]`/`[video incrustado]`, `<meta refresh>`, tablas (celdas separadas por hueco; con `|` solo si la tabla tiene `<th>`; los `div/center` dentro de celdas no rompen línea).
- `theme.h` — `Theme` claro/oscuro + `fixContrast`.
- `pagerender.h/.cpp` — `drawDocument(Canvas&, Document&, RenderCtx)` (texto, enlaces subrayados con resaltado, código con fondo, imágenes/placeholders, campos con caret, botones, casillas) y `hitFocus(doc, ctx, mx, my)` → índice en `doc.focus`.
- `imgworker.h/.cpp` — hilo de descarga+decodificación (stb_image PNG/JPEG/GIF/BMP; **no** WebP/SVG), cola `imgw_submit/imgw_poll/imgw_pending`, reduce con promedio por área a la caja reservada.
- `netclient.cpp` — `http_get` ahora lleva un `std::mutex` global (se usa desde 2 hilos).
- `tools/render_host.cpp` + `tools/README.md` — arnés PC→PNG.

### Falta (en este orden)
1. **Reescribir `app/main.cpp`** (hoy sigue siendo el de v0.3.0 y **no compila** con los módulos nuevos: usa la API vieja de `htmldoc` y `canvas.h`). Reutilizar de v0.3.0: logging, `Config`/marcadores (`configLoad/Save`, `bookmarksLoad/Save`), `handleAction` (`psb:` links), arranque SDL/teclado/ratón/mando, `makeCursor`, bucle principal, `onKeyDown` base. Diseño acordado:
   - **Geometría 1920x1080:** franja de pestañas y=0..40; barra de herramientas y=40..88; viewport desde y=88; scrollbar de 14 px a la derecha; contenido centrado de ancho `min(config.pageW, W-28-80)` (por defecto 1200).
   - **Pestañas:** `std::vector<Browser*> g_tabs; int g_cur; #define B (*g_tabs[g_cur])`. Cada `Browser` guarda doc, url, `html` (fuente, para re-maquetar), historial (≤50, cada entrada con `layoutKey`; si no coincide con el actual → recargar), `scrollY` (float, mostrado) y `scrollTarget` (int), `sel` (índice de `doc.focus`), `hover`, `editField`, estado de la barra de URL, `id` único y `docGen` (se incrementa al cambiar de documento). Atajos: Ctrl+T nueva, Ctrl+W cerrar, Ctrl+PgUp/PgDn cambiar; clic en pestaña / en "x" / en "+"; clic central en enlace = nueva pestaña.
   - **Barra:** atrás, adelante, recargar, inicio (iconos dibujados con `line/circle`), omnibox redondeada (candado si https; host en color de texto y resto en tenue; edición con caret y "seleccionar todo" al entrar), estrella (marcador; rellena si ya está), botón de marcadores, botón de menú → `about:settings`. Lista `UiRect` reconstruida en cada dibujo para el ratón. Barra de progreso fina bajo la barra mientras carga (la carga de páginas sigue siendo **bloqueante**). Globo de estado abajo-izquierda con la URL del enlace bajo el cursor / mensajes temporales.
   - **Scroll suave:** `scrollBy(px)` mueve `scrollTarget`; cada frame `scrollY += (target-scrollY)*(1-exp(-dt/60ms))` y redibuja; opción `smooth` en config. Rueda = `wheelStep*40` px; flechas 64 px; PgUp/PgDn/Espacio 90 % del alto. Scrollbar arrastrable. Registrar en el log el tiempo de dibujo por frame (primeros N) para medir rendimiento en la consola.
   - **Foco/selección:** Tab / cruceta recorren `doc.focus`; Enter/clic activan: enlace → seguir; campo texto → `editField` (teclado ASCII, Enter envía el formulario, Esc sale, Tab pasa al siguiente); botón → enviar; checkbox → alternar; radio → marcar y desmarcar los de igual `name`+`form`.
   - **Envío de formularios:** construir `action?campo=valor&...` con `urlPercentEncode` (solo campos con `name`; submit/button solo el pulsado; checkbox/radio solo si marcados; `hidden` siempre). Quitar la query previa de `action`. `method=post` se envía **como GET** (avisar en el estado) hasta que exista POST en `netclient`.
   - **Anclas:** `#frag` → buscar en `doc.anchors` (línea) y saltar; navegar a URL con fragmento = cargar sin fragmento y luego saltar; mismo documento = solo saltar.
   - **Imágenes:** cada ~120 ms, recorrer líneas visibles ±1 pantalla; para `ImageRef.state==0`: resolver URL con `urlResolve(B.url, src)`, mirar la caché global (`map<url|WxH, pix>`, tope ≈40 MB) o enviar `ImgJob` (máx. 3 en curso), `state=1`. Al llegar `ImgResult`: buscar pestaña por `tabId`, ignorar si `gen != docGen`, rellenar `pix/pw/ph` y `state=2` (o 3 si falló). Al restaurar un doc del historial, pasar `state==1` → 0. Opción `images` on/off.
   - **Re-maquetar** (`parseHtml(B.html, contentW, zoom)`) al cambiar zoom o ancho de página, conservando la proporción de scroll; en otras pestañas al activarlas.
   - **about:home** con formulario de búsqueda (`https://html.duckduckgo.com/html/`, GET, campo `q`), marcadores y enlaces; **about:settings** con: sensibilidad/aceleración/rueda (ya existen), tema claro/oscuro (`psb:theme`), tamaño del texto 80–200 % (`psb:zoom±`), ancho de página (`psb:pw±`), scroll suave, imágenes on/off, página de inicio.
   - URLs de vídeo (`.mp4/.webm/.m3u8`) no se descargan: mostrar página "Vídeo aún no soportado" con la URL.
   - Mando: igual que v0.3.0 (botones 0 abrir, 1 atrás, 2 recargar, 3 inicio, 4/5 página — índices **supuestos**; cruceta = foco/atrás/adelante; stick izquierdo = scroll continuo proporcional).
2. **Actualizar `.github/workflows/build-pkg.yml`:** ya copia `app/*` y `third_party/` (las fuentes están en `app/fonts_data.cpp`, no hace falta copiar TTF). Comprobar que el `COMMONFILES`/lista de fuentes del Makefile incluye todos los `.cpp` nuevos (`textengine`, `pagerender`, `imgworker`, `fonts_data`, `htmldoc`) y que el `sed` mantiene `-lSceNet -lSceSsl -lSceHttp -lSceKeyboard`. Añadir `-I$(TOOLCHAIN)/include` si `<stb/...>` no se resuelve (debería, es el include estándar del toolchain). `fonts_data.cpp` pesa ~600 KB de texto; compila en segundos.
3. **Compilar de verdad** con el toolchain (descargarlo, `apt install clang lld`, replicar los pasos del CI: copiar `samples/` a un temporal, crear `psbrowser/` desde el Makefile del sample SDL2, copiar `app/*` y `keyboard.h/.cpp`, `sed` de TITLE/TITLE_ID/CONTENT_ID, `make EXTRAFLAGS=-DPSB_STRICT_TLS`). Resolver errores de enlace de `std::thread` (si no enlaza: usar `scePthreadCreate` de libkernel, o pedir las imágenes en el hilo principal en trozos).
4. Probar en la PS4 y pedir el `log.txt`: rendimiento del scroll (ms/frame), si las imágenes cargan, si el hilo funciona, mapeos del mando.
5. Actualizar `docs/PRUEBAS.md` ("Prueba 6") y este documento; dar comandos `git` exactos.

## 6. Roadmap posterior (decisión del usuario: primero 1+2 de abajo, luego el resto)
1. ✅(en curso) Aspecto nativo + fluidez (fuente TTF, tema, pestañas, scroll suave).
2. ✅(en curso) Imágenes y formularios GET.
3. Más sitios sin JS: cabeceras/User-Agent de navegador, cookies básicas, POST en `netclient` (`sceHttpCreateRequestWithURL` con método POST + `Content-Type`), WebP (stb no lo trae; evaluar libwebp), CSS básico (colores de fondo, `text-align:center`, anchos), tablas reales, alternativas sin JS para YouTube (Invidious/Piped: inestables).
4. **Experimento de vídeo (riesgo alto):** PKG de prueba aparte que intente abrir un `.mp4` directo con `libSceAvPlayer` y lo muestre; registrar cada paso en el log. No se sabe si OpenOrbis lo permite junto con el SDL actual (render por software). Si funciona, los enlaces `[video]` ya detectados lo abrirían.
5. Descargas, caché en disco, historial persistente.
6. Plan largo: reconocimiento y port de NetSurf (`docs/NETSURF.md`; `git.netsurf-browser.org` no era accesible desde el entorno de la IA).

## 7. Estructura del repo
```
app/main.cpp          (v0.3.0, A REESCRIBIR — ver §5.1)   app/netclient.*  HTTP/HTTPS sobre sceHttp/sceSsl (mutex)
app/htmldoc.*         HTML -> Document maquetado en px     app/url.*        resolver/normalizar URLs, DuckDuckGo
app/textengine.*      TTF (stb_truetype)                   app/pagerender.* dibujo del documento + hit-test
app/canvas.h          lienzo ARGB                          app/theme.h      temas claro/oscuro
app/imgworker.*       descarga+decodifica imágenes (hilo)  app/fonts_data.* fuentes incrustadas
app/mouse.*           libSceMouse en runtime               app/log.h        psb_log() -> /data/psbrowser/log.txt
third_party/fonts/    TTF + licencia OFL                   third_party/font8x8_basic.h (ya sin uso)
tools/                arnés PC→PNG                         .github/workflows/build-pkg.yml  CI
docs/                 PLAN.md NETSURF.md PRUEBAS.md CONTEXTO.md
```
PKG: título `PS Browser`, Title ID `PSBR00001`, Content ID `IV0000-PSBR00001_00-PSBROWSER0000000`.

## 8. Limitaciones conocidas
Sin JavaScript, sin vídeo/audio, sin CSS (salvo color en línea y `display:none`), sin cookies/caché/descargas, POST enviado como GET, imágenes solo PNG/JPEG/GIF/BMP, carga de páginas bloqueante, teclado solo ASCII al escribir, índices de botones del mando supuestos, rueda/ratón ajustables en `about:settings`.

## 9. Prompt sugerido para una IA nueva
"Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md` del zip adjunto. v0.3.0 está probada y funciona; v0.4.0 está a medias: los módulos nuevos (htmldoc, textengine, pagerender, imgworker, canvas, theme) están hechos y verificados en PC, pero `app/main.cpp` hay que reescribirlo según §5.1 y luego compilar con el toolchain real. Responde en español, sé honesto sobre lo no probado, pídeme `log.txt` antes de tocar mapeos de entrada y dame los comandos `git` exactos si cambias el workflow."
