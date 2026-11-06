# Plan por fases

| Fase | Objetivo | Estado |
|------|----------|--------|
| 0 | Hello de red: HTTP/HTTPS con `sceHttp`, visor de texto, CI que genera PKG | **Hecho y probado en PS4** (v0.1.2) |
| 1a | Visor con enlaces navegables, barra de URL (teclado nativo `libSceKeyboard`), ratón (`libSceMouse`), mando, historial, página de inicio | Código listo y compilado (v0.2.0); **pendiente de probar en PS4** |
| 1b | Texto con FreeType (tildes y símbolos) y fuente proporcional | Sin empezar |
| 1c | Imágenes PNG/JPEG (libpng, libjpeg-turbo) y formularios GET simples | Sin empezar |
| 2 | Reconocimiento de NetSurf en CI: clonar, listar dependencias, compilar las librerías `libnsfb`, `libcss`, `libdom`, `hubbub`... como `.a` con `orbis-cc` | Sin empezar |
| 3 | Frontend framebuffer de NetSurf sobre SDL2 de OpenOrbis + *fetcher* propio basado en `sceHttp` | Sin empezar |
| 4 | Extras: marcadores, descargas a `/data`, almacén de certificados propio, caché | Sin empezar |

## Por qué esta secuencia

- La fase 0 es la prueba de que lo más arriesgado (red + TLS) funciona. Si falla, se sabe pronto y con un log pequeño.
- La fase 1 es un **plan B real**: un visor propio (HTML simple, texto + enlaces + imágenes) puede ser útil aunque
  NetSurf resulte demasiado costoso de portar.
- NetSurf (fases 2-3) es una cadena larga de librerías propias en C; conviene validarlas una por una en el CI antes de
  integrarlas.

## Riesgos conocidos

1. **Cabeceras de OpenOrbis incompletas:** `sceSslTerm()` está declarada sin parámetros (se llama con un *cast*), y
   varias funciones de `Http.h` están como `void f();` sin firma (p. ej. `sceHttpSetAutoRedirect`, `sceHttpSetRecvTimeOut`),
   por eso las redirecciones se gestionan a mano leyendo `Location`.
2. **TLS/certificados:** el sample oficial salta la verificación. No se sabe de antemano qué `verifyErr` devuelve la consola
   para cada web; el log de la fase 0 lo dirá.
3. **Memoria y pools:** los tamaños de pool (`net` 16 KB, `ssl` 300 KB, `http` 1 MB) son los habituales en homebrew, pero
   no están validados aquí.
4. **Renderer por software:** SDL 2.0.9 en PS4 solo dio render por software en el proyecto de Ren'Py. El lienzo se sube a
   una textura solo cuando cambia algo; para páginas grandes habrá que cuidar el rendimiento.
5. **Fuente:** la 8x8 solo cubre ASCII; el resto de caracteres sale como `?` hasta la fase 1 (FreeType).
6. **E/S de ficheros con rutas de la app:** en CPython hubo problemas con `open()` sobre `/app0` (bug de `stat`/`O_CLOEXEC`);
   en este proyecto solo se escribe en `/data/psbrowser/`, que sí funcionó con `fopen`.
