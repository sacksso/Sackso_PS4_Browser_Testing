# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva junto con el zip del repo (y, si los tienes, el último `log.txt` y el zip `logs` del CI). Idioma de trabajo: **español**.
> Última actualización: 6 de noviembre de 2026 (v0.4.1, probada en PS4 real).

## 1. Qué es
Navegador homebrew para **PS4 con GoldHEN**, hecho con el toolchain **OpenOrbis** (LLVM, `x86_64-pc-freebsd12-elf`).
No usa WebKit ni JavaScript: es un visor propio de HTML estático (texto, enlaces, imágenes, formularios GET) con interfaz de "navegador de escritorio" (pestañas, barra, tema claro/oscuro, scroll suave).
Objetivo a largo plazo: más compatibilidad, teclado en pantalla con mando, vídeo; plan B/C: portar NetSurf (`docs/NETSURF.md`) o un servidor intermedio para sitios con JavaScript. Proyecto personal/aprendizaje.

## 2. Cómo trabajamos (reglas del usuario)
- La IA escribe código y workflows; el usuario sube a GitHub, ejecuta el CI (GitHub Actions, workflow manual `build-pkg.yml`), instala el PKG en su PS4 real y devuelve `/data/psbrowser/log.txt` (a veces `last.html`, `config.txt`, `bookmarks.txt` y el zip `logs` del CI).
- Si cambia un workflow, dar los comandos `git` exactos. No añadir al commit carpetas de desarrollo sin seguimiento.
- Ser honesto sobre lo que **no** se ha probado en consola. Pedir el log antes de cambiar mapeos de entrada.
- Repo del usuario: `Sackso_PS4_Browser_Testing` (GitHub). Proyecto anterior reutilizado: `sacksso/Sackso_Ren.py_ps4-testing`.
- Consejos prácticos para la IA:
  - Los archivos de chat/log que sube el usuario pueden venir en **UTF-16** (BOM `ff fe`): decodificar con `utf-16`, no con `cat`. Si hay bytes raros, filtrar con `tr -cd '\11\12\40-\176'`.
  - Si el entorno tiene red: descargar el toolchain (`https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz`, 158 MB), `apt install clang lld` y **compilar de verdad** replicando el CI (ver §8). Si no hay red, decirlo: los cambios quedan sin compilar y el CI es la comprobación.
  - `/bin/sh` es dash (sin brace expansion).

## 3. Estado por versiones
| Versión | Contenido | Estado |
|---|---|---|
| 0.1.2 | Red HTTP/HTTPS con `sceHttp`, visor de texto | Probada en PS4 |
| 0.2.0 | Enlaces, barra de URL, historial, teclado, ratón, mando | Probada |
| 0.3.0 | `about:settings`, marcadores, barra de botones, `[video]`, `<meta refresh>` | Probada: "todo se ve bien" |
| 0.4.0 | Rediseño de escritorio: TTF proporcional, tema, pestañas, scroll suave, imágenes, formularios GET, anclas | Compiló en el CI y arrancó, pero **se colgó en Wikipedia** (ver §5) |
| 0.5.0 (sin probar en PS4) | 0.4.1 + compilación con `-O2` en el CI + teclado en pantalla con el mando (`osk.*`) | Compila solo con stubs en PC; **falta CI y prueba en consola** |
| **0.4.1** | 0.4.0 + arreglo del cuelgue (GIF desactivado, pausa/cancelación del hilo de imágenes, límite de tamaño, más logs) | **Probada en PS4: "todo bien".** Es la base actual |

La cadena de texto de arranque del log aún dice `psbrowser v0.4.0` (no se cambió); conviene subirla a 0.4.1 en la próxima compilación.

## 4. Resultados de las pruebas en PS4 real
### Prueba 6 (v0.4.0)
- Funciona: arranque, tema, pestañas, barra, info.cern.ch, Wikipedia EN (1,1 MB, 1149 líneas, 1617 enlaces, 20 imágenes, alto 34 932 px).
- El log terminó justo tras descargar `donate.gif` (223 KB) y **no volvió a aparecer ningún `boton`/`hat`**: la app entera dejó de responder.
- Hipótesis (no demostrada, pero el arreglo funcionó): `stb_image` decodifica GIF con una función recursiva (hasta ~4000 niveles) y la pila del hilo de imágenes en PS4 es pequeña. Además, `http_get` retiene un mutex toda la petición y el hilo de imágenes podía acaparar la red mientras el hilo principal intentaba navegar.

### Prueba 7 (v0.4.1) — OK
- Navegó por Wikipedia EN, Hacker News y dos páginas de hightouch.com (552 KB, 45 imágenes) sin colgarse.
- Imágenes: PNG y JPEG decodifican bien (`[img] ok 245x52`, `ok 250x327`, `ok 160x84`).
- **SVG falla** (`image/svg+xml`: logo de Wikipedia, icono `y18.svg` de Hacker News): no hay decodificador; se muestra el recuadro. Muy frecuente en la web moderna.
- **Rendimiento: ~95–116 ms por frame** (dibujo ~45–50 ms + subida/presentación ~50–60 ms) = ~10 fps. Es el principal punto débil de la interfaz.
- Ratón USB: `conectado=0` en toda la prueba (no había ratón conectado); el mando se usó para todo.
- Mapeo del mando observado en el log (DualShock 4 vía SDL): botón 0 = ✕ (abrir/activar), 13 = cruceta arriba, 14 = abajo, 15 = izquierda, 16 = derecha (con `hat` 1/4/8/2). El botón 1 (○, atrás) aparece usado pero **sigue siendo supuesto** hasta confirmarlo; el resto de índices (2 = recargar, 3 = inicio, 4/5 = página) son supuestos.
- `config.txt` del usuario: `mouseSens=3.50 mouseAccel=1 wheelInvert=0 wheelStep=3 home=`. Marcador guardado: Kintsugi (es.wikipedia).

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

## 6. Arquitectura actual (v0.4.1)
- `main.cpp` (~1300 líneas): pestañas (`std::vector<Browser*> g_tabs; g_cur; #define B (*g_tabs[g_cur])`), barra de herramientas (atrás/adelante/recargar/inicio, omnibox con candado, estrella de marcadores, menú), scroll suave (`scrollTarget` → `scrollY` con suavizado exponencial), scrollbar arrastrable, foco con Tab/cruceta sobre `doc.focus`, envío de formularios GET (POST se envía como GET con aviso), anclas `#id`, `about:home`/`about:settings`/`about:bookmarks`, `<meta refresh>`, página "vídeo no soportado" para `.mp4/.webm/.m3u8`, re-maquetado al cambiar zoom/ancho, caché global de imágenes (~40 MB), log de rendimiento de los primeros 40 frames (`[perf]`). Clic central: enlace en pestaña nueva / cerrar pestaña. Atajos: Ctrl+T/W/PgUp/PgDn, F5, F6.
- `htmldoc.*`: HTML → `Document` maquetado en píxeles (títulos, listas, `<pre>`, tablas simples, imágenes, formularios, anclas, `display:none`, color en línea). `pagerender.*`: dibujo del documento + hit-test. `textengine.*`: TTF con stb_truetype y caché de glifos. `canvas.h`: lienzo ARGB con recorte y mezcla alfa. `theme.h`: temas.
- `imgworker.*`: hilo de descarga+decodificación (PNG/JPEG/BMP con stb_image; **GIF desactivado**; tope 12 Mpx). API: `imgw_start/submit/poll/pending/cancel/pause`. `load()` en main llama `imgw_cancel()` + `imgw_pause(true)` mientras descarga la página. Las URLs `.gif` no se piden.
- `netclient.*`: `http_get` sobre sceHttp/sceSsl con mutex global. `url.*`: resolver/normalizar URLs, percent-encode/decode, desenvolver enlaces de salida de DuckDuckGo. `mouse.*`, `log.h` (`psb_log()` → `/data/psbrowser/log.txt`).
- `fonts_data.cpp` (~600 KB): Gontserrat (sans) y VeraMono (mono) incrustadas. Copias y licencia OFL en `third_party/fonts/`.
- `tools/render_host.cpp`: arnés PC→PNG para ver el render sin consola.
- `.github/workflows/build-pkg.yml`: copia `app/*.cpp|*.h` y `keyboard.*` del sample, `sed` del Makefile (TITLE, TITLE_ID, CONTENT_ID, `COMMONFILES :=`, `LIBS += -lSceNet -lSceSsl -lSceHttp -lSceKeyboard`), `make EXTRAFLAGS=-DPSB_STRICT_TLS`. Título `PS Browser`, Title ID `PSBR00001`, Content ID `IV0000-PSBR00001_00-PSBROWSER0000000`.

## 7. Roadmap acordado (el usuario quiere, en este orden de interés)
1. **Rendimiento (~10 fps → 30–60).** Causa: se compone el lienzo completo 1920x1080 ARGB (~8 MB) en cada frame (~45 ms) y se sube entero con `SDL_UpdateTexture` (~55 ms). Plan: (a) no presentar si nada cambió (`g_dirty`); (b) capa de página en caché: al hacer scroll mover los píxeles existentes y dibujar solo la franja nueva; (c) actualizar la textura por rectángulos sucios (`SDL_UpdateTexture` con `SDL_Rect`); (d) valorar renderizar a 1280x720 y dejar que SDL escale; (e) medir con `[perf]`, separando dibujo de barra y de página.
2. **Teclado en pantalla con el mando.** Overlay modal dibujado con el mismo motor: cuadrícula de teclas, cruceta mueve, ✕ escribe, ○ borra, □ espacio, △ mayúsculas/símbolos, L1/R1 mueve el cursor, Options/Start = Enter; capas minúsculas/mayúsculas/símbolos/acentos (hoy el teclado físico solo escribe ASCII). Se abre al entrar en la omnibox o en un campo de texto con el mando. Alternativa a evaluar más tarde: `sceImeDialog` del sistema (sin verificar que OpenOrbis lo soporte).
3. **Más compatibilidad (sin JS):** User-Agent de navegador y cabeceras `Accept`/`Accept-Language`; cookies básicas; POST real en `netclient` (`Content-Type: application/x-www-form-urlencoded`); gzip/deflate (`Accept-Encoding`; stb trae `stbi_zlib_decode_malloc` utilizable para el cuerpo deflate, gzip requiere parsear la cabecera); charsets distintos de UTF-8 (ISO-8859-1 al menos); **SVG** (nanosvg, header-only, hay que descargarlo); **WebP** (libwebp, hay que descargarlo); CSS básico (colores de fondo, `text-align:center`, anchos, `display`), tablas reales; reactivar **GIF** cuando exista un hilo con pila grande verificada (o parchear la recursión de stb).
4. **Vídeo (riesgo alto).** PKG de prueba aparte que abra un `.mp4` directo (H.264/AAC) con `libSceAvPlayer` y lo muestre; registrar cada paso en el log. Se desconoce si convive con el render por software de SDL y cómo obtener los fotogramas (formato/memoria). Si funciona, los enlaces `[video]` ya detectados lo abrirían. YouTube sin JS solo vía instancias Invidious/Piped (inestables).
5. **Sitios con mucho JavaScript:** servidor intermedio en el PC/móvil del usuario (navegador real que renderiza y devuelve HTML simplificado o imágenes). Proyecto aparte.
6. Descargas, caché en disco, historial persistente; port de NetSurf a largo plazo.

## 8. Cómo compilar localmente (replicando el CI)
1. Descargar y descomprimir el toolchain; `export OO_PS4_TOOLCHAIN=<ruta>/OpenOrbis/PS4Toolchain`.
2. Copiar `samples/` a un temporal, crear `psbrowser/` a partir de `samples/SDL2/` (Makefile, `sce_sys`, `sce_module`), copiar `app/*.cpp app/*.h` y `samples/keyboard/keyboard/keyboard.*`, copiar `third_party/`.
3. `sed` del Makefile como en el workflow (ver arriba), `make eboot.bin EXTRAFLAGS=-DPSB_STRICT_TLS`.
4. Un banco de pruebas en PC (SDL2 + stubs de `orbis/libkernel.h` y `keyboard.h`) sirve para ejecutar la lógica sin consola; `tools/render_host.cpp` renderiza páginas a PNG.

## 9. Limitaciones conocidas
Sin JavaScript, sin vídeo/audio, sin CSS (salvo color en línea y `display:none`), sin cookies/caché/descargas, POST enviado como GET, imágenes solo PNG/JPEG/BMP (sin GIF, SVG ni WebP), carga de páginas bloqueante, teclado solo ASCII al escribir y solo físico, ~10 fps, índices de botones del mando (salvo ✕ y cruceta) supuestos.

## 10. Git (cuando haya cambios)
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party
git commit -m "v0.4.1: arreglo de cuelgue con imagenes; documentacion"
git push
```
Después: Actions → **Build PKG** → *Run workflow* → instalar el PKG → devolver `/data/psbrowser/log.txt` (y el zip `logs` si falla).

## 11. Prompt sugerido para una IA nueva
"Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md` del zip adjunto. v0.4.1 está probada y funciona (pestañas, imágenes PNG/JPEG, formularios GET). Lo siguiente, en este orden: rendimiento (~10 fps), teclado en pantalla con el mando, más compatibilidad con páginas (cookies, POST, gzip, SVG, WebP, CSS básico) y soporte de vídeo (experimento con libSceAvPlayer). Responde en español, sé honesto sobre lo no probado o no compilado, pídeme `log.txt` antes de tocar mapeos de entrada y dame los comandos `git` exactos si cambias el workflow."


## 12. Cambios de v0.5.0 (sin probar en consola)
- **`-O2` en el CI** (`build-pkg.yml`): v0.4.1 compilaba sin ninguna optimización (`flags.txt` solo tenía `-DPSB_STRICT_TLS`). Es la sospecha principal de los ~10 fps. Comprobar en el log los `[perf]` (antes: dibujo ~45-50 ms, subida ~50-60 ms).
- **Teclado en pantalla** (`app/osk.h/.cpp`, integrado en `main.cpp`): capas letras / símbolos / acentos, cursor por caracteres UTF-8, modo contraseña. Se abre al activar con ✕ un campo de texto, y con los botones 8 o 9 abre la barra de direcciones. Mapeo con el teclado abierto: 0 ✕ escribir, 1 ○ borrar (vacío = cerrar), 2 □ espacio, 3 △ mayúsculas, 4/5 L1/R1 cursor, 9 = Enter, cruceta mueve. **Los índices 2-5, 8 y 9 son supuestos**: confirmar con el log (`boton N`).
- Comprobado solo en PC: sintaxis de `main.cpp` con stubs de SDL y lógica de `osk.cpp` con sanitizers. No se ha compilado con el toolchain real.
- Pendiente del roadmap: rendimiento por rectángulos sucios / capa en caché, cookies/POST/gzip/charsets/SVG/WebP/CSS, vídeo.
