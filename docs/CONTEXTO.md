# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva junto con el zip del repo, `PLAN_MAESTRO.md` (si lo tienes) y el último `log.txt` y zip `logs` del CI. Idioma de trabajo: **español**.
> Última actualización: 9 de noviembre de 2026, noche (resultados de v0.9.5 en PS4 + sonda Piglet; antes: **v0.9.5, tandas 1 y 2: sondeo SDL/GPU + opción `gpu` + Canvas 2D + modo juego**). Versiones v0.9.0–v0.9.5: **ninguna probada aún en PS4 salvo el log de v0.9.0/0.9.1 ya analizado**; v0.9.3 y v0.9.4 no se han compilado en el CI ni probado. El último log recibido es de v0.9.1. Las secciones numeradas §1–§11 y los bloques v0.8.x de más abajo son **históricos**: donde digan «sin JavaScript», «sin DOM/CSS» o «descargas de un solo flujo» ya no es cierto. Lo vigente por versión está en `docs/V0.9.x.md`; el plan por fases en `PLAN_MAESTRO.md`.

# >>> v0.9.6 (COMPATIBILIDAD JS PARA JUEGOS + SONDAS CI) - SIN PROBAR EN PS4 - leer primero <<<
> 9-nov-2026, noche. Detalle en `docs/V0.9.6.md`. Solo cambia el prelude JS (NodeFilter/TreeWalker/NodeIterator, Path2D, audio silencioso con `AudioContext` stub, stubs de pantalla completa), `tools/tests/test_prelude_v096.js`, la cadena de version y el workflow (sondas). Probado en PC con node (NO con QuickJS real).
## Que pedir al usuario tras instalar
1. Zip `logs` del CI con `probe_sdl` marcado: **`piglet_probe.txt`** (lo necesito para el renderer GPU `gpu=2`) y `sdl_probe.txt`.
2. `log.txt`: primera linea `== psbrowser v0.9.6 ...`; si hay `[js] ERROR`, ver si desaparecieron `NodeFilter is not defined`; probar un juego sencillo de friv y devolver `[game]`/`[canvas]` + que se vio.
3. Si un sitio que antes iba deja de ir: sospechar del `AudioContext` stub (ver V0.9.6.md).
## Git (v0.9.6)
```bash
cd Sackso_PS4_Browser_Testing-main
git add app/jsprelude.h app/main.cpp tools/tests/test_prelude_v096.js .github/workflows/build-pkg.yml docs
git commit -m "v0.9.6: compat JS para juegos (TreeWalker, Path2D, audio stub) y sondas CI"
git push
```
Luego Actions -> Build PKG -> Run workflow con `probe_sdl` y `js_engine` marcados.
---
# >>> RESULTADOS v0.9.5 EN PS4 (9-nov-2026, noche) + SONDA PIGLET <<<
> El usuario instalo v0.9.5 (compila: el CI no cayo a `js_fallback`/`canvas_fallback`) y devolvio `log.txt` + zip `logs`.
## Hechos medidos
- Log: primera linea `== psbrowser v0.9.5 (DOM+CSS+JS+Canvas2D+GPU probe) ==`, `[jsx] motor JS: QuickJS | js=2`, 99 cookies cargadas. Sesion: DDG, archive.org (listados y un ZIP de 678 MB), nintendolife (verificacion).
- `[gpu]`: 1 controlador de video ("PS4"), **1 controlador de render = `software`** (flags 0x9, textura max 0x0). `gpu=1` por SDL no puede ayudar.
- Rendimiento UI: dibujo 26 ms (pagina interna) + subida/presentacion ~52 ms por fotograma a 1080p => ~12 fps al hacer scroll.
- JS en archive.org: `NodeFilter is not defined` (webcomponents-bundle), `TypeError: not a function` (archive.min.js, 755 KB), `interrupted` (limite de CPU del temporizador). Pagina de ~945 KB: 6,7 s la 1.a vez, ~1 s la 2.a (cache).
- Descargas: el ZIP de archive.org por trozos de 16 MB: pico 3,49 MB/s, media 2,31 MB/s (antes 0,65 MB/s por conexion).
- `docs/canvas_test.html` (alojada en GitHub Pages por el usuario): **segun el usuario "todo salio ok"** (no se vio la lista literal ni los fps del modo juego). No hay lineas `[game]`/`[canvas]` en el log recibido (la prueba fue otra sesion/ no se envio ese log).
## sdl_probe.txt (CI)
- SDL 2.0.9. Libs: `libSceGnmDriver.so`, `libSceGnmDriverCompat.so`, `libScePigletv2VSH.so` (EGL/GLES del sistema), `libSceVideoOut*.so`, `libSceVdec*`. Include: `orbis/GnmDriver.h`, `orbis/VideoOut.h`, `SDL_opengles2*.h`. Samples: **`piglet`**, `graphics`, `SDL2`...
- `libSDL2.a` contiene simbolos `SW_RenderDriver` y `PS4_RenderDriver/PS4_CreateRenderer`, pero en ejecucion SDL solo lista `software`. Las cadenas `gnm`/`vulkan` que aparecen son solo coincidencias de texto (sin interpretar).
## Hipotesis (SIN verificar) y siguiente paso
- Hay un camino real a la GPU: **Piglet = EGL + OpenGL ES 2** del sistema, con muestra oficial en el toolchain. Plan: renderer experimental `gpu=2` que cree contexto EGL, suba el lienzo 1920x1080 como textura GLES2 y dibuje un quad (y el video NV12/RGBA como textura), sustituyendo los ~52 ms de presentacion por software. Riesgos: modulo sprx del sistema, memoria de GPU, interaccion con SDL (que tambien abre VideoOut: probablemente haya que prescindir de SDL_Window en ese modo), cuelgues. Hay que reaprovechar la proteccion `gpu_try.txt`.
- **Paso inmediato (hecho en el repo, sin compilar):** el workflow tiene una sonda nueva "Sonda de Piglet (GLES2) y GNM" (con `probe_sdl` marcado) que escribe `logs/piglet_probe.txt`: archivos y fuente del sample `piglet`, Makefile, cabeceras EGL/GLES, simbolos de `libScePigletv2VSH.so` y funciones de `GnmDriver.h`. **Pedir ese archivo al usuario antes de escribir una linea del renderer.**
- Aunque funcione, WebGL/WASM siguen siendo irreales (QuickJS sin JIT); la GPU acelera presentacion, escalado de video y canvas 2D, no el JavaScript.
## Git (solo cambia el workflow)
```bash
cd Sackso_PS4_Browser_Testing-main
git add .github/workflows/build-pkg.yml docs/CONTEXTO.md
git commit -m "CI: sonda de Piglet/GNM; contexto con resultados de v0.9.5"
git push
```
Despues: Actions -> Build PKG -> Run workflow con `probe_sdl` marcado -> descargar `logs` y enviar `piglet_probe.txt`.
---
# >>> v0.9.5 (SONDEO SDL/GPU + CANVAS 2D + MODO JUEGO) - SIN PROBAR EN PS4 - leer primero <<<
> 9-nov-2026. Detalle en `docs/V0.9.5.md`. El usuario decidió **no probar v0.9.4 y saltar a v0.9.5** para testear a fondo (se le avisó de que apilar versiones sin probar dificulta el diagnóstico; el CI puede fallar por varias versiones a la vez: pedir `build.log`). Objetivo declarado: aprovechar al máximo la potencia general y de render, y más compatibilidad (más webs, juegos y utilidades web, ports a navegador).
## Qué hay (primera tanda)
- Sondeo de lo que ofrece SDL: CI `probe_sdl` → `logs/sdl_probe.txt`; en consola líneas `[gpu]` en `log.txt` y sección en `about:bench`.
- Config `gpu` (0 = software, por defecto; 1 = intentar renderer ACELERADO solo si SDL declara uno). Ajustes → Renderizado (`psb:gpu`, se aplica al reiniciar). Protección: `/data/psbrowser/gpu_try.txt`; si la app cierra antes de 30 s con gpu=1, el siguiente arranque vuelve a `gpu=0`. Con renderer acelerado `g_partFailed = true` (sin `UpdateWindowSurfaceRects`).
## Qué pedir al usuario tras instalar
1. Zip `logs` del CI: `sdl_probe.txt` (¿trae backends distintos de software? ¿algo de GNM?) y `build.log` si falla (puede fallar por v0.9.3/0.9.4, no solo 0.9.5).
2. `log.txt`: primera línea `== psbrowser v0.9.5 ...`, todas las líneas `[gpu]`, `[net] listo`, `[jsx]`. Luego repetir con `gpu=1` (dejar 30 s abierto) y devolver `[gpu]` + `about:bench` (F9).
3. Lo pendiente de v0.9.4 (Paso 0 del PLAN_MAESTRO): firewall 8765, `/ping`, un sitio sin captcha y uno con captcha, `vscale` 3/4 con fps.
## Tanda 2 (hecha, sin probar): Canvas 2D + modo juego (detalle completo y limitaciones en `docs/V0.9.5.md`)
- `app/canvas2d.cpp/.h`: rasterizador propio (sin SDL/QuickJS; probado en PC: `tools/tests/test_canvas2d.cpp`). `app/jsx.cpp`: natives `__c.*` (create, resize, op, str, image, getPixels, putPixels) bajo `#ifndef PSB_NO_CANVAS`, `jsx_eval()`, `jsx_set_game_mode()`. `app/jsprelude.h`: bloque "Canvas 2D (v0.9.5)" (Ctx2D, CanvasGradient, ImageData, Image con `src`, eventos de entrada `__psbKey/__psbMouse/__psbPad`) y **`requestAnimationFrame` ahora pasa la marca de tiempo**. `app/imgworker.cpp`: `imgw_decode_native()`. `app/main.cpp`: modo juego (F2 / menú contextual; `gameEnter/gameExit/gameEvent/gameKeys/gameMouse/gameTick/gamePresent`), ajuste `gscale`, `host.decodeImage`.
- **Los opcodes de `C2dOp`/`C2dStr` (canvas2d.h) deben coincidir con `O`/`S` del prelude**: lo comprueba `tools/tests/test_canvas_prelude.js`; si se añade un opcode, tocar los dos y correr el test.
- CI: tres niveles de compilación: con JS+Canvas -> con JS sin Canvas (`-DPSB_NO_CANVAS`, `logs/canvas_fallback.txt`) -> sin JS (`-DPSB_NO_JS`, `logs/js_fallback.txt`). Si falla `canvas2d.cpp`/`main.cpp`/`imgworker.cpp` (C++ puro, sin interruptor) fallan los tres: pedir `build.log`.
- Verificación de sintaxis sin QuickJS: `g++ -std=c++17 -fsyntax-only -Itools/stubs_qjs -Itools/stubs -Iapp app/jsx.cpp` (el stub de `quickjs.h` es mínimo, no la API real).
- **Sin verificar**: enlace QuickJS<->canvas (`tools/tests/test_jsx_canvas.cpp` lo prueba con QuickJS real), mapa del mando (suposición), velocidad en el Jaguar, decodificación de stb en consola.
- Página de prueba: `docs/canvas_test.html` (alojarla en un servidor; GitHub Pages sirve).
## Qué pedir al usuario tras la tanda 2
`logs.zip` del CI (¿`canvas_fallback.txt`/`js_fallback.txt`?), `log.txt` con `[game]` (botones/ejes reales, textura, modo) y `[canvas]` (descargas de imágenes), resultado de `canvas_test.html` (lista OK/FALLA) y fps que dice la página en modo juego con `gscale` 1:1 y ajustado.
## Siguiente (tanda 3, no hecha)
Audio (`HTMLAudioElement` real / WebAudio mínimo), `Path2D`, texto con las fuentes del motor de texto, rueda/táctil hacia la página, optimizar con datos reales; decidir con el sondeo GPU si hay camino por GNM. WebGL/WASM no son realistas con QuickJS sin JIT.
## Git (v0.9.5)
Cambiados/nuevos: `app/main.cpp`, `app/jsx.cpp/.h`, `app/jsprelude.h`, `app/imgworker.cpp/.h`, `app/canvas2d.cpp/.h` (nuevos), `tools/stubs/SDL2/SDL.h`, `tools/stubs_qjs/quickjs.h` (nuevo), `tools/tests/test_canvas2d.cpp`, `test_canvas_prelude.js`, `test_jsx_canvas.cpp` (nuevos), `.github/workflows/build-pkg.yml` (**SÍ cambia**), `docs/*` (incl. `canvas_test.html`), `PLAN_MAESTRO.md`.
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party .github README.md PLAN_MAESTRO.md
git commit -m "v0.9.5: sondeo SDL/GPU, Canvas 2D y modo juego"
git push
```
Después: Actions → **Build PKG** → *Run workflow* (`probe_sdl` y `js_engine` marcados) → instalar → probar.
---
# >>> v0.9.2 (CAPTCHAS/DESCARGAS DINAMICAS + !media + VIDEO) - SIN PROBAR EN PS4 - leer primero <<<
> 9-nov-2026. Detalle y tabla log->causa->cambio en `docs/V0.9.2.md`. Log recibido: v0.9.1. Cambios: compareDocumentPosition/initEvent/template.content en el prelude, POST en fetch/XHR (`JsHost::request`), presupuesto de carga solo CPU, ayudante PC protocolo 2 (`auto=1`, `/media`, `/status`), comando `!media`, `vscale` 3/4, log `[perfv] via LENTA`. Pendiente: confirmar con log que jQuery ya no falla, que mocha/akirabox hacen POST, que el ayudante conecta (0x80431068 antes), y por que la via rapida de video no se usaba. Sin soporte aun: modulos ES, Turnstile en PS4, HLS verificado, layout flex.
---
# >>> v0.9.1 (CORRECCIONES TRAS EL LOG DE v0.9.0 + COOKIES + CAPTCHAS RESUELTOS EN EL PC) - SIN PROBAR EN PS4 - leer primero <<<
> 9-nov-2026. Detalle completo y tabla log→causa→arreglo en `docs/V0.9.1.md`. **Probado en PC (QuickJS real, jQuery real, ASan/UBSan/TSan); NO compilado con el toolchain real ni probado en consola.** El log recibido era de **v0.9.0** (primera linea `== psbrowser v0.9.0 (DOM+CSS+JS) ==`); el CI compilo bien (QuickJS incluido, sin `js_fallback.txt`).
## Datos reales del log de v0.9.0 (PS4, WiFi)
- JS: 196 errores `[js] ERROR`, casi todos por `document.implementation` ausente (jQuery) + `ShadowRoot`/`MediaError`/`crypto`/`NodeList`. Cargas con JS: 4-32 s (scripts de publicidad bajados uno a uno).
- Descargas: `about:dlbench` en thinkbroadband: **1 conexion 0,65 MB/s, 2 → 1,29, 4 → 2,58** (escala casi lineal: limite por conexion); disco 110-130 MB/s (1 flujo) y ~70 (4 flujos). proof.ovh.net: **HTTP 429 con 2 conexiones** (`Retry-After: 10`). speed.hetzner.de: `sendRequest 0x804101E9`. **archive.org con `dlconn=6`: `sendRequest 0x809517D5` en bucle, 0,18 MB/s, fallo** (hipotesis: pools SSL/HTTP pequeños).
- Navegacion: el usuario visita sitios que piden verificaciones (`Just a moment...` de Cloudflare en spankbang y timeextension, captchas en paginas de descarga de PKG, 1fichier, mocha...). El Set-Cookie llega en `[hdr]` (visible en archive.org) pero **no habia cookies**.
## Que cambia
1. **JS compat:** `document.implementation`, `DOMParser`, `ShadowRoot`, `NodeList/HTMLCollection`, `MediaError`, `crypto`, `TextEncoder/Decoder`, eventos extra, `attributes` con acceso por nombre; setInterval que falla se cancela a los 3 errores. `tools/tests/test_jsx_compat.cpp` (uso: `test_jsx_compat <jquery.js> ...`, jQuery 1.12.4/2.2.4/3.5.1/3.7.1 OK).
2. **Red:** pools SSL 4 MB / HTTP 8 MB / NET 128 KB; cabeceras de 32 KB en descargas; `HttpResult` con `retryAfterMs`, `server`, `cfChallenge`.
3. **Descargas:** `dlconn` = MAXIMO (1..12, por defecto **8**; ciclo de ajustes 1,2,4,6,8,12,0). Empieza con 2 y sube (ventanas de 4 s, +12 %); 429/503 no son fatales (espera `Retry-After`, baja a `en_curso-1`, aparca trabajadores). El medidor `about:dlbench` sigue con conexiones fijas.
4. **Cookies:** `app/cookies.h/.cpp` (puro, probado), integrado en `netclient.cpp` (cabecera `Cookie`, `Set-Cookie`), `document.cookie`, `cookies.txt` Netscape, `cookies_import.txt` (se importa al arrancar y desde `about:cookies`), UA por dominio (`ua_overrides.txt`). Config nueva: `cookies`, `adblock`, `solver`.
5. **Adblock + cache** de scripts/CSS en el host JS (`adBlocked()`, `g_assetCache` 24 MB, solo .js/.css).
6. **Verificaciones:** `looksLikeChallenge()` / `pageHasCaptcha()` en `load()` → `about:verify` (F7) con los pasos; ayudante `tools/solver/psb_solver.py` (Playwright) con protocolo `/ping`, `/open?url=`, `/state` (`#UA`, `#DL`, cookies Netscape); `solverSync()/solverOpen()` en main.cpp. **El usuario resuelve a mano; nada automatico.**
7. **Atajos:** F7, Ctrl+U (`about:source`), Ctrl+Mayus+Supr (`about:cookies`), Alt+D; comandos de la barra `!solver IP:puerto`, `!ua dominio UA`, `!cookies`, `!verify`, `!source`.
## Riesgos (decirlos al usuario)
Ver `docs/V0.9.1.md` → «Limites y riesgos». Los mas importantes: pools no probados en consola; el JS de Cloudflare/hCaptcha NO se ejecuta (por eso la via es el PC); `psb_solver.py` no probado con un navegador real; cookie `cf_clearance` atada a UA e IP publica.
## Que pedir al usuario tras instalar
1. Primera linea del log `== psbrowser v0.9.1 ==`; lineas `[net] listo` (que los pools nuevos no fallen) y `[cookies] N cookies cargadas`.
2. Misma prueba de descarga que archive.org (la que fallo con 6): buscar `[dleng] velocidad total ... se sube a N` / `el servidor limita las conexiones` y la velocidad final `[dl] motor:`. Si sigue `0x809517D5`, devolver el log + zip `logs`.
3. Paginas con jQuery (WordPress/erome): `[jsx] DOM ok, N scripts (K con error)` deberia bajar mucho; tiempos `ms`.
4. Una pagina con verificacion de Cloudflare: F7 → `!solver` → flujo completo con `psb_solver.py` en el PC (instalar Playwright). Devolver el log (`[verify]`, `[solver]`, `[cookies]`) y si la pagina abre tras importar.
## Git (v0.9.1)
Archivos nuevos: `app/cookies.h/.cpp`, `tools/solver/*`, `tools/tests/test_cookies.cpp`, `test_jsx_compat.cpp`, `test_solver_state.cpp`, `docs/V0.9.1.md`. Cambiados: `app/main.cpp`, `app/netclient.*`, `app/dlengine.*`, `app/jsx.*`, `app/jsprelude.h`, `tools/tests/test_dlengine.cpp`, `tools/README.md`, `docs/*`. **El workflow NO cambia** (copia `app/*.cpp` y `app/*.h`).
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools README.md
git commit -m "v0.9.1: compat jQuery, cookies, descargas adaptativas, verificaciones resueltas en el PC"
git push
```
Despues: Actions → **Build PKG** → *Run workflow* → instalar → probar.
---
# >>> v0.9.0 (PRESENTACION PARCIAL + DOM/CSS/JS + MOTOR DE DESCARGAS + MEDIDOR) - SIN PROBAR EN PS4 - leer primero <<<
> 8-nov-2026. Entregado como `psbrowser-v0_9_0.zip` (mismo nombre en dos entregas seguidas: **la segunda** ya incluye el motor de descargas, `app/dlengine.*`; si el zip del usuario no lo tiene, es la primera). **Nada de esto se ha compilado con el toolchain real ni probado en consola.** Lo que sí se hizo: compilación en PC con `g++`, pruebas con QuickJS real y servidor de descargas simulado (ASan/UBSan/TSan limpios) y `-fsyntax-only` de `main.cpp` con los stubs de `tools/stubs`.

## Datos reales que motivaron la versión (foto de `about:bench`, v0.8.3 en PS4)
- Solo `Present` / limpiar+Present: **16,7 ms (60 fps, tope del vsync)**. Copia 864x480 a 1:1 + Present: **16,7 ms**. `UpdateWindowSurfaceRects` (cualquier tamaño) y `UpdateWindowSurface`: **16,7 ms**.
- Copia de textura **1920x1080** + Present: **50 ms (20 fps)**; copia 864x480 **escalada** a pantalla completa: **50 ms**. Conclusión: el coste NO es presentar, es **rellenar ~2 M de píxeles** (~33 ms de más). Hipótesis sin confirmar: framebuffer lento de escribir (memoria sin caché).
- Config del usuario entonces: `vscale=0`, `vpart=0`, `vdec=0`, `vhalf=1`, `pageW=1200`, `smooth=1`, `images=1`, `dark=1`. Sus pruebas: navegó webs «random» con el navegador como de PC para recoger logs, sobre todo del reproductor de vídeo (el log v0.8.3 no traía líneas `[bench]`; la foto sí).

## Qué añade v0.9.0
**1. Presentación (`main.cpp`: `present()`, `presentUiPartial()`, `presentVideoFast()`)**
- `uipart` (config, por defecto **1**; ajuste `psb:uipart`): compara el lienzo con el fotograma anterior (`g_uiPrev`), sube/copia solo el rectángulo que cambió y llama a `SDL_UpdateWindowSurfaceRects`; el cursor se restaura/pinta aparte; cada 3 s fuerza un fotograma completo; si `SDL_UpdateWindowSurfaceRects` falla se desactiva (`g_partFailed`, log `[perfui]`). **Supone que la superficie de la ventana NO es de doble búfer** (persiste entre fotogramas): si parpadea o deja restos, desactivarlo.
- `vpart` ahora funciona **con ratón conectado** (borra a negro el cursor anterior fuera del vídeo). Recomendado probar `vscale=1` (1:1, centrado) y `vpart=1`.
**2. DOM + CSS lite + JavaScript (`app/dom.*`, `app/jsx.*`, `app/jsprelude.h`)**
- Flujo: HTML → `Dom::parse` (parser tolerante, entidades) → CSS externos (máx. 5, 400 KB) → scripts (inline + externos, máx. 12, 1,5 MB c/u) → `DOMContentLoaded`/`load` → `Dom::serialize` (omite `display:none`/`visibility:hidden`/`hidden`; pasa `color` en línea) → `parseHtml` de siempre (**`htmldoc.cpp` no se tocó**). Los temporizadores/rAF/promesas se mueven con `jsx_pump()` desde el bucle principal; si el DOM cambia re-maqueta la pestaña (máx. cada 120 ms; se omite mientras se edita un campo/URL/OSK).
- Config `js` (0 = maquetador original sin DOM; 1 = DOM + CSS lite; **2 = + JavaScript, por defecto**; `psb:js`). El HTML original se conserva en `g_rawHtml` para detectar vídeos (los scripts no se serializan).
- CSS lite: selectores tag/#id/.clase/[attr op valor]/`:not`/`:first-child`/`:last-child`/`:nth-child`/combinadores ` > + ~`/listas; propiedades `display`, `visibility`, `color`; especificidad, orden y `!important`; `@media` se ignora; pseudoclases dinámicas/pseudoelementos nunca coinciden. Con motor JS activo se oculta `<noscript>`.
- JS: QuickJS-ng **v0.18.0**; prelude en JS (`jsprelude.h`) sobre primitivas nativas `__d` (DOM) y `__n` (sistema). APIs: `document`/`Element` (atributos, `classList`, `style`, `dataset`, `innerHTML`/`outerHTML`, `querySelector(All)`, `matches`/`closest`, `insertAdjacentHTML`, `createElement`, `document.write`), eventos (`addEventListener`/`dispatchEvent`/`click()`, burbujeo), `setTimeout/Interval`, `requestAnimationFrame` (~16 ms), Promesas, `fetch`/`XMLHttpRequest` (**solo GET, bloqueantes, máx. 10 por página, 1 MB**), `localStorage` (memoria), `URL`/`URLSearchParams`, `location`, `navigator`, `console` (→ log como `[js]`). Límites: 64 MB (asignador propio con cabecera de tamaño), pila 512 KB, 2,5 s por script, 250 ms por temporizador, 8 s de presupuesto de carga (el tiempo de red no cuenta). Un script que falla no para a los demás.
- **No existe aún:** `<canvas>` 2D (`getContext` → `null`), módulos ES (`type=module` se salta), eventos reales de ratón/mando hacia la página (hace falta mapa nodo↔línea del maquetador), `getBoundingClientRect` (devuelve ceros), WebGL, WebAudio, `window.location=` (navegación por JS), cookies de `fetch`.
- Compilación (CI, `.github/workflows/build-pkg.yml`, input `js_engine` por defecto true): clona `quickjs-ng` `v0.18.0`, copia `quickjs/dtoa/libregexp/libunicode.c` como `psbrowser/qjs_*.c` (con `#define NDEBUG` y `#define __STDC_NO_ATOMICS__ 1` antepuestos: sin Atomics no necesita pthread), copia sus `.h`, `third_party/qjs_shim/malloc_np.h` (stub: en OpenOrbis falta `<malloc_np.h>`) y `psb_qjs_shim.c`, y compila con `-Ipsbrowser`. **Si el build con JS falla, reintenta con `-DPSB_NO_JS`** (solo DOM + CSS lite) y deja `logs/js_fallback.txt` y `logs/build_js_failed.log`. `tools/build_local.sh` acepta `QJS_DIR=<ruta a quickjs>`.
**3. Descargas (`app/dlengine.*`, `asyncwriter.*`, `netclient.*`, `main.cpp`)**
- `DlEngine` (motor por trozos, sin dependencias de red: recibe un `DlFetchFn`): trozos de 16 MB con `Range`; cada trozo completo se anota en `<archivo>.part.meta` (url, ruta final, total, trozo, lista de trozos completos) **solo cuando ya está vaciado a disco** (`AsyncWriter::waitWritten`); reintentos del trozo desde el último byte (hasta 6 seguidos sin avanzar, espera creciente, vuelve a la URL original tras 2 fallos); `conns` trabajadores toman trozos de una cola común; escritura por posición con búfer de 2 MB por trabajador; archivo reservado al tamaño final (`fseek`+`fputc`). Servidor que responde 200 a `Range` → flujo único (como antes, sin reanudar); si al reanudar el servidor ya no admite Range o cambió el total → empieza de cero. Modo `discard` para el medidor.
- Reanudar: al descargar la **misma URL** continúa; lista de incompletas en `about:downloads` ([continuar]/[borrar], acciones `psb:dlresume:N`, `psb:dldrop:N`). Índice propio `<dir>/.partials` (**no se usa `opendir/readdir`**: no se usa en ningún otro punto del proyecto y podría no enlazar en OpenOrbis).
- Config `dlconn` (por defecto **1**; 0 = **motor antiguo completo** `runDownloadLegacy`, de respaldo; 2/3/4/6 = paralelo; `psb:dlconn`). Con varias conexiones: `HttpGateLock` (toma el cerrojo global de red una vez) + `http_request_nolock` en cada hilo.
- Medidor `about:dlbench` (`psb:dlbench:N`, N=0 última URL descargada, 1–3 servidores públicos **no verificados**: speed.hetzner.de, ipv4.download.thinkbroadband.com, proof.ovh.net): red con 1/2/4 conexiones (8 s c/u, sin guardar) + disco (64 MB secuencial y en 4 flujos intercalados), min/avg/max MB/s por segundo, reintentos, recomendación y `psb:dlconnset:N`. Logs: `[dlbench]`, `[dleng]`, `[dl] motor`.
- No incluido: instalar PKG desde el navegador (se guarda en `/data/psbrowser/downloads/`; instalar con el instalador del usuario), descomprimir ZIP, SHA-256, ETag/`If-Range` (hoy solo se compara el tamaño total), cola de varias descargas, pausa explícita (cancelar conserva lo descargado).

## Verificación hecha (SIN toolchain real, SIN consola)
- `g++ -fsyntax-only -Itools/stubs -Iapp app/main.cpp` limpio (stub de `SDL_RenderFillRect` añadido); `jsx.cpp` limpio con y sin `-DPSB_NO_JS`.
- `tools/tests/test_jsx.cpp` (DOM, CSS externo, scripts externos, `fetch`, timers, promesas, eventos, errores aislados) y `tools/tests/test_dlengine.cpp` (limpia 1/3/6 conexiones, cortes de red, sin Range, cancelar/reanudar, reanudar contra servidor sin Range, 404, HTML en vez de archivo, reintentos agotados, medidor, archivo menor que un trozo): ASan+UBSan y TSan sin avisos. `test_asyncwriter` y `test_netclient_mock` siguen pasando. Comandos en `tools/README.md`.

## Riesgos e incógnitas (decirlo SIEMPRE al usuario)
1. **QuickJS en OpenOrbis**: compilar/enlazar sin probar (shim de `malloc_np.h`, `localtime_r`/`gettimeofday`/`clock_gettime`, cabeceras pthread). Si falla, el CI cae solo a `PSB_NO_JS`: pedir `build_js_failed.log`.
2. **`uipart`**: supuesto de superficie persistente; puede dejar restos/parpadeo. El repintado parcial puede no ganar nada si el límite es otro.
3. **libSceHttp desde varios hilos a la vez** (`dlconn>1` y el medidor con 2 y 4 conexiones) **no está probado**; podría colgar o cerrar la app. Por eso `dlconn=1` por defecto.
4. Servidores concretos con `Range`/redirecciones (p. ej. archive.org); el `Referer` solo se envía si la descarga viene de la pantalla de confirmación.
5. JS: re-maquetar por temporizadores reinicia hover/selección; el re-parseo cada ≥120 ms cuesta CPU; `fetch` bloquea la interfaz hasta que responde la red.
6. **Seguridad pendiente**: el `fetch`/XHR de una página puede leer direcciones privadas (router, LAN); falta bloquear rangos privados/loopback y revisar rutas de descarga (path traversal en nombres) y redirecciones.
7. Los servidores de prueba del medidor no se han verificado desde aquí.

## Qué pedir al usuario tras instalar el PKG (prioridad del usuario: **WiFi**; el cable es secundario/incómodo)
1. Ejecutar el workflow (`js_engine` marcado). Mirar si el zip `logs` trae `js_fallback.txt` (build sin JS) → devolver `build_js_failed.log`, `build.log`, `flags.txt`.
2. En la PS4: primera línea del log `== psbrowser v0.9.0 (DOM+CSS+JS) ==`, y `[jsx] motor JS: QuickJS` (si dice «no incluido», fue el fallback).
3. Páginas: una estática normal (no debe cambiar), una con menús `display:none`, friv.com/juegos sencillos. Buscar en el log `[jsx] DOM ok, N scripts…`, `[js] ERROR…`, `[css]`. Probar `js=0/1/2` en Configuración para aislar regresiones.
4. Interfaz: desplazarse en una página larga y mover el cursor; si hay restos/parpadeo, desactivar «repintar solo lo que cambia». Vídeo con `vscale=1` y `vpart=1`, con y sin ratón (líneas `[perfv]`, `[vp]`).
5. Descargas en **WiFi**: `about:dlbench` con la última URL (o servidor de prueba), empezando por 1 conexión; **solo si no hay cuelgue** probar 2 y 4. Luego bajar un archivo grande y cortar el WiFi a mitad para ver reanudación (`[dleng]`, «Descargas incompletas»). Devolver `log.txt`, `config.txt`, `downloads.txt`, `last.html` y el zip `logs`.

## Plan acordado (tras revisar el «Documento Maestro de Evolución» de ChatGPT)
Orden: (1) probar la v0.9.0 y corregir lo que falle (no apilar más código sin probar); (2) Descargas 2.0: ETag/`If-Range` + manifiesto con validadores, SHA-256 opcional, cola, pausa, velocidad total, conexiones adaptativas **con datos del medidor**; (3) eventos reales hacia el DOM (mapa nodo↔línea), **Canvas 2D** (reusar `canvas.h`) y **Gamepad API** (mando ya funciona), bloqueo de direcciones privadas en `fetch`; (4) workflow de CI aparte que ejecute `tools/tests` con sanitizers; (5) layout (flex/media queries) según qué páginas fallen en los logs. Recortado: Gecko/OrbisFox, ANGLE, HTTP/2 (no ahora); WebGL = proyecto aparte y posterior; WASM solo como demos con intérprete pequeño; GPU = prueba de viabilidad (posible reutilizar lo de GnmScale), no una fase. Reglas del documento que se mantienen: diferenciar **implementado / compilado / probado en PC / probado en PS4 / verificado en uso**; no declarar nada hecho sin probar; no quitar el fallback `PSB_NO_JS`; PKG: no instalar ni modificar automáticamente.
- **Lista de deseos del usuario (vigente):** descargar archivos pesados (PKG, ZIP) a la máxima velocidad real por WiFi e instalarlos; juegos web (friv.com, HTML5/JS); más compatibilidad con páginas modernas; vídeo con más fps; atajos/ajustes tipo navegador de PC.

## Archivos de v0.9.0
Nuevos: `app/dom.h/.cpp`, `app/jsx.h/.cpp`, `app/jsprelude.h`, `app/dlengine.h/.cpp`, `third_party/qjs_shim/malloc_np.h`, `third_party/qjs_shim/psb_qjs_shim.c`, `docs/V0.9.0.md`, `tools/tests/test_jsx.cpp`, `tools/tests/test_dlengine.cpp`. Cambiados: `app/main.cpp` (config `uipart`/`js`/`dlconn`, presentación parcial, carga con `jsx_load`, `runDownloadEngine`, `about:dlbench`, ajustes), `app/asyncwriter.h/.cpp`, `app/netclient.h/.cpp`, `.github/workflows/build-pkg.yml` (**SÍ cambia**), `tools/build_local.sh`, `tools/README.md`, `tools/stubs/SDL2/SDL.h`.
## Git (v0.9.0)
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools third_party .github README.md
git commit -m "v0.9.0: presentacion parcial, DOM/CSS lite/JS (QuickJS), motor de descargas por trozos y medidor"
git push
```
Después: Actions → **Build PKG** → *Run workflow* (`js_engine` marcado) → instalar el PKG → probar según `docs/PRUEBAS.md` («Prueba v0.9.0») → devolver `log.txt`, zip `logs`, `config.txt`, `downloads.txt`.

# >>> v0.8.3 (BENCHMARK + OPCIONES DE VIDEO) - SIN PROBAR EN PS4 - leer primero <<<
> 7-nov-2026. Log v0.8.2 recibido (version confirmada). RESULTADOS: la via rapida SI funciona (`[perfv] RAPIDO`, subida=1 ms) pero `presentacion` = 51-63 ms FIJOS => ~14 fps (11 fps en la via lenta). Sync correcta (velocidad ~1.0x, 0 descartados, conversion 4.9 ms), cierre limpio (`[vp] cerrado limpio`). El cuello es `SDL_RenderCopy`(escalado)+`SDL_RenderPresent` a 1920x1080 con renderer SOFTWARE; la causa exacta NO esta confirmada.
## Que anade v0.8.3
- `runBench()` (F9, `psb:bench`, `about:bench`, logs `[bench]`): mide ms/fotograma de: Present solo, Clear+Present, copia 1920x1080, copia 864x480 1:1, copia escalada, `SDL_UpdateWindowSurfaceRects` con rectangulos 1x1/480x270/960x540/864x480/completo, `SDL_UpdateWindowSurface`.
- Config `vscale` (0 ajustar, 1 nativo 1:1, 2 x2 si cabe) y `vpart` (experimental: solo actualiza el rectangulo del video; desactivado con raton conectado o si falla; `g_partFailed`).
- **Pulido (misma v0.8.3, sin probar en PS4):** Ctrl+F busqueda en pagina (resalta amarillo/naranja, Intro/F3/Mayus+F3, barra inferior; solo teclado, sin OSK de mando), buscador configurable (`search`: DDG, DDG Lite, Bing, Google, Startpage; `url.cpp g_searchEngine`), User-Agent configurable (`ua`: PS4, Chrome, Firefox, Smart TV, Lynx; `g_netUserAgent` en netclient, se aplica por peticion), `about:shortcuts` (F1), `about:history` (Ctrl+H, persistente en `/data/psbrowser/history.txt`, max 300), contenido de `<noscript>` ahora visible (htmldoc), icono `assets/icon0.png` (512x512, el workflow lo copia a `sce_sys/icon0.png`; si el PKG falla por el icono, quitar ese paso).
- Stubs SDL ampliados. Compila con `-fsyntax-only`; NO probado en consola.
## Siguiente paso
Pedir al usuario `log.txt` con las lineas `[bench]` y 20 s de video con cada opcion. Decidir segun resultados (ver PRUEBAS.md v0.8.3). Si todo el Present es fijo ~55 ms: alternativa = reducir resolucion de ventana/renderer o probar otro backend de SDL.
## Lista de deseos del usuario (pendiente)
Atajos y configuraciones tipo navegador de PC, mas compatibilidad (streaming/HLS, 4K sceAvPlayerStreamCount=0), mas calidad y fps de video, paginas con su diseno real (DOM/CSS/JS, ver docs/JS.md, servidor de render remoto), WebP/SVG/GIF, cookies, descargas multiconexion, Ctrl+F, User-Agent configurable.
## Git (v0.8.3)
`git add app assets tools/stubs .github/workflows/build-pkg.yml README.md docs && git commit -m "v0.8.3: benchmark de presentacion y opciones de video" && git push` (workflow sin cambios).

# >>> ESTADO v0.8.2 (VIA RAPIDA DE VIDEO) - SIN PROBAR EN PS4 - leer primero <<<
> 7-nov-2026 (noche, 2.º envío). El usuario dijo «el vídeo va mal incluso descargándolo primero» y subió `log_1.txt`, `logs__20_.zip` (CI), `video_frame0_1.bin`, etc. **Ese log es de v0.8.0** (línea 1: `== psbrowser v0.8.0 ==`; no hay líneas `[perfv]` ni `velocidad`): o no instaló v0.8.1, o el CI compiló el repo antiguo. **Pedirle que confirme que la primera línea del log diga v0.8.2.**

## Lo que enseña ese log (v0.8.0, vídeo CON audio)
- `14057500-480p.mp4`: **864x480**, AUDIO 2 canales **44100 Hz** (se remuestrea a 48000), 188 s; `PRIMER FOTOGRAMA: width=864 height=480 pitch=896 crop(R44) fps=0 aspecto=1.002`. Mismo resultado **por streaming y desde `/data/psbrowser/downloads/`** → **no es la red ni el disco**.
- **Con audio NO hay cámara lenta:** `pos` avanza ~2000 ms por cada línea de 2 s de pared (reloj de audio del AvPlayer) y los fps mostrados son **10,4–14 con un vídeo de ~24–30 fps** → va **entrecortado**, no lento. Cuello = coste por vuelta ≈ 85–95 ms: `[perf]` de la interfaz = dibujo 6–8 ms + **subida+presentación 53–56 ms** (fijo, medido también en 0.5.0) + conversión NV12→ARGB y escalado CPU a 1920×1080. Encaja con ~11 fps.
- Los clips sin audio (v0.8.0, log anterior) sí iban a 0,5x; la sincronización por reloj de pared de v0.8.1 es para ese caso.
- **`11258651-4k.mp4` por streaming: `sceAvPlayerStreamCount = 0` tras 40 s** → «No se encontraron pistas». El log muestra una petición de **1 MB cada ~3 s con un TLS nuevo por petición** (~350 KB/s): o el AvPlayer no soporta ese vídeo (4K/HEVC probable), o la cabecera `moov` está al final y el streaming secuencial no llega a tiempo. **Sin diagnosticar.** Pendiente: reutilizar la conexión en `vsource`/`netclient` (keep-alive) o pedir bloques mayores; probar si 4K no entra aunque se descargue.
- `sceAudioOutOpen` devuelve 536870919/536870918 según la vez (puertos distintos, sin error); `sceAudioOutInit` = 0x8026000E en 2.ª llamada (inofensivo).

## Qué cambia en v0.8.2
**Vía rápida de presentación (`presentVideoFast`, `vpCleanPlayback` en `main.cpp`; `vp_frame()` en `vplayer.*`):** cuando el vídeo se reproduce sin nada encima (estado PLAYING, overlay oculto, sin mensajes, sin toast, sin diálogo de salida, sin panel de estadísticas) el fotograma ARGB se sube como **textura de SDL del tamaño nativo del vídeo** (p. ej. 864×480 = 1,6 MB en vez de componer y subir 8 MB) y `SDL_RenderCopy` lo escala con letterbox; el fondo negro solo se limpia en el primer fotograma de la vía rápida (siempre, si hay ratón conectado, para borrar el cursor). Con overlay/mensajes visibles se usa la vía normal de v0.8.1 (más lenta). **Hipótesis a verificar:** que el coste fijo de ~55 ms sea sobre todo `UpdateTexture(8 MB)`+`RenderClear`+`RenderCopy` de pantalla completa y no `SDL_RenderPresent` en sí (si lo fuera, la vía rápida casi no mejora y habría que mirar `[perfv]` y plantearse otra salida: p. ej. bajar la resolución de la ventana en modo vídeo). Log nuevo: **`[perfv] RAPIDO t=… subida=… presentacion=… (total … ms)`** y `[perfv] textura de video nativa WxH`. Si `total` baja de ~40 ms, ya se pasa de 15 fps.
Compila con stubs (`SDL_DestroyTexture` y `SDL_SetRenderDrawColor` añadidos a `tools/stubs/SDL2/SDL.h`); **no probado en consola**.

## Pendiente
1. Instalar v0.8.2, confirmar la versión en la 1.ª línea del log y devolver `log.txt`: `[perfv] RAPIDO`, `[vp] … velocidad`, fps con vídeo con audio y sin audio; cierre por Salir y por menú de PS (código CE si hay crash).
2. Si sigue por debajo de ~20 fps: acelerar conversión NV12→ARGB (la conversión se mide: `conversion N ms` en la línea `[vp]`) y/o reducir el coste de presentación a pantalla completa.
3. 4K/streaming lento: keep-alive en `netclient` para los rangos; comprobar límites del AvPlayer.
4. El resto del bloque v0.8.1 de abajo sigue vigente (cierre, F8, duración en la lista, HLS, servidor remoto).

## Git (v0.8.2)
Cambiados: `app/main.cpp`, `app/vplayer.cpp`, `app/vplayer.h`, `tools/stubs/SDL2/SDL.h`, `docs/CONTEXTO.md`, `docs/PRUEBAS.md`, `README.md` (más todo lo de v0.8.1 si aún no lo subió: `app/vsync.h`, `tools/tests/…`). El workflow NO cambia.
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools README.md
git commit -m "v0.8.2: via rapida de video (textura nativa), sincronizacion por reloj de pared, cierre mas limpio, F8 clasifica"
git push
```
Después: Actions → **Build PKG** → *Run workflow* → instalar → mirar que el log empiece por `== psbrowser v0.8.2 ==`.

---
# >>> ESTADO v0.8.1 (SINCRONIZACION DE VIDEO + CIERRE + F8) - SIN PROBAR EN PS4 - leer primero <<<
> 7-nov-2026 (noche). En esta sesión SÍ se recibió el zip del repo (`psbrowser-v0_8_0-integrado.zip`) además del CONTEXTO, `log.txt` (el mismo de 1535 líneas ya analizado en «RESULTADOS v0.8.0»), `last.html` (Hacker News, irrelevante), `config.txt`, `bookmarks.txt`, `downloads.txt`, `clipboard.txt`, `video_frame0.bin` y el zip `logs` del CI (compilación de v0.8.0 OK, clang 18.1.3, `-O2 -DPSB_STRICT_TLS`). No hubo log nuevo: los cambios de abajo parten del análisis del log de v0.8.0.

## Qué cambia en v0.8.1
1. **Sincronización por reloj de pared (arregla la «cámara lenta»)** — `app/vsync.h` (lógica pura, **probada en PC** con `tools/tests/test_vsync.cpp`: casos A–E = reproductor que entrega un fotograma por petición a 70 ms/vuelta, reproductor con reloj propio, vuelta rápida de 4 ms, seek de +60 s, parada de red de 2 s; velocidad resultante 0,99–1,00x). Se usa en `vplayer.cpp::vp_update()` **solo cuando el vídeo no tiene audio** (`!haveAudio`, caso de los clips de vista previa): ancla el primer fotograma al reloj de pared; si el siguiente va atrasado pide hasta 8 fotogramas seguidos y muestra solo el último (descarta el resto); si llega >90 ms adelantado lo convierte a un buffer aparte (`P.pend`) y lo muestra cuando le toca; re-ancla tras seek (`vp_seek_abs` hace `sync.reset()`), pausa/continuar, paradas >400 ms entre fotogramas (red) y saltos >2 s. **Con audio no se toca nada** (manda el reloj de audio del AvPlayer, un fotograma por vuelta como en v0.8.0): sigue sin verificarse si ahí también hay cámara lenta, porque ningún log tiene vídeo con audio.
   - **La causa de la cámara lenta sigue siendo una HIPÓTESIS**: el log mostraba +24 fotogramas y +1000 ms de posición por línea, pero 12 fps medidos (≈2 s de pared por 1 s de contenido). El log nuevo lo confirmará: **`[vp] … fps mostrados | velocidad Nx (X ms de contenido en Y ms de pared) | descartados N | conversion M ms | … | reloj=pared|audio`**. Si `velocidad` ≈ 1,0x con `descartados` > 0, funcionó (se ve a ritmo real pero con menos fps). Si `velocidad` sigue ≈ 0,5x, la hipótesis era falsa (¿reloj `sceKernelGetProcessTime`?, ¿AvPlayer limitando por su cuenta?) y hay que mirar `[perfv]`.
2. **Diagnóstico de rendimiento en vídeo:** líneas **`[perfv] t=… dibujo=… subida=… presentacion=… (total … ms)`** (30 primeras vueltas con vídeo y luego 1 de cada 5 hasta 90), con el reloj `SDL_GetTicks` del bucle principal. Separa `drawFrame`, `SDL_UpdateTexture` y `SDL_RenderCopy+Present`. Con esto se decide si merece la pena el cambio grande de presentación (ver «Pendiente»). Panel de estadísticas (tecla I / botón 3) ampliado: fps, **velocidad**, descartados, ms de conversión y qué reloj manda.
3. **Cierre más limpio (NO arregla con certeza el crash del menú de PS4):** `vp_close()` ahora hace `sceAvPlayerStop` → espera 60 ms → `sceAvPlayerClose`, `sceAudioOutClose` (registra el código), libera texturas que queden (`releaseLeftoverTextures`, avisa en el log si había alguna) y escribe `[vp] cerrado limpio`; `sceSysmoduleLoadModule`/`sceUserServiceInitialize`/`sceAudioOutInit` se llaman **una sola vez** por proceso; nueva `vp_shutdown()` que `hardExit()` llama antes del `_exit(0)`.
   - **Evidencia sobre el crash (v0.8.0):** `psb_log` hace `fflush` en cada línea, así que la ausencia de `[quit]` NO es un búfer perdido: el sistema mató/colapsó el proceso antes de que el bucle recibiera `SDL_QUIT` (cierre desde el botón PS → «Cerrar aplicación»). Las direcciones de memoria directa se reutilizan entre vídeos (`0x203540000` repetida), o sea que las texturas sí se liberan: no hay fuga evidente. Los 9 `sceAudioOutOpen` devolvieron siempre el mismo puerto (536870919) y el `sceAudioOutInit` repetido da 0x8026000E (inofensivo). **Causa sin identificar.** Falta: el código CE-xxxxx-x que muestra la PS4, y probar por separado (a) salir con el botón Salir de la app tras ver un vídeo (camino `hardExit()` aún no probado) y (b) cerrar desde el menú de PS sin haber abierto ningún vídeo (para saber si es culpa del vídeo o del navegador en general).
4. **F8 / `about:videos` clasifica y ordena (`vidScore`, probado en PC con `tools/tests/run_scanvideos.sh`):** las URLs con `preview`, `thumb`, `trailer`, `teaser`, `hover`, `/td.`, `_td.`, `-td.`, `/tn.`, `_tn.` se marcan «parece una VISTA PREVIA (clip corto)» y van al final; antes van las que llevan calidad en la ruta (2160…240); HLS/WebM puntúan algo menos. Si **todo** lo encontrado son vistas previas: aviso en el toast y en la página («esta página parece un listado: abre la página del vídeo y pulsa F8 allí»). **No** muestra duración/tamaño (haría falta un HEAD/Range por URL; pendiente).
5. **Menor:** `renderFrame()` ya no llama a `SDL_RenderClear` (la textura cubre toda la pantalla; ahorra un relleno de 8 MB por frame; efecto no medido).

## Verificación hecha (SIN toolchain real; sin red)
`g++ -std=c++17 -fsyntax-only -Wall -Wextra` de `main.cpp` y de `vplayer.cpp` (este con stubs temporales de `orbis/AudioOut.h`, `UserService.h`, `Sysmodule.h`, `libkernel.h`) **sin avisos**; `test_vsync` OK (ASan/UBSan); `run_scanvideos.sh` OK. **Lo que solo comprueba el CI/consola:** compilación real de `vplayer.cpp` (usa `sceKernelGetProcessTime`, `sceAudioOutClose`, `std::swap` sobre vectores), todo el comportamiento de vídeo en consola.

## Pendiente (en este orden)
1. **Instalar v0.8.1 y devolver `log.txt`** con: un clip sin audio (vista previa) → líneas `[vp] … velocidad`; **un vídeo completo con audio** (p. ej. un `*-480p.mp4` de `downloads.txt` ya descargado, vía `about:downloads` → [Reproducir]) → ver si con audio hay cámara lenta y si el sonido va sincronizado; cierre por Salir de la app y por menú de PS; código CE si hay crash.
2. **Techo de ~15 fps por presentación** (`[perf]`: dibujo 7–8 ms + subida+presentación 51–62 ms; en 0.5.0 igual). Según `[perfv]`: si domina `presentacion`, probar **textura de vídeo propia a tamaño nativo** (`SDL_CreateTexture` streaming del tamaño del vídeo, `SDL_RenderCopy` escalado con letterbox y el overlay solo cuando esté visible) en lugar de convertir/escalar a 1920×1080 en CPU y copiar 8 MB por fotograma. Si domina `dibujo`/`conversion`, optimizar NV12→ARGB (por filas, sin tablas).
3. **Duración/tamaño en la lista F8** (HEAD o `Range: bytes=0-0` por URL, con tope y sin bloquear la interfaz).
4. **HLS (`.m3u8`)** con `libSceAvPlayerStreaming` (sin probar), luego decidir el servidor de renderizado remoto (§7.4-C) para páginas «como son».
5. Resto del roadmap (§7): cookies, WebP/SVG/GIF, CSS lite, JavaScript.

## Qué pedir al usuario tras instalar el PKG
`log.txt` (líneas `[vp]`, `[perfv]`, `[quit]`, `[video]`), `config.txt`, el código CE si hay crash, y qué vídeo/URL usó (con o sin audio).

## Git (v0.8.1)
Archivos nuevos: `app/vsync.h`, `tools/tests/test_vsync.cpp`. Cambiados: `app/vplayer.cpp`, `app/vplayer.h`, `app/main.cpp`, `tools/tests/run_scanvideos.sh`, `tools/tests/test_scanvideos.cpp`, `tools/README.md`, `docs/PRUEBAS.md`, `docs/CONTEXTO.md`, `README.md`. **El workflow NO cambia** (copia `app/*.h` y `app/*.cpp`, `vsync.h` entra solo).
```bash
cd Sackso_PS4_Browser_Testing-main
git add -A app docs tools README.md
git commit -m "v0.8.1: sincronizacion de video por reloj de pared, cierre mas limpio, clasificacion de videos en F8, diagnostico [perfv]"
git push
```
Después: Actions → **Build PKG** → *Run workflow* → instalar → probar según `docs/PRUEBAS.md` («Prueba v0.8.1»).

---
# >>> ESTADO v0.8.0 (VIDEO + SALIR) - YA PROBADA EN PS4 (resultados abajo); texto original de la sesion de integracion <<<
> Actualizado el 7 de noviembre de 2026. El usuario pidio **"focus total" en reproducir video dentro del navegador** (streaming sin descargar, o descargando y viendolo desde el propio navegador) **y un boton de salir** con mando, raton y teclado. v0.7.0 tampoco se ha probado en consola.

## RESULTADOS v0.8.0 (prueba en PS4, log de 1535 líneas, 184 líneas `[vp]`) — LEER ANTES DE TOCAR EL VIDEO
**Funciona (confirmado en consola):** `sceSysmoduleLoadModule(0xA5)=0`, `sceAvPlayerInit` devuelve un puntero válido, `AddSource` OK, el streaming por rangos HTTP (`read(pos=…)`) alimenta al AvPlayer, el decodificador escribe en memoria directa (`textura: … memoria directa`), aparecen eventos 3 (listo) y la pista de vídeo, y **el volcado `video_frame0.bin` demuestra que el formato es NV12 lineal con `pitch`=ancho** (comprobado numéricamente: diferencia media entre filas vecinas 2,1 frente a 66,6 con un ancho erróneo; plano UV centrado en ~129; tras `pitch*alto*1.5` vienen 65 536 bytes a cero que añade el volcado). No hace falta cambiar la conversión de color. 9 reproducciones seguidas sin que el AvPlayer falle; cada una hace `sceAvPlayerClose=0`.

**Problema 1 — «clips de 10 segundos»: NO es un límite del reproductor.** Las 9 reproducciones del log fueron **todas `…/td.mp4`** (`tbv.sb-cd.com/t/<id>/…/td.mp4`), que reportan `duracion 10084 ms` y tamaños 320×192 / 320×432 / 320×688: son los **vídeos de vista previa (hover) de las miniaturas**, que `scanVideos()` (F8) encuentra en las páginas de listado/playlist (`referer=…/playlist/…`). El vídeo completo vive en la página del propio vídeo y a menudo su URL se construye con JavaScript. Pendiente en `scanVideos()`: **clasificar y ordenar** (descartar o mandar al final `td.mp4`, `/t/…`, `preview`, `thumb`, `trailer`, `teaser`; priorizar por URL con `1080/720/480/p.mp4`, tamaño o ruta `vdownload`), **mostrar en la lista la duración/tamaño** (HEAD/Range corto) y **aviso en la página de listado** («esto parece un listado: abre la página del vídeo y pulsa F8»). En `downloads.txt` el usuario sí bajó vídeos completos (`…-480p.mp4`, 12–33 MB), así que cuando la URL real está en el HTML funciona.

**Problema 2 — pocos fps / cámara lenta (diagnóstico, por confirmar con marcas de tiempo):**
- Las líneas `[vp] N fotogramas/s (total T) pos=P ms` muestran, línea tras línea, **+24 fotogramas y +1000 ms de posición** (p. ej. total 26→50→74→98, pos 1083→2083→3083→4083) pero el cálculo de fps da **12,0**. Es decir: el contenido es de **24 fps**, se muestran **todos** los fotogramas en orden, pero tardamos ~2 s de reloj de pared por cada 1 s de contenido → **se reproduce a ~0,5× (cámara lenta)**, no «con fotogramas saltados». Si el intervalo de la estadística es de 2 s, esa es la causa; **confirmar añadiendo marca de tiempo en el log** (`SDL_GetTicks`).
- Causa probable: el bucle principal consume **un fotograma por iteración** y una iteración cuesta ~66–70 ms (`[perf]` de la interfaz: dibujo 7–8 ms + `subida+presentacion` 51–62 ms; ver Prueba v0.5.0). El AvPlayer en vídeo sin audio entrega el **siguiente** fotograma de su cola (su reloj sigue al que se consume), no el que toca por reloj de pared. 
- **Arreglo propuesto (reproductor):** sincronizar por **reloj de pared propio**: guardar `t0` (pared) y `ts0` (primer fotograma); en cada iteración pedir fotogramas con `GetVideoDataEx` **en bucle** mientras `ts <= pared-t0+ts0`, convertir/subir **solo el último** y dejar un fotograma «pendiente» si aún no toca; así se descartan fotogramas cuando vamos tarde en vez de ir a cámara lenta. Con audio, usar el reloj de audio. **Techo real ~15 fps** mientras cada frame cueste ~60 ms de presentación: para 24–30 fps hay que abaratar la presentación en modo vídeo (no redibujar toda la interfaz por fotograma; `SDL_UpdateTexture` solo del rectángulo del vídeo; textura del vídeo a su tamaño nativo y escalar con `SDL_RenderCopy` en vez de convertir a 1920×1080 en CPU; separar en el log `convertir / subir / presentar` en modo vídeo para saber qué cuesta). Probar si `SDL_RenderPresent` espera al vsync.
- `fps=0`, `aspecto=0.000` en el primer fotograma: el AvPlayer no informa de la tasa; se deduce de los `ts` (83 ms el primero; 41,7 ms entre fotogramas = 24 fps).

**Problema 3 — crash al cerrar desde el menú de PS4 tras ver vídeos:** el log **no contiene ninguna línea `[quit]`** y termina en `boton 1` ×3 tras `[nav] about:videos`: el usuario cerró con el botón PS → «Cerrar aplicación», no con el botón Salir de la app (ese camino `hardExit()/_exit(0)` sigue **sin probar**). Sin más trazas en el log del cierre. Hipótesis (sin confirmar): al terminar el proceso quedan vivos recursos del AvPlayer/AudioOut: en una sesión hubo **9 `sceAvPlayerInit` y 9 `sceAudioOutOpen`** (el 2.º en adelante con `sceAudioOutInit = 0x8026000E`, «ya inicializado»), memoria directa (`textura: … memoria directa`: 1–29 MB por vídeo, no se sabe si se libera), un hilo de audio propio y el módulo 0xA5 cargado. Qué hacer: (1) que `vp_close()` haga **`sceAvPlayerStop` → esperar → `sceAvPlayerClose`**, **`sceAudioOutClose`** del puerto, **liberar la memoria directa** en el callback de deallocate/al cerrar, y **unir el hilo de audio** antes de volver; (2) **inicializar AvPlayer/AudioOut una sola vez** y reutilizarlos entre vídeos; (3) interceptar el cierre del sistema (`SDL_QUIT`/eventos de `libSceSystemService`) para llamar a `vp_close()` + `hardExit()`; (4) pedir al usuario el **código de error** que muestre la PS4 (CE-xxxxx-x) y probar **dos casos**: salir con el botón Salir de la app, y salir con el menú de PS tras ver un solo vídeo / ninguno. 

**Entrada de mando observada:** `boton 0/1/14` y `hat` (cruceta) funcionan en la lista de vídeos; `boton 1` (○) cierra el reproductor.

**Sitios de vídeo/streaming «como son» (pregunta del usuario, respuesta honesta):** con el motor actual (sin JS, sin CSS real, sin MSE) **no se pueden usar los reproductores de las propias webs ni sitios de streaming**. Hay tres vías: (a) **extraer la URL real** del vídeo en el HTML y reproducirla con nuestro reproductor (funciona en sitios que la dejan en claro; en otros está ofuscada por JS); (b) **HLS (`.m3u8`)**: probar `libSceAvPlayerStreaming`/AvPlayer con listas HLS (el toolchain trae `libSceAvPlayerStreaming.so`; **sin probar**), lo que cubriría gran parte del streaming sin DRM; (c) **servidor de renderizado remoto** (§7.4-C: Chromium + `ffmpeg` en el PC del usuario, que sirve teselas de la página y el vídeo transcodificado a MP4/H.264 por la LAN) = la única vía general para páginas «como son» y reproductores incrustados. **Contenido con DRM (Netflix, Disney+, Prime…) es inviable** en cualquier caso (Widevine/PlayReady). Orden recomendado: 1) arreglar sincronización/fps y el crash de cierre (concreto y medible), 2) clasificación de URLs en F8, 3) probar HLS, 4) decidir el servidor remoto.

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
| **0.8.0** | 0.7.0 + **reproductor de video** (libSceAvPlayer, streaming por rangos o archivo descargado), **boton/dialogo Salir**, `about:videos` (F8), ajustes de video | **Probada en PS4 (log de 1535 líneas):** el vídeo reproduce (primer fotograma bien, color correcto), pero ~12 fps / cámara lenta; crash al cerrar desde el menú de PS4 tras reproducir; botón Salir y entradas de vídeo sin confirmar |
| **0.8.1** | 0.8.0 + sincronización de vídeo por reloj de pared (solo sin audio), cierre del reproductor más limpio, clasificación de vistas previas en F8, logs `[perfv]` y velocidad real | **Sin probar en PS4** (compila con stubs; `vsync` y `scanVideos` probados en PC). Es el siguiente PKG a instalar |
| **0.8.2** | 0.8.1 + vía rápida de vídeo (textura nativa escalada por SDL cuando no hay overlay) | **Sin probar en PS4.** Siguiente PKG a instalar (el log de v0.8.0 mostró ~11 fps con audio, entrecortado, por el coste de presentar 1920×1080) |

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

### Prueba v0.8.0 (log de 1535 líneas)
Ver el bloque «RESULTADOS v0.8.0» al principio del documento. Resumen: AvPlayer funciona, NV12 correcto, 9 vídeos de vista previa (`td.mp4`, 10 s) reproducidos a ~12 fps y en cámara lenta (24 fps de contenido, ~0,5× de velocidad), sin `[quit]` en el log (cierre por el menú de PS4 con crash). `config.txt`: `wheelStep=7 dark=1 zoom=100 pageW=1200 smooth=1 images=1 vdec=0 vhalf=1 volume=80`. `[perf]` de la interfaz: dibujo 7–8 ms, subida+presentación 51–62 ms (igual que en 0.5.0).

## 5. Hallazgos técnicos (probados en PS4 salvo que se diga)
- **Red/TLS con el sistema:** `sceHttp`/`sceSsl` funcionan: DNS, HTTP, HTTPS, chunked, redirecciones (manuales leyendo `Location`). Pools: net 16 KB, ssl 300 KB, http 1 MB. Módulos NET, HTTP, SSL vía `sceSysmoduleLoadModuleInternal`. Certificados reales validados (`verifyErr=0`) → TLS estricto por defecto (`-DPSB_STRICT_TLS`).
- `sceHttpSetResponseHeaderMaxSize(tpl, 128*1024)` evita `sendRequest 0x80431073` en Wikipedia.
- Cabeceras de OpenOrbis con firmas incompletas (`void f();`): llamar con *cast* a la firma real.
- **SDL 2.0.9 de PS4:** solo ve el mando; render por software; 1920x1080. Teclado (`libSceKeyboard`, clase `Keyboard` del sample `keyboard`) y ratón (`libSceMouse` cargada en runtime, `app/mouse.*`) van con APIs nativas. Códigos de tecla = USB HID (D=7, L=15, T=23, W=26, 0=39, Enter=40, Esc=41, Backspace=42, Tab=43, Space=44, F5=62, F6=63, Home=74, PgUp=75, End=77, PgDn=78, →=79, ←=80, ↓=81, ↑=82).
- Escribir en `/data/psbrowser/` con `fopen` funciona.
- `std::thread` y `std::mutex` **sí** enlazan y funcionan en OpenOrbis (hilo de imágenes probado). Pero la **pila de los hilos es pequeña**: evitar recursión profunda y buffers grandes en la pila dentro de hilos.
- El toolchain trae `stb_image.h`, `stb_truetype.h`, `stb_image_write.h` (header-only), fuentes de muestra (Gontserrat, VeraMono, SIL OFL) y FreeType del sistema. Se usan stb + fuentes incrustadas.
- libc++ del toolchain: `std::sqrt(float)` devuelve `double` y rompe `std::min(1.f, ...)` → en `canvas.h` se usa `sqrtf`.
- **Verificado en v0.8.0:** `libSceAvPlayer` reproduce MP4 H.264 por streaming desde una fuente propia (formato de fotograma NV12 lineal, `pitch`=ancho, memoria directa para texturas). **Sin verificar aún:** HLS con `libSceAvPlayerStreaming`, audio (los clips de vista previa no traen audio: `audio=0`), `sceImeDialog` (teclado del sistema), `scePthreadAttrSetstacksize`/`pthread_attr_setstacksize` para hilos con pila grande (no se sabe si enlazan con los stubs de OpenOrbis).

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
"[ESTADO ACTUAL: lee primero el bloque >>> v0.9.0 <<< de docs/CONTEXTO.md y luego «RESULTADOS v0.8.0» (vídeo con AvPlayer). v0.9.0 añade presentación parcial, DOM/CSS lite/JavaScript (QuickJS), motor de descargas por trozos reanudable y el medidor about:dlbench; NADA está probado en mi PS4 todavía. Mi prioridad es descargar archivos pesados por WiFi.] Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md` del zip adjunto y pídeme el zip del repo si no lo he subido. Lo primero es verificar v0.8.1 con mi `log.txt` (líneas `[vp] … velocidad`, `[perfv]`, `[quit]`) y mi `config.txt`. Después, según el roadmap: presentación de vídeo más barata, duración en la lista F8, HLS, servidor de renderizado remoto, **JavaScript** (`docs/JS.md`), cookies, WebP/SVG/GIF. Responde en español, sé honesto sobre lo no probado o no compilado, pídeme `log.txt` antes de tocar mapeos de entrada, dame los comandos `git` exactos si cambias archivos y entrégame cada vez un `CONTEXTO.md` actualizado."


## 12. v0.9.3 (leer primero; ver docs/V0.9.3.md)
Estado: **sin probar en PS4**. Cambios: timeouts cortos y hosts muertos en carga de páginas; cabeceras/Referer/Origin en fetch y XHR; detección inmediata de formatos de vídeo no soportados (`app/vsniff.h`); tecla S / botón "Tam" / botón 2 del mando cambian el tamaño de imagen del vídeo (`vscale` por defecto 3 con migración `cfgv=2`); alto contraste y tamaño de cursor (Ajustes → Accesibilidad); avisos más largos; `psb_solver.py` más estricto (tipos de descarga, solo red local, cookies filtradas, `/open` no bloquea).
Hallazgos del log v0.9.1: `0x80431068` = timeout (suposición); WebM no se reproduce (AvPlayer); pkg-zone.com usa módulos ES.
Pendiente principal: soporte de `type=module` en QuickJS; verificar en PS4 mocha/akirabox/vikingfile (este último solo con el ayudante del PC y el firewall abierto).

## 13. v0.9.4 (ver docs/V0.9.4.md)
Sin probar en PS4. Prelude con stubs de WebSocket/EventSource/indexedDB/Intl/FileReader/BroadcastChannel/postMessage/XMLSerializer/etc. (test: `node tools/tests/test_prelude_stubs.js`), clase `Request`, páginas de error con causa legible y "Reintentar" (`app/neterr.h`). Pendiente principal sin cambios: módulos ES, iframes, descargas (reposo/cola/página), app Android. Lista completa en PENDIENTE.md.
