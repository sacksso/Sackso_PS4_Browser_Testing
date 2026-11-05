# Qué probar en la PS4 (fase 0) y qué enviar

1. Instala el PKG `IV0000-PSBR00001_00-PSBROWSER0000000.pkg` y ábrelo.
2. Comprueba en pantalla, para cada URL (cualquier botón = siguiente):
   - `http://example.com/` → debería dar `HTTP 200`.
   - `https://example.com/` → HTTPS.
   - `https://info.cern.ch/`, la página de Wikipedia de PS4 (grande, se trunca a 512 KB) y `https://www.wikipedia.org/` (redirecciones).
3. Prueba el desplazamiento (cruceta/stick).
4. Sube `/data/psbrowser/log.txt` (y `last.html` si hace falta). Cosas que me interesan:
   - Líneas `[net] load ... = 0x...` (carga de módulos) y `[net] listo`.
   - Líneas `[tls] verifyErr=0x...` por cada HTTPS (0 = certificado validado por la consola).
   - Cualquier `[http] ... fallo 0x...` con el paso que falló.
   - Salida del mando (`boton N`).
5. Si no hay red: comprueba que la consola tiene internet desde el menú del sistema y que no hay otro homebrew con red abierto.

## Si algo falla

| Síntoma | Probable causa | Siguiente paso |
|--------|----------------|----------------|
| `net_init` falla | Módulos NET/HTTP/SSL no cargan o pools | Mirar los códigos `[net]` del log |
| `createConnection` / `sendRequest` con error | DNS, conectividad o TLS | Mirar `[tls]` y el código 0x8043xxxx |
| HTTPS falla pero HTTP va | Certificados | Probar con el modo permisivo (por defecto) y revisar `verifyErr` |
| Pantalla negra | Render/SDL | Mirar `SDL_Init...` y `sin renderer` en el log |

## Prueba 2 (v0.1.1): diagnosticar `0x80431073`

En la prueba 1 solo falló `https://en.wikipedia.org/wiki/PlayStation_4` (en `sendRequest`, tras aceptar el TLS); `https://www.wikipedia.org/`
funcionó (122 KB). Esta versión:
- reintenta una vez en modo compat (sin keep-alive y con `Accept` / `Accept-Encoding: identity`) y lo anota en el log;
- añade tres URLs: `en.wikipedia.org/wiki/Main_Page`, la versión móvil `en.m.wikipedia.org` y la misma página en texto plano (`action=raw`).

Qué miro en el log: si el reintento (`[http] reintento: OK`) arregla la página, si falla solo la grande o todas las de `en.wikipedia.org`,
y el `verifyErr` en modo estricto (debería seguir en 0).
