# Contexto del proyecto psbrowser (para continuar en otro chat o con otra IA)

> Pega este documento al inicio de una conversación nueva (con Claude u otra IA), junto con el zip del repo. Está pensado para que alguien sin contexto previo pueda seguir.
> Fecha de este documento: 5 de noviembre de 2026. Idioma de trabajo: español.

## 1. Qué es y para qué
Navegador homebrew **de texto** para PS4 (con GoldHEN), construido con el toolchain **OpenOrbis** (LLVM, `x86_64-pc-freebsd12-elf`). Objetivo a largo plazo: portar **NetSurf**; plan B: hacer crecer el visor propio.
Es un proyecto personal / de aprendizaje. El usuario compila con **GitHub Actions** (Linux) y prueba los PKG en su PS4 real; entre pruebas puede pasar tiempo.

## 2. Cómo trabajamos
- Claude (o la IA) escribe el código y los workflows; el usuario sube los cambios a GitHub, ejecuta el CI, instala el PKG en su PS4 y devuelve el **log** `/data/psbrowser/log.txt` (y a veces `last.html`).
- Si cambia un workflow, hay que dar al usuario los comandos `git` exactos para subirlo. No añadir al commit carpetas de desarrollo sin seguimiento.
- La compilación se hace en el CI de GitHub, no en el PC del usuario.
- Repo del proyecto anterior (descartado) del que se reutilizó mucho: `https://github.com/sacksso/Sackso_Ren.py_ps4-testing` (port de CPython/Ren'Py a PS4: SDL2, CI, teclado/ratón nativos).

## 3. Estado actual
| Fase | Qué es | Estado |
|------|--------|--------|
| 0 | Hello de red (HTTP/HTTPS con `sceHttp`) + visor de texto | **Probada en PS4 real** (v0.1.2): todas las URLs de prueba cargan |
| 1a | Visor con enlaces, barra de URL, historial, teclado, ratón, mando (v0.2.0) | Probado parcialmente: pantalla, red y teclado/ratón responden; el log mostró el ratón con deltas de ±1 (muy lento) |
| 1a+ | v0.3.0: configuración (`about:settings`: sensibilidad, aceleración, rueda, inicio), marcadores, barra de botones, enlaces `[video]`/`[video incrustado]`, `<meta refresh>` | Compila (sintaxis); **sin probar en consola** |
| 1b | FreeType (tildes/símbolos) | Pendiente |
| 1c | Imágenes PNG/JPEG, formularios GET | Pendiente |
| 2-3 | Reconocimiento y port de NetSurf (fetcher propio sobre `sceHttp`, frontend framebuffer/SDL2) | Pendiente |

## 4. Estructura del repo
```
app/main.cpp        interfaz: estado del navegador, historial, dibujo, entrada (teclado/ratón/mando)
app/netclient.*     cliente HTTP/HTTPS sobre libSceHttp/libSceSsl (redirecciones a mano, límite de cabeceras)
app/htmldoc.*       HTML -> Document (líneas con spans: estilo + enlace), HTML->ASCII, entidades
app/url.*           resolver URLs relativas, normalizar lo escrito en la barra, búsqueda DuckDuckGo
app/canvas.h        lienzo ARGB en memoria + texto con fuente 8x8 (se sube a una textura SDL solo si cambia algo)
app/mouse.*         libSceMouse cargada en runtime (copiado del repo de Ren'Py)
app/log.h           psb_log() -> /data/psbrowser/log.txt
third_party/font8x8_basic.h   fuente 8x8 de dominio público (tipo cambiado a unsigned char para C++)
.github/workflows/build-pkg.yml   CI: descarga toolchain OpenOrbis, compila, genera PKG (artefactos: psbrowser-pkg, logs)
docs/               PLAN.md, NETSURF.md, PRUEBAS.md, este CONTEXTO.md
```
Identidad del PKG: título `PS Browser`, Title ID `PSBR00001`, Content ID `IV0000-PSBR00001_00-PSBROWSER0000000`.

## 5. Cómo se compila (CI)
`build-pkg.yml` (manual, *workflow_dispatch*, inputs `toolchain_tag` y `strict_tls`, este último `true` por defecto):
1. Instala clang, lld, make; instala **libssl1.1** (PkgTool.Core la necesita en Ubuntu 24.04) y `CLR_OPENSSL_VERSION_OVERRIDE=1.1`.
2. Descarga el último release de `OpenOrbis/OpenOrbis-PS4-Toolchain` (probado con **v0.5.4**, asset `toolchain-llvm-18.tar.gz`).
3. Copia `samples/` a un directorio temporal, crea `psbrowser/` a partir del Makefile del sample **SDL2** (+ `sce_sys`, `sce_module`), copia `app/*` y `third_party/`, copia `keyboard.h/.cpp` del sample `keyboard`, y con `sed` fija TITLE/TITLE_ID/CONTENT_ID, vacía `COMMONFILES` y añade las libs `-lSceNet -lSceSsl -lSceHttp -lSceKeyboard`.
4. `make EXTRAFLAGS=-DPSB_STRICT_TLS` genera `eboot.bin` y el `.pkg`. Se sube PKG + sha256, y logs.
Verificado en local (sin consola): el código compila y enlaza con ese toolchain; el PKG lo genera el CI.

## 6. Hallazgos técnicos importantes (probados en PS4 salvo que se diga)
- **Red/TLS con el sistema:** `sceHttp`/`sceSsl` de OpenOrbis funcionan: DNS, HTTP, HTTPS, chunked y redirecciones. Pools que funcionan: net 16 KB, ssl 300 KB, http 1 MB. Módulos a cargar con `sceSysmoduleLoadModuleInternal`: NET, HTTP, SSL.
- **Certificados:** la consola valida certificados reales: `verifyErr=0` en todos los HTTPS probados. Por eso el modo por defecto es TLS estricto.
- **Límite de cabeceras de respuesta:** por defecto `libSceHttp` falla con `sendRequest 0x80431073` en páginas con cabeceras grandes (las `/wiki/` de Wikipedia traen ~5,7-6,4 KB). **Solución comprobada:** `sceHttpSetResponseHeaderMaxSize(tpl, 128*1024)` (devolvió 0). La causa exacta del código de error no está confirmada oficialmente; la solución sí funciona.
- **Cabeceras de OpenOrbis con firmas incompletas:** varias funciones de `Http.h` están declaradas como `void f();` (p. ej. `sceHttpSetResponseHeaderMaxSize`, `sceHttpSetAutoRedirect`) y `sceSslTerm()` sin parámetros; se llaman con *cast* a la firma real (`int f(int, size_t)`, `int sceSslTerm(int)`). Las redirecciones se resuelven a mano leyendo `Location`.
- **SDL 2.0.9 de PS4:** solo ve el mando; render por software; 1920x1080. Teclado y ratón van con APIs nativas (`libSceKeyboard` vía la clase `Keyboard` del sample; `libSceMouse` cargada en runtime con `sceKernelLoadStartModule`/`sceKernelDlsym`; ambos funcionaron en el proyecto de Ren'Py).
- **Sistema de archivos:** escribir en `/data/psbrowser/` con `fopen` funciona.
- **Fuente 8x8:** el array original era `char` y no compila en C++ con 0xFF; está cambiado a `unsigned char`.
- **Bug de shell del contenedor de desarrollo:** `/bin/sh` es dash (no hay *brace expansion* en `mkdir -p a/{b,c}`).

## 7. Controles de la fase 1a (sin probar)
- Teclado: F6 / Ctrl+L = barra de URL; escribir en la página también la abre; Enter = ir; Esc = cancelar; Tab / Shift+Tab = enlace siguiente/anterior; Enter = abrir; Backspace o Alt+Izq = atrás; Alt+Der = adelante; F5 = recargar; flechas, RePág/AvPág, Inicio/Fin, Espacio = desplazar.
- Ratón: clic izquierdo = abrir enlace o enfocar la barra; clic derecho = atrás; rueda = desplazar (**la dirección es una suposición**).
- Mando: cruceta arriba/abajo = enlace anterior/siguiente; izquierda/derecha = atrás/adelante; botones 0 abrir, 1 atrás, 2 recargar, 3 inicio, 4/5 página (**índices supuestos**; el log anota `boton N` y `hat N`); stick = desplazar.
- Una entrada que no parece URL se busca en `https://html.duckduckgo.com/html/?q=...` (no probado).

## 8. Limitaciones y riesgos conocidos
- Sin JavaScript, imágenes, formularios, enlaces internos `#ancla`, descargas, cookies ni caché.
- Texto monoespaciado 8x8 ampliado x2; los caracteres no ASCII se pliegan a ASCII (acentos -> letra base) o salen como `?`. FreeType queda para la fase 1b.
- Cuerpos limitados a 2 MB; la carga es **bloqueante** (la pantalla no responde mientras descarga).
- El dibujo redibuja el lienzo completo cuando algo cambia (puede ser lento en páginas grandes; sin medir en la consola).
- Teclado: se asume que `Key2Char` devuelve ASCII y que las teclas siguen los códigos HID de `orbis/_types/keyboard.h`. Si algo no responde, mirar `tecla:` en el log.

## 9. Próximos pasos propuestos
1. Probar la v0.2.0 y devolver `log.txt` (ver `docs/PRUEBAS.md`, "Prueba 4"). Corregir mapeos de rueda, botones y velocidad del ratón según el log.
2. Fase 1b: FreeType (el `build-deps.yml` del repo de Ren'Py ya compilaba FreeType/libpng/libjpeg como `.a` para PS4).
3. Fase 1c: imágenes, formularios GET (para buscar sin salir a la barra de URL), enlaces `#ancla`.
4. Fase 2: *recon* de NetSurf en CI (clonar, compilar sus librerías `libparserutils`, `hubbub`, `libcss`, `libdom`, `libnsfb`... con `orbis-cc`), según `docs/NETSURF.md`. Nota: `git.netsurf-browser.org` no era accesible desde el entorno de desarrollo de Claude; hay que confirmarlo en el CI.

## 10. Prompt sugerido para una IA nueva
"Continúa el proyecto psbrowser (navegador homebrew para PS4, OpenOrbis). Lee `docs/CONTEXTO.md`, `docs/PLAN.md` y `docs/PRUEBAS.md` del zip adjunto. Estado: fase 0 probada en PS4, fase 1a compilada pero sin probar. Pídeme el `log.txt` de la PS4 antes de cambiar el mapeo de entrada. Si cambias el workflow, dame los comandos `git` exactos. Responde en español y sé honesto sobre lo que no se haya probado."
