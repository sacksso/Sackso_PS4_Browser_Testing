#!/bin/sh
# Prueba en PC de scanVideos() (buscador de videos en el HTML crudo). Se ejecuta desde la raiz del repo:  sh tools/tests/run_scanvideos.sh
set -e
OUT=/tmp/psb_scanvideos; mkdir -p "$OUT"
python3 - "$OUT" <<'PY'
import sys
s = open('app/main.cpp', encoding='utf-8', errors='surrogateescape').read()
a = s.index('static std::string lowerStr('); b = s.index('static bool g_quitAsk')
c = s.index('// Clasifica una URL de video'); d = s.index('static void captureVideos(')
open(sys.argv[1] + '/scan_extract.inc', 'w', encoding='utf-8', errors='surrogateescape').write(s[a:b] + s[c:d])
PY
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wno-unused-function -Iapp -I"$OUT" -o "$OUT/test_scan" tools/tests/test_scanvideos.cpp app/url.cpp
"$OUT/test_scan"
