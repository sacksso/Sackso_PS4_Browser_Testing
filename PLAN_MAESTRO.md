# psbrowser — PLAN MAESTRO (v0.9.4 en adelante; actualizado en v0.9.5)

Este documento combina (1) la visión y el orden de trabajo acordados y (2) el inventario detallado anterior (`PENDIENTE.md`, al final como Apéndice A).
Estado: v0.9.4 entregada, **sin probar en PS4**, compilada solo con stubs. Nada de lo de abajo está hecho salvo lo marcado `[~]` (hecho sin probar) o `[x]` (verificado).

---

## 1. Metas del usuario (en orden de importancia)
1. **Descargar PKGs, ZIPs y otros archivos** desde páginas de alojamiento (vikingfile, onefile, mochalife/mocha.my, akirabox, pkg-zone...) directamente en la PS4 y sin tanto problema.
2. Que el navegador sea **usable a diario** (leer, buscar, foros, wikis, descargas).
3. Webs de vídeo (tipo xvideos, solo como ejemplo) con **buena fluidez**.
4. Explorar si se puede **aprovechar la potencia de la PS4** (GPU) para juegos web o portes a navegador.

---

## 2. Estrategia para descargas (meta 1)
Principio: **la consola hace lo mínimo y un ayudante hace lo difícil.** Un Chromium real (PC ahora, Android después) resuelve captchas, esperas y clics; la PS4 solo recibe el enlace final con sus cookies y User-Agent, y descarga. Es más fiable que pedirle a QuickJS que ejecute páginas diseñadas para esquivar bots.

### 2.1 Pasos, en orden
- [ ] **Paso 0 — verificar lo entregado**: compilar v0.9.4, abrir el firewall (`netsh advfirewall firewall add rule name=psb_solver dir=in action=allow protocol=TCP localport=8765`, PowerShell como administrador), comprobar `http://IP_PC:8765/ping`, y probar un sitio sin captcha y uno con captcha. Enviar `log.txt`.
- [ ] **Paso 1 — flujo de un solo paso**: estando en la página de descarga, un botón/atajo la manda al ayudante, resuelves el captcha en el PC/móvil y la descarga empieza sola en la PS4 (hoy son varios pasos manuales con `!solver`/verify). (M)
- [ ] **Paso 2 — descargas robustas** (lo que más importa con PKGs de varios GB): reanudar tras cortes y tras cerrar el navegador, impedir el reposo de la consola mientras descarga, comprobar espacio libre antes de empezar, aviso sonoro/visible al terminar, cola, página de descargas con pausa/cancelar/velocidad/tiempo restante. (M cada una; algunas dependen de APIs de la consola que hay que comprobar)
- [ ] **Paso 3 — perfiles por sitio**: tabla `sitio → resultado` en `docs/` y un archivo editable de perfiles (UA, cabeceras, esperas, si necesita ayudante). Cada sitio falla por un motivo distinto; hay que documentarlos uno a uno con logs reales. (S/M, continuo)
- [ ] **Paso 4 — app Android** como alternativa al PC (ver la sección 1 del Apéndice A, "Ayudante en Android").
- [ ] **Paso 5 — reintento de enlaces caducados** (403/410 → pedir un enlace nuevo al ayudante) y verificación de integridad (tamaño y, si la página da hash, SHA-256/MD5).

### 2.2 Estado conocido por sitio (por confirmar con logs)
| Sitio | Qué se sabe | Qué falta |
|---|---|---|
| vikingfile | Turnstile (captcha de Cloudflare). Los errores `aclib is not defined` vienen del adblock y parecen inofensivos | Ayudante + firewall abierto; probar flujo completo |
| mocha.my / mochalife | Error `not a function` en el decodificador de emails de Cloudflare (v0.9.2 añadió `compareDocumentPosition`) | Re-probar con v0.9.3/0.9.4 (cabeceras + Referer en XHR/fetch) |
| akirabox | Next.js; el presupuesto de CPU ya se arregló en v0.9.2 | Re-probar; si falla, capturar la petición real con el ayudante |
| onefile | Sin datos | Probar y enviar log |
| pkg-zone.com | Página vacía: probablemente usa `type=module` (Vite/Inertia); además un script de terceros daba timeout (22 s, ya acotado) | **Módulos ES** |

---

## 3. Navegador de uso diario (meta 2)
**Techo realista** con el motor actual: lectura, búsquedas, foros, wikis, descargas. Webs pesadas (React/Next complejas) y streaming en la propia página irán lentas: QuickJS es un intérprete sin JIT, y el dibujado es por software (`SDL_RENDERER_SOFTWARE`) sobre una CPU Jaguar modesta. No competirá con Chrome, pero se puede acercar.

Lo que más lo acerca (orden sugerido):
- [ ] **Módulos ES + `import()` dinámico** (deja de salir en blanco la web moderna). Requiere conocer la firma real de `JS_SetModuleLoaderFunc` en `quickjs-ng v0.18.0`: pedir salida de `grep -n "ModuleLoader" quickjs.h` del CI. (L)
- [ ] **iframes + `postMessage`** (captchas, reproductores embebidos). (L)
- [ ] **Restaurar pestañas y scroll** tras cierre inesperado; recordar scroll al volver atrás. (M)
- [ ] **Zoom por sitio**, buscar en la página, autocompletar en la barra con historial. (S/M)
- [ ] **CSS**: grid, `position: sticky`, `:is/:where/:has`, `clamp()`, fuentes web y de iconos, WebP/AVIF/SVG, `srcset`. Primero auditar qué soporta ya el motor CSS. (M/L)
- [ ] **Interacción real con la página**: eventos de ratón/mando hacia el DOM (hace falta mapa nodo↔línea del maquetador; ya anotado como pendiente en CONTEXTO.md). (L)
- [ ] **Modo lectura**, exportar/importar marcadores, exportar log desde el navegador. (S/M)
- [ ] **Accesibilidad extra**: tamaño de fuente de la interfaz, foco más grueso. (S)
- [ ] **Indicador de carga y cancelar** (hoy la carga es síncrona). (M/L)

---

## 4. Vídeo (meta 3)
Estado: `!media` con el ayudante del PC (yt-dlp) saca un MP4 directo que la PS4 reproduce; esa es la ruta más realista a corto plazo. Solo MP4 (H.264 + AAC): WebM/MKV/FLV/AVI/HLS se rechazan al instante con aviso (v0.9.3).

- [ ] **Medir en la consola** `vscale` 3 (720p) y 4 (540p): antes ~11 fps a pantalla completa (presentar 1080p cuesta 43–58 ms por fotograma). Expectativa sin garantía: 720p/540p mejoran bastante; 1080p fluido por software es improbable.
- [ ] **Abrir `<video>` de la página en el reproductor propio** (botón/atajo) en vez de depender de `!media`. (M)
- [ ] **HLS (.m3u8)**: probar si el AvPlayer lo reproduce o resolver a MP4 con el ayudante. (M/L)
- [ ] **WebM/MKV**: remux/transcodificación en el PC o móvil (ffmpeg) y servir a la PS4. (L)
- [ ] Anuncios y ventanas emergentes en webs de vídeo: `window.open` completo y adblock editable por el usuario. (M)
- [ ] Subtítulos, recordar posición, siguiente automático, velocidad de reproducción. (M)
- [ ] Sugerir bajar el tamaño cuando los fps caen. (S)

---

## 5. Aprovechar la potencia de la PS4: juegos web y portes (meta 4)

### 5.1 Punto de partida (hechos del proyecto)
- Se dibuja con **SDL en modo software** (`SDL_RENDERER_SOFTWARE`): toda la pintura y el escalado los hace la **CPU**. La **GPU no se usa**.
- Medido en el log: presentar a pantalla completa cuesta 43–63 ms por fotograma; el coste dominante es rellenar píxeles.
- `<canvas>`: `getContext()` devuelve `null` hoy. No hay WebGL, ni WebAssembly (QuickJS no lo trae), y `navigator.getGamepads()` devuelve `[]`.
- Hardware (de memoria, sin verificar contra documentación): CPU de 8 núcleos Jaguar a ~1,6 GHz (lenta por núcleo) y GPU de ~1,8 TFLOPS (potente). Es decir, **la potencia está en la GPU, justo lo que el navegador no usa**.

### 5.2 Caminos posibles (de menor a mayor esfuerzo)
1. **Spike de renderer acelerado (primero, barato)**: añadir una opción de configuración `gpu` (por defecto 0) que intente `SDL_CreateRenderer` sin `SDL_RENDERER_SOFTWARE`, lo registre en el log y, si falla, vuelva a software. Si funciona, escalar y componer en GPU haría el vídeo 1080p mucho más fluido y aceleraría el repintado de la interfaz. **Riesgo**: no sé si el toolchain/SDL del CI (OpenOrbis) ofrece un backend acelerado; si el fallo es un cuelgue y no un error, se pierde la prueba. Por eso va como opción desactivada y con log. Necesito saber qué SDL/toolchain usa el CI exactamente.
2. **Canvas 2D** sobre el rasterizador propio (`getContext('2d')` con rectángulos, rutas, imágenes, texto, transformaciones). Viable para juegos 2D sencillos (puzles, plataformas básicas, juegos hechos con Phaser en modo Canvas). Límite: JavaScript interpretado. (L)
3. **Entrada y audio para juegos**: Gamepad API real con el mando, eventos de teclado/ratón hacia la página, `<audio>`/WebAudio básico. (M/L)
4. **WebGL**: exigiría el camino 1 (GPU) más enlazar cientos de funciones GLES2 a JavaScript y un buen rendimiento de JS. Proyecto enorme; no realista a corto plazo. (XL)
5. **WebAssembly** (juegos portados con Emscripten): QuickJS no lo trae; habría que integrar un intérprete de WASM (p. ej. wasm3), lo que corre de 10 a 50 veces más lento que nativo (estimación general, no medida aquí). (XL)

### 5.3 Conclusión honesta
- **Juegos web 2D simples** (Canvas 2D): alcanzable a medio/largo plazo, tras los caminos 2 y 3.
- **Juegos 3D/WebGL/WASM en el navegador**: no son realistas a corto-medio plazo con QuickJS sin JIT.
- **Para exprimir la PS4, la vía natural es el código nativo** (portes homebrew), no un navegador. El navegador puede ganar mucho con la GPU (camino 1), pero eso beneficia sobre todo al vídeo y a la interfaz, no convierte la consola en una plataforma de juegos web.

---

## 6. Orden de trabajo recomendado (fases)
**Fase 0 — Verificar**: compilar v0.9.4, firewall, probar sitios, enviar log (y la salida de `grep -n "ModuleLoader" quickjs.h`, y qué SDL/toolchain usa el CI).
**Fase 1 — Descargas fiables**: flujo de un paso, reposo/espacio/aviso, reanudación, página de descargas, perfiles por sitio.
**Fase 2 — Compatibilidad**: módulos ES + `import()`, iframes + `postMessage`, XHR/fetch completos, CSS.
**Fase 3 — Vídeo**: medir fps con `vscale`, abrir `<video>` propio, HLS, anuncios/pop-ups.
**Fase 4 — Android**: app ayudante.
**Fase 5 — Navegador diario**: sesión, zoom por sitio, buscar, autocompletar, modo lectura, copias de seguridad.
**Fase 6 — GPU/juegos (exploratoria)**: spike `gpu`, luego Canvas 2D, entrada y audio.

Regla de trabajo: no apilar más código sin probar; cada fase empieza con el log de la anterior.

---

## 7. Qué necesito del usuario
- Log de la prueba de v0.9.4 y lista de sitios probados con resultado.
- Salida de `grep -n "ModuleLoader" quickjs.h` (del clon de quickjs-ng del CI).
- Qué SDL / toolchain compila el CI (para valorar el camino GPU).
- Qué sitios de descarga y de vídeo importan más, para ordenar los perfiles.

---

# Apéndice A — Inventario detallado anterior (PENDIENTE.md)
Leyenda: `[ ]` pendiente, `[~]` hecho en v0.9.4 SIN PROBAR (o solo en parte), `[x]` verificado.

## 0. Primero: verificar v0.9.3 en la PS4
- [ ] Que el CI compile `jsx.cpp` con QuickJS real (no se pudo comprobar aquí).
- [ ] Fps del vídeo con `vscale` 3 y 4 (antes ~11 fps a pantalla completa). Tecla S / botón "Tam" / botón 2 del mando.
- [ ] Que `.webm` muestre el aviso al instante y no espere 14 s.
- [ ] Alto contraste legible en todas las pantallas (inicio, ajustes, descargas, verificación, errores).
- [ ] Cursor 150 % / 200 % sin restos al moverlo (actualización parcial de la interfaz).
- [ ] Cabeceras/Referer/Origin en fetch y XHR: ¿arreglan mocha.my y akirabox? (hipótesis)
- [ ] pkg-zone.com carga rápido (ya sin esperar 22 s) y muestra el aviso de módulos ES.
- [ ] Ayudante del PC: abrir el puerto 8765 en el firewall y comprobar `/ping`.

---

## 1. Ayudante en Android (alternativa al PC para captchas)
Objetivo: misma función que `tools/solver/psb_solver.py` pero en el móvil.

**Diseño recomendado: app Android propia (Kotlin + WebView)**
- [ ] Servidor HTTP embebido (NanoHTTPD o `ServerSocket`) que habla el mismo protocolo: `/ping`, `/open?url=&auto=`, `/status`, `/state`, `/media`. Así la PS4 no cambia nada (`!solver IP:8765`). (M)
- [ ] WebView con JavaScript, cookies de terceros y DOM storage activados. Turnstile corre en Chromium real. (S)
- [ ] `CookieManager` → texto Netscape (mismo formato que `build_state`); User-Agent real del WebView como línea `#UA`. (S)
- [ ] `DownloadListener` del WebView → líneas `#DL` (url, nombre, tamaño) y cancelar la descarga en el móvil. (S)
- [ ] Interceptar respuestas con `shouldInterceptRequest` para detectar `.pkg`/`octet-stream` que el listener no vea. (M)
- [ ] Filtrar cookies por los sitios abiertos (igual que `filter_cookies` del PC). (S)
- [ ] Aceptar solo clientes de la red local (+ opción `--allow` con la IP de la PS4). (S)
- [ ] Servicio en primer plano con notificación y `WakeLock`/`WifiLock` para que Android no lo mate. (M)
- [ ] Pantalla con la IP:puerto bien grande para escribirla en la PS4, y botón copiar. (S)
- [ ] `/media`: sin yt-dlp nativo. Opciones: (a) dejarlo solo en el PC, (b) Termux + yt-dlp aparte, (c) extraer el `<video src>` del WebView. (M/L)
- [ ] Workflow de GitHub Actions que compile el APK (Gradle) junto al PKG. (M)
- [ ] Documentar: móvil y PS4 en el mismo Wi-Fi (los enlaces suelen ir atados a la IP pública); routers con "aislamiento de clientes" lo impiden.

Limitaciones conocidas: Android mata apps en segundo plano; no hay forma de probarlo aquí, saldrá sin verificar y se ajusta con logs. Playwright/Termux no sirve para captchas con interfaz.

---

## 2. Descargas (lo que más se nota con PKGs grandes)
- [ ] **Impedir el reposo de la PS4** mientras haya descargas activas (API de la consola para evitar el apagado automático/pantalla). (S/M, solo probable en PS4)
- [ ] **Cola de descargas** con reanudación tras cerrar y reabrir el navegador (guardar progreso y `Range`). (M)
- [ ] **Página de descargas**: pausa, reanudar, cancelar, reordenar, velocidad y tiempo restante. (M)
- [ ] **Espacio libre** antes de empezar; aviso si no cabe. (S)
- [ ] **Aviso al terminar** (sonido + mensaje aunque estés en otra pantalla). (S)
- [ ] **Reintentar enlaces caducados**: si da 403/410, volver a pedir el enlace al ayudante. (M)
- [ ] **Verificar integridad** (tamaño final y, si la página da hash, SHA-256/MD5). (M)
- [ ] **Nombre de archivo** limpio (caracteres raros, `filename*=UTF-8''`). Comprobar. (S)
- [ ] Límite de velocidad opcional para no saturar el Wi-Fi mientras se juega. (S)

---

## 3. Navegación y calidad de vida
- [ ] **Restaurar sesión**: pestañas abiertas y posición de scroll tras un cierre inesperado. (M)
- [ ] **Recordar scroll y selección** al volver atrás/adelante. (S/M)
- [ ] **Zoom por sitio** (recordar el zoom de cada dominio). (S)
- [ ] **Modo lectura** (quitar menús/anuncios, texto grande). (M)
- [ ] **Buscar en la página** (Ctrl+F / botón del mando). (M)
- [ ] **Autocompletar** en la barra con historial y marcadores. (S/M)
- [ ] **Exportar/importar** marcadores, historial y configuración (copia de seguridad). (S)
- [ ] **Exportar el log** desde el navegador (sin sacar el USB). (S)
- [~] **Páginas de error con "Reintentar"** y causa legible (timeout, DNS, TLS, 403) en vez del código `0x8043...`. (S)
- [ ] **Indicador de carga** y poder cancelar con Esc/círculo (hoy la carga es síncrona). (M/L)
- [ ] **Teclado en pantalla** mejor para URLs (atajos `.com`, `https://`). (S/M)
- [ ] **Pestañas**: lista visible, cerrar otras, reabrir cerrada. Comprobar qué existe. (S/M)
- [ ] **Accesibilidad extra**: tamaño de fuente de la interfaz, resaltado del foco más grueso, lectura de contraste en páginas con colores propios. (S)
- [ ] **Gestión de cookies**: ver/borrar por sitio. (S)
- [ ] **Adblock**: lista editable por el usuario y botón "desbloquear en este sitio". (S/M)

---

## 4. Vídeo
- [ ] **Subtítulos** (VTT/SRT externos). (M/L)
- [ ] **Recordar la posición** de cada vídeo y reanudar. (S)
- [ ] **Lista de reproducción / siguiente** automático. (M)
- [ ] **Velocidad de reproducción** (0,75x–1,5x) si el AvPlayer lo permite. Comprobar. (M)
- [ ] **HLS (.m3u8)**: hoy se avisa y se rechaza; probar si el AvPlayer lo reproduce o resolver a MP4 con el ayudante. (M/L)
- [ ] **WebM/MKV**: no los lee el AvPlayer; opción de remuxar/transcodificar en el PC o móvil (ffmpeg) y servirlos a la PS4. (L)
- [ ] **Calidad automática**: si los fps caen, sugerir bajar el tamaño (720p → 540p) con un aviso. (S)
- [ ] **Etiqueta `<video>` en páginas**: abrir en el reproductor propio con un botón. Comprobar qué existe. (M)

---

## 5. Compatibilidad web (más sitios en general)
Ordenado por impacto estimado.

### 5.1 Lo más importante
- [ ] **Módulos ES (`<script type="module">`)**: hoy se omiten y páginas Vite/Inertia/SvelteKit/Astro quedan vacías (pkg-zone.com). Usar `JS_SetModuleLoaderFunc` + carga de `import` por red + `import.meta`. **Riesgo**: la firma exacta en quickjs-ng 0.18 no se pudo comprobar sin red; comprobar `quickjs.h` del CI primero. (L)
- [ ] **`import()` dinámico** (lo usan Next.js/Webpack/Vite para dividir código). Va con lo anterior. (M)
- [~] **`document.cookie` real** conectado al almacén de cookies (muchas páginas de captcha/descarga lo leen y escriben). Comprobar estado actual. (S/M)
- [ ] **`<iframe>`**: carga de documentos anidados y `postMessage` entre ellos (captchas, reproductores embebidos). Hoy parece no soportado. (L)
- [ ] **`window.open` / `location.reload` / navegación por JS** completos. Comprobar. (M)
- [ ] **`history.pushState`/`popstate`** coherentes con el botón atrás (SPA). Existe `pushState`; verificar. (S/M)

### 5.2 APIs que faltan o hay que verificar
Ya existen (según el prelude): IntersectionObserver, ResizeObserver, MutationObserver, matchMedia, getComputedStyle, requestIdleCallback, structuredClone, queueMicrotask, AbortController, FormData, URLSearchParams, Blob, customElements, Shadow DOM, localStorage/sessionStorage, crypto, TextEncoder/Decoder, DOMParser, Notification (stub).
Por añadir o verificar:
- [~] `File` / `FileReader` / `URL.createObjectURL` (S/M)
- [~] `WebSocket` (stub que falle limpio o implementación real) (M/L)
- [~] `EventSource` (SSE), `BroadcastChannel` (S/M)
- [~] `indexedDB` (stub en memoria para que no rompa librerías) (M)
- [~] `Intl` (DateTimeFormat/NumberFormat/PluralRules básicos; QuickJS-ng no lo trae completo) (M)
- [~] `ReadableStream` / `response.body` / `fetch` con streaming (M)
- [~] `Request` como clase, `Response.headers` completas, `fetch` con `credentials`/`signal`/`redirect` (S/M)
- [ ] `XMLHttpRequest`: `responseType` arraybuffer/blob/json, eventos `readystatechange` 2 y 3, `progress` (S/M)
- [ ] `FormData` multipart real (hoy se convierte a urlencoded) (M)
- [~] `XMLSerializer`, `TreeWalker`, `NodeIterator`, `Range` completo (S/M)
- [ ] `navigator.clipboard`, `navigator.userAgentData`, `navigator.language(s)`, `screen.*`, `visualViewport` (S)
- [ ] `CSS.supports`, `CSSStyleSheet` insertRule/deleteRule (S/M)
- [ ] `Worker` real o stub que no rompa (hay una referencia; verificar) (M)
- [ ] Métodos HTTP `PUT`/`DELETE`/`PATCH` en fetch/XHR (hoy solo GET/HEAD/POST) (S)

### 5.3 CSS / render
- [ ] Probar y ampliar: flexbox y grid (pasos de columnas/filas, `gap`), `position: sticky`, `calc()`, variables CSS, `@media` (ancho, `prefers-color-scheme`), `:is()`/`:where()`/`:has()`, `clamp()`. Comprobar qué soporta el motor CSS actual. (M/L)
- [ ] Fuentes web (`@font-face`) y fuentes de iconos (Font Awesome, Material Icons) que hoy salen como cuadrados. (M)
- [ ] Imágenes: WebP/AVIF/SVG, `srcset`/`<picture>`, `loading=lazy`. Comprobar formatos. (M)
- [ ] Páginas con diseño móvil: opción de User-Agent móvil/escritorio rápida por sitio. (S)

### 5.4 Red / seguridad
- [ ] **TLS**: certificados/algoritmos que fallen en sceSsl; mensaje claro y opción "continuar de todos modos" por sitio. (M)
- [ ] **HTTP/2/compresión**: `br` (Brotli) y `zstd` si el servidor los exige. Comprobar `Accept-Encoding`. (M)
- [ ] **Redirecciones** largas y `<meta refresh>` en bucle: límite y aviso. (S)
- [ ] **Timeouts configurables** (conexión/respuesta) en Ajustes. (S)
- [ ] **Registro de compatibilidad**: tabla `sitio → resultado` en `docs/` actualizada con cada prueba (vikingfile, onefile, mochalife/mocha.my, akirabox, 1fichier, pkg-zone, psx-place...). (S)

### 5.5 Sitios de descarga concretos
- [ ] **vikingfile**: Turnstile → solo con ayudante (PC/Android) + firewall abierto. Documentar el flujo.
- [ ] **onefile / mochalife (mocha.my) / akirabox**: re-probar con v0.9.3 (cabeceras + Referer). Si fallan, capturar la petición real con el ayudante y replicarla.
- [ ] **pkg-zone.com**: depende de módulos ES.
- [ ] Añadir a cada sitio un "perfil" (UA, cabeceras, esperas) opcional en un archivo editable.

---

## 6. Ayudante del PC (`psb_solver.py`)
- [ ] Comprobación automática de firewall al arrancar y mensaje con el comando exacto (ya se imprime; falta autodetección). (S)
- [ ] Empaquetar como `.exe` (PyInstaller) para no instalar Python/Playwright a mano. (M)
- [ ] Modo sin ventana (headless) cuando no haga falta el captcha, y modo con ventana cuando sí. (S)
- [ ] Descubrimiento automático por mDNS/UDP: la PS4 encuentra el ayudante sin escribir la IP. (M)
- [ ] Token compartido (emparejado una vez) en vez de solo filtrar por IP. (M)

---

## 7. Calidad del proyecto (para mantenerlo)
- [ ] Tests de `jsx` en CI con QuickJS real (hoy solo se prueban en PC si hay `quickjs.h`). (M)
- [ ] Test del prelude en Node con un DOM simulado para cazar errores de JS antes de la PS4. (M)
- [ ] CI que ejecute los tests de `tools/tests/` en cada push. (S)
- [ ] Mantener `CONTEXTO.md` y `docs/Vx.y.z.md` al día en cada versión.
- [ ] Respaldo si la compilación con JS falla: avisar claramente (hoy cae en `PSB_NO_JS` sin que se note). (S)

---

## 8. Errores conocidos / dudas abiertas
- `0x80431068` interpretado como TIMEOUT (de memoria, sin confirmar).
- mocha.my: error `not a function` en el decodificador de emails de Cloudflare; v0.9.2 añadió `compareDocumentPosition`, sin verificar.
- vikingfile: errores `aclib is not defined` por el adblock; inofensivos, pero hay que confirmarlo.
- Rendimiento de vídeo: el coste dominante es rellenar píxeles de pantalla (~16 ms por millón); cualquier mejora pasa por pintar menos área.
- `JsHost.request` cambió de firma en v0.9.3: revisar cualquier código externo que lo use.

---

## 9. Orden sugerido
1. Verificar v0.9.3 y recoger el log.
2. Descargas: impedir reposo, cola/reanudación, página de descargas, aviso final (sección 2).
3. App Android (sección 1).
4. Compatibilidad: `document.cookie`, XHR/fetch completos, stubs de APIs que rompen librerías (5.2).
5. Módulos ES e `import()` (5.1), con la comprobación previa de la API del CI.
6. Iframes y `postMessage` (5.1).
7. Resto de calidad de vida y vídeo.

---

## 10. Registro de avances (v0.9.4)
- `[~]` Stubs/APIs nuevas en el prelude: WebSocket, EventSource, BroadcastChannel, indexedDB, Intl básico, File, FileReader (falla limpio), ReadableStream mínimo, postMessage, XMLSerializer, visualViewport, clase Request. Solo XMLSerializer/ReadableStream/Intl/Request hacen algo útil; el resto fallan con un error normal para que la página use su alternativa.
- `[~]` `document.cookie` ya estaba conectado al almacén de cookies (no hacía falta tocarlo).
- `[~]` Páginas de error con causa legible y "Reintentar".
- Descartado por ahora: `PUT`/`DELETE`/`PATCH` (no pude comprobar las constantes de `sceHttp`).
- `TreeWalker`/`NodeIterator`, `Range` completo, iframes y módulos ES siguen pendientes.

---

## 11. Registro de avances (v0.9.5, primera tanda)
- `[~]` Sondeo de SDL/GPU (CI `probe_sdl` → `logs/sdl_probe.txt`; consola: líneas `[gpu]` y sección en `about:bench`). Responde al "spike de renderer acelerado" de §5.2-1 sin riesgo.
- `[~]` Opción `gpu` (0 por defecto): intenta un renderer acelerado solo si SDL declara uno; protección `gpu_try.txt` (vuelve a software si la app cierra antes de 30 s).
- Decisión del usuario: no probar v0.9.4 y saltar a v0.9.5. Riesgo aceptado: varios cambios sin probar a la vez; ante un fallo de CI pedir `build.log`.
- `[~]` Tanda 2 (hecha, sin probar): Canvas 2D por software + imágenes (`new Image`), `requestAnimationFrame` con marca de tiempo, Gamepad API, teclado/ratón/mando hacia la página y modo juego (F2). CI con fallback en 3 niveles (sin Canvas / sin JS).
- Pendiente (tanda 3): audio, Path2D, fuentes reales en canvas, rueda/táctil, optimizar con datos de la PS4.
