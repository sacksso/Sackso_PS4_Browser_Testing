# Arnés de prueba en PC (sin PS4)
Renderiza un HTML a PNG con exactamente el mismo código que usará la consola (htmldoc + textengine + pagerender), para revisar la maquetación sin compilar el PKG.

```
mkdir -p /tmp/stbinc && ln -sfn <TOOLCHAIN>/include/stb /tmp/stbinc/stb      # cabeceras stb del toolchain OpenOrbis
g++ -std=c++17 -O1 -I/tmp/stbinc -Iapp -o render tools/render_host.cpp app/htmldoc.cpp app/textengine.cpp app/pagerender.cpp app/fonts_data.cpp
./render pagina.html salida.png X <oscuro 0|1> <scroll px> <zoom %>      # X = argumento sin uso
```

# Pruebas en PC de la logica de descargas (v0.6.0)
`tools/stubs/` trae stubs minimos de SDL2, `orbis/*.h`, `keyboard.h` y `stb_image.h` (solo declaraciones) para comprobar sintaxis y ejecutar logica sin el toolchain.
```
# sintaxis (main.cpp, netclient.cpp...) con stubs; no enlaza
g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Itools/stubs -Iapp app/main.cpp
# nombres de archivo, tipos, tamanos (download.cpp)
g++ -std=c++17 -fsanitize=address,undefined -Iapp tools/tests/test_download.cpp app/download.cpp -o /tmp/test_dl && /tmp/test_dl
# netclient.cpp (sink + puerta de cabeceras) contra un sceHttp simulado
g++ -std=c++17 -fsanitize=address,undefined -Itools/stubs -Iapp tools/tests/test_netclient_mock.cpp app/netclient.cpp app/inflate.cpp app/charset.cpp -o /tmp/test_nc && /tmp/test_nc
```
Esto NO sustituye al CI ni a la consola: `mouse.cpp` y `textengine.cpp` necesitan mas stubs y no se comprueban asi.

# Pruebas en PC añadidas en v0.7.0
```
# escritura a disco en hilo aparte (integridad, contrapresion, abort, /dev/full)
g++ -std=c++17 -pthread -fsanitize=address,undefined -Itools/stubs -Iapp tools/tests/test_asyncwriter.cpp app/asyncwriter.cpp -o /tmp/test_aw && /tmp/test_aw
# iconos .ico (BMP 1/4/8/24/32 bits, PNG interno, fuzz): necesita Pillow para generar los casos
g++ -std=c++17 -fsanitize=address,undefined -Iapp tools/tests/test_ico.cpp app/icodec.cpp -o /tmp/test_ico   # lo invoca tools/tests/ico_test.py
python3 -I tools/tests/ico_test.py
```
