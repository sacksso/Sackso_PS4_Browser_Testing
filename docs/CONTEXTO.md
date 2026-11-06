# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva junto con el zip del repo (y, si los tienes, el último `log.txt` y el zip `logs` del CI). Idioma de trabajo: **español**.
> Última actualización: 6 de noviembre de 2026 (v0.5.1 compilable en PC con stubs; **v0.5.0 probada en PS4 real**).

## 1. Qué es
Navegador homebrew para **PS4 con GoldHEN**, hecho con el toolchain **OpenOrbis** (LLVM, `x86_64-pc-freebsd12-elf`).
No usa WebKit ni JavaScript: es un visor propio de HTML estático (texto, enlaces, imágenes, formularios) con interfaz de "navegador de escritorio" (pestañas, barra, tema claro/oscuro, scroll suave, teclado en pantalla para el mando).
Objetivo a largo plazo: más compatibilidad, vídeo; plan B/C: portar NetSurf (`docs/NETSURF.md`) o un servidor intermedio para sitios con JavaScript. Proyecto personal/aprendizaje.

## 2. Cómo trabajamos (reglas del usuario)
- La IA escribe código y workflows; el usuario sube a GitHub, ejecuta el CI (GitHub Actions, workflow manual `build-pkg.yml`), instala el PKG en su PS4 real y devuelve `/data/psbrowser/log.txt` (a veces `last.html`, `config.txt`, `bookmarks.txt` y el zip `logs` del CI).
- Si cambia un workflow, dar los comandos `git` exactos. No añadir al commit carpetas de desarrollo sin seguimiento.
- Ser honesto sobre lo que **no** se ha probado en consola. Pedir el log antes de cambiar mapeos de entrada.
- Repo del usuario: `Sackso_PS4_Browser_Testing` (GitHub). Proyecto anterior reutilizado: `sacksso/Sackso_Ren.py_ps4-testing`.
- Consejos prácticos para la IA:
  - Los archivos de chat/log que sube el usuario pueden venir en **UTF-16** (BOM `ff fe`): decodificar con `utf-16`, no con `cat`.
  - Si el entorno tiene red: descargar el toolchain (`https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz`, 158 MB), `apt install clang lld` y **compilar de verdad** replicando el CI (ver §8). Si no hay red (lo normal hasta ahora), decirlo: se comprueba solo con **stubs** de SDL/orbis (`g++ -fsyntax-only -I<stubs>`) y pruebas de lógica en PC; el CI es la comprobación real. Los stubs de `SDL2/SDL.h`, `keyboard.h`, `orbis/{Http,Net,Ssl,Sysmodule,libkernel}.h` y `stb/*.h` hay que recrearlos (son mínimos: declaraciones con las firmas que usa el código).
  - `/bin/sh` es dash (sin brace expansion).
  - El workspace de la IA es efímero (`/tmp` se pierde al cerrar el chat): todo lo importante va al zip que se entrega.

## 3. Estado por versiones
| Versión | Contenido | Estado |
|---|---|---|
| 0.1.2–0.3.0 | Red HTTP/HTTPS con `sceHttp`, visor, enlaces, marcadores, ajustes | Probadas |
| 0.4.0 | Rediseño de escritorio (TTF, tema, pestañas, scroll suave, imágenes, formularios GET) | Se colgó en Wikipedia (GIF/hilo) |
| 0.4.1 | Arreglo del cuelgue (GIF desactivado, pausa/cancelación del hilo de imágenes) | Probada: bien, pero ~10 fps |
| **0.5.0** | 0.4.1 + **`-O2` en el CI** + **teclado en pantalla** con el mando (`osk.*`) | **Probada en PS4: "el teclado funciona excelente, el rendimiento se siente excelente"** |
| **0.5.1** | 0.5.0 + gzip/deflate, charsets ISO-8859-1/Windows-1252, POST real, cabeceras Accept*, inventario del toolchain en el CI | **Sin probar en consola.** Es el siguiente PKG a instalar |

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

- **gzip:** `sceHttp` NO descomprime solo (hipótesis no confirmada); v0.5.1 pide `Accept-Encoding: gzip, deflate`, descomprime con `inflate.cpp` y, si falla, reintenta en modo compat (`identity`). Revisar en el log las líneas `[http] Content-Encoding=...`.

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

## 7. Roadmap (el usuario quiere, en este orden de interés)
1. ~~Rendimiento~~ y ~~teclado en pantalla~~: hechos y aprobados. Opcional más adelante: rectángulos sucios para pasar de ~15 a 30+ fps.
2. **Compatibilidad con páginas (sin JS)** — en curso:
   - Hecho en 0.5.1 (sin probar): gzip/deflate, charsets latinos, POST, cabeceras `Accept`.
   - Siguiente: **cookies** básicas (jar en memoria por dominio, `Set-Cookie`/`Cookie`; con `sceHttp` hay que leer `Set-Cookie` de `GetAllResponseHeaders` y añadir `Cookie` a mano); **SVG** (nanosvg header-only: que el CI lo descargue con `curl` fijando versión, y compilar con `-DPSB_NO_SVG` si falla la descarga); **WebP** (libwebp); **GIF** (reactivar cuando haya hilo con pila grande o parchear la recursión de stb); **CSS básico** (colores de fondo, `text-align`, anchos, `display`, `<style>` simple); tablas reales; User-Agent configurable en `config.txt` (`ua=`); `multipart/form-data`; `<select>` visible; favicons `.ico`.
3. **Vídeo (riesgo alto).** Primero leer `logs/toolchain_inventory.txt` del CI para saber si el toolchain trae `libSceAvPlayer`/cabeceras. Luego un PKG de prueba aparte que abra un `.mp4` directo (H.264/AAC) con `libSceAvPlayer`, registrando cada paso en el log. Se desconoce si convive con el render por software de SDL y cómo obtener los fotogramas. Si funciona, los enlaces `[video]` ya detectados lo abrirían. YouTube sin JS solo vía instancias Invidious/Piped (inestables). Alternativa: servidor intermedio que transcodifique.
4. **Sitios con mucho JavaScript:** servidor intermedio en PC/móvil del usuario. Proyecto aparte.
5. Descargas, caché en disco, historial persistente; NetSurf a largo plazo.

## 8. Cómo compilar localmente (replicando el CI)
1. Descargar y descomprimir el toolchain; `export OO_PS4_TOOLCHAIN=<ruta>/OpenOrbis/PS4Toolchain`.
2. Copiar `samples/` a un temporal, crear `psbrowser/` a partir de `samples/SDL2/` (Makefile, `sce_sys`, `sce_module`), copiar `app/*.cpp app/*.h` y `samples/keyboard/keyboard/keyboard.*`, copiar `third_party/`.
3. `sed` del Makefile como en el workflow (ver arriba), `make eboot.bin EXTRAFLAGS=-DPSB_STRICT_TLS`.
4. Un banco de pruebas en PC (SDL2 + stubs de `orbis/libkernel.h` y `keyboard.h`) sirve para ejecutar la lógica sin consola; `tools/render_host.cpp` renderiza páginas a PNG.

## 9. Limitaciones conocidas
Sin JavaScript, sin vídeo/audio, sin CSS (salvo color en línea y `display:none`), sin cookies/caché/descargas, imágenes solo PNG/JPEG/BMP (sin GIF, SVG, WebP ni ICO), carga de páginas bloqueante, ~15 fps, charsets no latinos sin soporte, formularios sin multipart, índices de botones del mando 8/9/18/19 sin confirmar.

## 10. Git (cuando haya cambios)
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party .github
git commit -m "v0.5.1: gzip, charsets, POST real, cabeceras y inventario del toolchain"
git push
```
Después: Actions → **Build PKG** → *Run workflow* → instalar el PKG → devolver `/data/psbrowser/log.txt` (y el zip `logs` aunque compile bien: trae `toolchain_inventory.txt`).

## 11. Prompt sugerido para una IA nueva
"Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md` del zip adjunto. v0.5.0 está probada y funciona muy bien (pestañas, imágenes PNG/JPEG, formularios, teclado en pantalla, rendimiento aprobado). v0.5.1 (gzip, charsets, POST) está sin probar. Lo siguiente: verificar v0.5.1 con mi log, seguir con compatibilidad (cookies, SVG, WebP, GIF, CSS básico) y después el experimento de vídeo con libSceAvPlayer. Responde en español, sé honesto sobre lo no probado o no compilado, pídeme `log.txt` antes de tocar mapeos de entrada y dame los comandos `git` exactos si cambias el workflow."
