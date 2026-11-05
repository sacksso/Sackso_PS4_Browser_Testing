# psbrowser

Navegador homebrew para PS4 (OpenOrbis, GoldHEN). Objetivo a largo plazo: portar **NetSurf** (motor ligero, sin
JavaScript moderno). Mientras tanto, el repo avanza por fases pequeñas que se pueden probar en la consola.

> Uso personal / aprendizaje. Reutiliza el toolchain y el CI que ya funcionaron en el proyecto de Ren'Py para PS4.

## Estado: fase 0 (hello de red + visor de texto)

Qué hace el PKG actual:

- Inicializa la red del sistema (`libSceNet`, `libSceSsl`, `libSceHttp`) como en el sample `net_http` de OpenOrbis.
- Descarga una lista de URLs de prueba (http y https, con redirecciones) y las muestra en pantalla como texto plano
  (conversión HTML→texto muy básica, fuente 8x8 ASCII).
- Mando: cualquier botón = siguiente URL; cruceta arriba/abajo o stick izquierdo = desplazar.
- Log completo en `/data/psbrowser/log.txt`; última página descargada en `/data/psbrowser/last.html`.

Lo que se ha verificado **sin consola**: el código compila y enlaza con OpenOrbis v0.5.4 (`toolchain-llvm-18`) y
genera `eboot.bin`. Lo que **no** se ha podido verificar: que el PKG se genere (lo hace el CI) y nada de lo que ocurre en
la PS4 real. Ver `docs/PRUEBAS.md`.

## Cómo construir

1. Sube el repo a GitHub.
2. Actions → **Build PKG** → *Run workflow*.
3. Descarga el artefacto `psbrowser-pkg` (PKG + `.sha256`) y, si falla, el artefacto `logs`.

Título: `PS Browser` · Title ID: `PSBR00001` · Content ID: `IV0000-PSBR00001_00-PSBROWSER0000000`.

## Estructura

```
app/            código de la app (main, cliente HTTP, canvas/texto, HTML→texto)
third_party/    font8x8_basic.h (dominio público; tipo cambiado a unsigned char para C++)
docs/           PLAN.md (fases y decisiones), NETSURF.md (ruta a NetSurf), PRUEBAS.md (qué probar y qué logs enviar)
.github/workflows/build-pkg.yml
```

## Decisiones clave

- **TLS con el sistema, no con mbedTLS/OpenSSL:** OpenOrbis trae cabeceras y stubs de `libSceHttp`/`libSceSsl`, y el
  sample `net_http` hace HTTPS con ellas. Así se evita portar libcurl + una librería TLS para la primera versión.
- **DNS lo resuelve `sceHttp`:** no depende de `getaddrinfo` de la libc de OpenOrbis (en el port de CPython hubo que
  stubear `sceNetGetDnsInfo`, así que esa vía no está validada).
- **Certificados:** por defecto el hello acepta cualquier certificado y registra el `verifyErr` (igual que el sample).
  Con `strict_tls=true` rechaza los que la consola no valide. No uses el modo permisivo para datos sensibles.
