# Ruta a NetSurf

> Nota honesta: desde el entorno donde se montó este repo **no se pudo acceder a `git.netsurf-browser.org`**, así que
> NetSurf no se ha clonado ni compilado todavía. Lo siguiente sale de su documentación pública y de los Makefile de
> distribuciones; hay que confirmarlo en el CI (fase 2).

## Piezas

- **Librerías propias** (se compilan en este orden, todas con su `buildsystem`): `libparserutils`, `libwapcaplet`,
  `hubbub` (HTML5), `libcss`, `libdom`, `libnsgif`, `libnsbmp`, `libsvgtiny`, `libnsutils`, `libnslog`, `libnspsl`, `libutf8proc`,
  y `libnsfb` para el frontend framebuffer.
- **Frontend framebuffer:** `make TARGET=framebuffer`. Habla con `libnsfb`, que abstrae el dispositivo de salida/entrada y
  tiene un backend SDL. Hay que comprobar contra qué versión de SDL está escrito el backend (la documentación antigua
  habla de SDL 1.2) y si basta adaptarlo a SDL2 2.0.9 de OpenOrbis.
- **Red:** el *fetcher* estándar usa libcurl y OpenSSL. En distribuciones como Buildroot libcurl es opcional en el paquete,
  así que NetSurf puede compilarse sin él. Idea: escribir un *fetcher* nuevo que use `sceHttp` (ya probado en la fase 0) y
  registrarlo en lugar del de curl.
- **Fuentes:** el frontend framebuffer soporta fuentes internas o FreeType. FreeType ya se compiló como `.a` para PS4 en el
  proyecto de Ren'Py (`build-deps.yml`); aquí habría que reutilizar ese paso y validarlo con un enlace real.
- **Imágenes:** libpng y libjpeg-turbo (también en `build-deps.yml`), más `libnsgif`/`libnsbmp` propias.
- **JavaScript:** NetSurf usa Duktape y es limitado; muchas webs modernas no funcionarán. El valor está en páginas simples.

## Trabajo de la fase 2 (CI)

1. Workflow `recon-netsurf.yml`: clonar los repos oficiales, listar dependencias y versiones, guardar `logs`.
2. Wrappers `orbis-cc` / `orbis-cxx` (ya existen en el repo de Ren'Py; incluyen el parche del `struct stat` y `sys/endian.h`).
3. Compilar cada librería con `continue-on-error` y un resumen OK/FALLO, igual que `build-deps.yml`.
4. Herramientas que NetSurf compila para el *host* durante el build (p. ej. `convert_image`, que necesita libpng del host):
   separar bien build-host y build-target.

## Criterio para seguir o abandonar NetSurf

Si en la fase 2 más de ~3 librerías propias no compilan o el backend de `libnsfb` exige reescribir mucho, se pasa a
hacer crecer el visor propio de la fase 1.
