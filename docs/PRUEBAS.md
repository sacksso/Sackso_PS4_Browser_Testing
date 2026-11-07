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

## Prueba 3 (v0.1.2): hipótesis del tamaño de cabeceras

Resultado de la prueba 2: fallan **todas** las páginas `/wiki/...` de `en.wikipedia.org` y `en.m.wikipedia.org` (también con el reintento
compat); funcionan `www.wikipedia.org` y `en.wikipedia.org/w/index.php?...&action=raw` (173 KB). Con keep-alive, `Accept-Encoding` y TLS ya
descartados, la diferencia más probable son las **cabeceras de la respuesta** (las páginas de artículo traen muchas y grandes).
Es una hipótesis, no está confirmada. La v0.1.2:
- pide a `libSceHttp` un límite de cabeceras de respuesta de 128 KB con `sceHttpSetResponseHeaderMaxSize` y registra su código de retorno;
- registra las cabeceras de respuesta de cada página que funciona (líneas `[hdr]`), para comparar tamaños y contenido;
- añade `https://news.ycombinator.com/` como prueba extra.

Qué miro: la línea `sceHttpSetResponseHeaderMaxSize(...) = 0x...` (0 = aceptada), si `/wiki/PlayStation_4` pasa a dar 200 y, si no, el tamaño
de las cabeceras de las páginas que sí funcionan.

## Prueba 4 (v0.2.0, fase 1a): visor con enlaces y entrada

Esta versión **no se ha probado en la consola**. Cosas a comprobar y a reportar (el log `/data/psbrowser/log.txt` lo registra todo):

1. Arranca y muestra la página de inicio (`psbrowser - inicio`) con enlaces. ¿Se ve bien a 1080p?
2. **Teclado USB:** Tab / Shift+Tab seleccionan enlaces, Enter los abre, F6 abre la barra de URL, escribir y Enter navega, Esc cancela, Backspace vuelve atrás.
   Mira en el log las líneas `tecla: codigo=... char=...` (se guardan las 60 primeras).
3. **Ratón USB:** ¿aparece el cursor? ¿se mueve a velocidad razonable? ¿clic izquierdo abre el enlace bajo el cursor, clic derecho vuelve atrás?
   **Rueda:** la dirección es una suposición (valor positivo = subir). Si va al revés, dímelo (línea `rueda: N` en el log).
4. **Mando:** los índices de botón son una suposición del DS4 (0 abrir, 1 atrás, 2 recargar, 3 inicio, 4/5 página). El log anota `boton N` y `hat N`;
   dime qué botón físico produjo cada número si no coincide.
5. **Búsqueda:** F6, escribir `ps4 homebrew`, Enter (usa `html.duckduckgo.com/html/`, no probado).
6. Páginas largas: Wikipedia, Hacker News. ¿Es fluido el desplazamiento? (El dibujo es por software a 1080p; si va lento, dímelo.)

Limitaciones conocidas de la v0.2.0: sin imágenes, sin JavaScript, sin formularios, sin enlaces internos (`#ancla`), los caracteres no ASCII
salen como `?` salvo vocales acentuadas/ñ, que se convierten a su letra base.

## Prueba 5 (v0.3.0)
1. Abrir `C` (barra) o F10: `about:settings`. Subir la sensibilidad con `[ + ]`/`[ ++ ]` y comprobar que el cursor se mueve más. Cada cambio se guarda en `/data/psbrowser/config.txt`.
2. Probar la rueda; si va al revés, activar "Rueda invertida".
3. Marcar una página con `+` o Ctrl+D; verla en `B` (`about:bookmarks`).
4. Visitar una página con `<video>`/`<iframe>`: debe salir `[video]` o `[video incrustado]` con la URL en la barra de estado (aún no se reproduce).
5. Devolver `log.txt` (buscar líneas `rueda:` y `[refresh]`).

## Prueba v0.6.0: descargas (sin probar en PS4)
Instala el PKG y devuelve `/data/psbrowser/log.txt` (busca lineas `[dl]`, `[http] ... (solo cabeceras)`), el zip `logs` del CI y, si hay, el contenido de `/data/psbrowser/downloads.txt`.
1. **Archivo directo:** abre un enlace a un `.zip`/`.pdf`/`.pkg` pequeno (p. ej. algun release de GitHub). Debe salir "Descargar archivo" con nombre, tipo y tamano. X sobre **[ Descargar ]** (ya viene seleccionado) -> recuadro "Descargando" con barra, velocidad y tiempo restante -> `about:downloads`.
2. **Cancelar:** durante una descarga grande pulsa **O (boton 1)**: debe volver con "Descarga cancelada" y sin dejar archivo `.part`. (Si el servidor no envia datos durante unos segundos, la cancelacion tarda: no se atiende hasta el siguiente trozo.)
3. **Imagen:** abre la URL directa de una imagen PNG/JPEG (o `Imagenes de la ultima pagina` desde `about:downloads` tras visitar una pagina con imagenes): vista previa + Descargar. Con WebP/GIF/SVG no hay vista previa (aun no se decodifican) pero se descargan igual.
4. **Video directo:** un enlace `.mp4`/`.webm`. Descarga (no reproduce). `.m3u8`/`.mpd` muestra el aviso de streaming.
5. **Archivo grande (>100 MB):** comprueba que la barra avanza, que la velocidad es razonable y que la interfaz no se queda "colgada" (se redibuja cada ~300 ms).
6. **Errores:** enlace roto (404), descarga detras de Cloudflare/captcha (403/503 o una pagina HTML en vez del archivo): debe salir "Descarga fallida" con la causa y [ Reintentar ].
7. `about:downloads` (o Ctrl+J con teclado): `[ borrar ]` borra el archivo de verdad.
Cosas que no se pueden saber sin consola: velocidad real, disco lleno, si `rename` funciona en `/data`, si el servidor corta sin `Content-Length`.
