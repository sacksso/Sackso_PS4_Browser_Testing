# Arnés de prueba en PC (sin PS4)
Renderiza un HTML a PNG con exactamente el mismo código que usará la consola (htmldoc + textengine + pagerender), para revisar la maquetación sin compilar el PKG.

```
mkdir -p /tmp/stbinc && ln -sfn <TOOLCHAIN>/include/stb /tmp/stbinc/stb      # cabeceras stb del toolchain OpenOrbis
g++ -std=c++17 -O1 -I/tmp/stbinc -Iapp -o render tools/render_host.cpp app/htmldoc.cpp app/textengine.cpp app/pagerender.cpp app/fonts_data.cpp
./render pagina.html salida.png X <oscuro 0|1> <scroll px> <zoom %>      # X = argumento sin uso
```
