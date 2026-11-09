# psb_solver - captchas y verificaciones, resueltos por ti en el PC

La PS4 no puede dibujar un captcha ni ejecutar la comprobacion de Cloudflare. Este ayudante deja que **tu las resuelvas en el PC** (con un navegador real) y le pasa a la PS4 el resultado: cookies (p. ej. `cf_clearance`), el User-Agent con el que se resolvieron y las descargas que el PC intento iniciar. No resuelve nada solo.

## Preparar (una vez)
```
pip install playwright
python -m playwright install chromium        # o usa tu Chrome: --channel chrome
python tools/solver/psb_solver.py --allow <IP de la PS4>
```
Te muestra algo como `!solver 192.168.1.20:8765`. En la PS4: Ctrl+L (o el boton de la barra) y escribe ese comando una vez.

## Usar
1. En la PS4, en la pagina que pide la verificacion (o que tiene captcha): **F7**.
2. **[ Abrir esta pagina en el PC ]** -> en el PC se abre la pagina. Resuelve el captcha / pulsa el boton de descarga como siempre.
3. **[ Traer cookies y descargas del PC ]** -> la PS4 recibe cookies + User-Agent (+ las descargas que el PC intento hacer, que se cancelan en el PC).
4. **[ Reintentar la pagina en la PS4 ]**, o **[ Descargar en la PS4 ]** en la lista de descargas capturadas.

## Notas
* PC y PS4 en la misma red (la cookie de Cloudflare esta atada a tu IP publica y al User-Agent; por eso se manda tambien el UA).
* Algunos enlaces de descarga son de un solo uso o caducan en minutos: si falla, repite el paso 2-3 y descarga enseguida.
* `--fake` arranca sin navegador para probar solo la conexion PS4 <-> PC.
* El perfil (`~/.psb_solver_profile`) conserva las verificaciones resueltas: la siguiente vez muchas veces no hace falta repetirlas.
* Sin el ayudante: copia un `cookies.txt` (formato Netscape, el de las extensiones de navegador) a `/data/psbrowser/cookies_import.txt` por FTP y usa `about:cookies` -> Importar.
