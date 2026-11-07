# JavaScript en psbrowser: estado, límites y plan

> Estado a 7 nov 2026: **no hay JavaScript todavía.** v0.7.0 solo prepara el terreno (sonda en el CI). Este documento explica por qué no se puede "añadir un motor y ya", qué sí se puede lograr y en qué orden.

## 1. Por qué es el punto más grande
Un motor JS (QuickJS, Duktape…) solo ejecuta el *lenguaje*. Los sitios esperan además un **DOM** (árbol de elementos que el script lee y modifica), **eventos**, **temporizadores**, **red** (`fetch`/`XMLHttpRequest`), **CSS/layout** (los scripts leen tamaños y alternan clases para mostrar u ocultar) y, en las webs modernas, `canvas`, `localStorage`, `Promise`/módulos, `IntersectionObserver`, Web Components...

Hoy `htmldoc.cpp` convierte el HTML a líneas ya maquetadas **en una sola pasada, sin árbol DOM ni CSS**. Un script no tendría nada que modificar. Por eso el trabajo real no es "meter QuickJS" sino, por orden:

| Fase | Qué | Esfuerzo | Resultado visible |
|---|---|---|---|
| 0 | **Sonda en el CI** (hecha en v0.7.0, `probe_js`): baja QuickJS-ng, lista archivos/API y prueba a compilar cada `.c` con los flags de OpenOrbis | pequeño | `logs/quickjs_probe.txt` dice si compila y con qué archivos |
| 1 | **Árbol DOM**: parsear HTML a nodos (`dom.h/.cpp`) y maquetar desde el árbol (reescribir la parte central de `htmldoc.cpp`, 858 líneas) | grande | Igual que hoy, pero sobre un árbol; base de todo lo demás |
| 2 | **CSS lite**: `<style>`/`<link>`, selectores simples (`tag`, `.clase`, `#id`, descendiente), `display`, `color`, `background`, `text-align`, márgenes | grande | Mejora sitios SSR (XenForo, blogs) incluso **sin JS** |
| 3 | **Motor** QuickJS enlazado + `console`, `window`, `document` (`getElementById`, `querySelector(All)`, `createElement`, `appendChild`, `textContent`, `innerHTML`, atributos, `classList`), `setTimeout/setInterval` movidos desde el bucle principal, límite de tiempo y de memoria por script | grande | Páginas con "progressive enhancement": menús, pestañas, contadores, rellenado de contenido simple |
| 4 | Eventos (`click`, `submit`, `DOMContentLoaded`, `load`) y `fetch`/`XMLHttpRequest` sobre `http_request` en un hilo | medio | Búsquedas en vivo, "cargar más", formularios AJAX simples |
| 5 | Interruptor en ajustes: JS activado/desactivado por sitio, y registro de errores en `log.txt` | pequeño | Seguridad y diagnóstico |

## 2. Qué NO se conseguirá con este camino (sé realista)
- **Reddit, YouTube, X, Instagram, Gmail, apps SPA (React/Vue pesados)**: necesitan layout CSS completo, `canvas`/WebGL, Web Components, `IntersectionObserver`, `Proxy`/`async` avanzados, WebSockets... y cientos de APIs. No hay motor pequeño que las cubra.
- **Apps de canvas** como `paint.js.org` o `kleki.com` (las que probaste: salen en blanco): requieren `<canvas>` 2D y eventos de puntero. `canvas` 2D básico se podría hacer después de la fase 3 (tenemos un lienzo ARGB propio), pero es otro proyecto.
- **Cloudflare/captcha**: exigen una huella de navegador real; no se resuelven con un motor JS propio.
- **Vídeo**: no depende de JS sino de `libSceAvPlayer` (experimento aparte, ver CONTEXTO §7.3).

## 3. La alternativa realista para sitios modernos (camino C del CONTEXTO)
Un **servidor de renderizado remoto** en tu PC/móvil/VPS (Chromium sin cabeza): la PS4 pide `/render?url=…`, recibe imágenes de la página real ya con JS, un mapa de zonas clicables y campos, y devuelve clics/teclas. Es la **única** vía a Reddit/YouTube/Gmail. Reutiliza `http_request` y el decodificador PNG/JPEG, y aprovecha lo ya hecho (pestañas, teclado en pantalla, menú contextual). Costes: necesita un equipo encendido y hay latencia por página. Puede combinarse con las fases 1–5 (las páginas sencillas se renderizan localmente; las difíciles, en remoto).

## 4. Detalles técnicos ya pensados para la fase 3
- **Motor elegido:** QuickJS-ng (MIT, C, ~60k líneas, sin JIT → funciona en la PS4 sin memoria ejecutable). Se compilaría en el CI como `.c` junto al resto (el Makefile de OpenOrbis ya coge `$(PROJDIR)/*.c`). La sonda comprueba justo eso.
- **Descarga en el CI** (no en el repo): `git clone --depth 1 --branch <tag fijo>`; si falla, compilar con `-DPSB_NO_JS` y seguir sin JS (el PKG nunca debe romperse por esto).
- **Pila:** la del hilo principal es suficiente; si se ejecuta en un hilo, evitar pilas pequeñas (ver CONTEXTO §5: `std::thread` pila pequeña) y fijar `JS_SetMaxStackSize`.
- **Seguridad:** sin acceso a archivos; `JS_SetMemoryLimit` (p. ej. 48 MB), `JS_SetInterruptHandler` (p. ej. 150 ms por tarea y 2 s por página) para que un script malo no congele la consola.
- **Temporizadores:** una cola ordenada que el bucle principal vacía cada frame (como `pumpImages`).
- **Reentrada:** tras cada tarea JS, si el árbol cambió → re-maquetar solo si hubo cambios (marcar `g_dirty`).

## 5. Qué necesito para empezar la fase 3 (de ti)
1. Ejecutar el CI de v0.7.0 y pasarme `logs/quickjs_probe.txt` (dice si QuickJS-ng compila en el toolchain y qué archivos hay).
2. Decidir si prefieres **(A)** empezar por el árbol DOM + CSS lite (mejora sitios incluso sin JS, es la base) o **(B)** el servidor de renderizado remoto (da Reddit/YouTube antes, pero depende de un PC). Se pueden hacer los dos; el orden depende de qué sitios quieres ver primero.
