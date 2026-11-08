#!/bin/bash
# Compila el eboot con el toolchain real de OpenOrbis replicando el CI (sin generar el PKG).
# Uso: OO_PS4_TOOLCHAIN=<ruta>/OpenOrbis/PS4Toolchain tools/build_local.sh   (desde la raiz del repo)
set -euo pipefail
: "${OO_PS4_TOOLCHAIN:?define OO_PS4_TOOLCHAIN}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
T="${TMPDIR:-/tmp}/psb_build"
rm -rf "$T"; mkdir -p "$T"; cp -r "$OO_PS4_TOOLCHAIN/samples" "$T/samples"
work="$T/samples/psbrowser"; proj=psbrowser
mkdir -p "$work/$proj"
cp "$T/samples/SDL2/Makefile" "$work/Makefile"
cp -r "$T/samples/SDL2/sce_sys" "$work/sce_sys"; cp -r "$T/samples/SDL2/sce_module" "$work/sce_module"
rm -f "$work/sce_sys/param.sfo"
cp "$REPO"/app/*.cpp "$REPO"/app/*.h "$work/$proj/"
cp "$T/samples/keyboard/keyboard/keyboard."* "$work/$proj/"
cp -r "$REPO/third_party" "$work/third_party"
FLAGS="-O2 -DPSB_STRICT_TLS"
if [ -n "${QJS_DIR:-}" ]; then   # QuickJS-ng (git clone --branch v0.18.0 https://github.com/quickjs-ng/quickjs): sin el, se compila con -DPSB_NO_JS
  for f in quickjs dtoa libregexp libunicode; do { echo '#define NDEBUG 1'; echo '#define __STDC_NO_ATOMICS__ 1'; cat "$QJS_DIR/$f.c"; } > "$work/$proj/qjs_$f.c"; done
  cp "$QJS_DIR"/*.h "$work/$proj/"; cp "$REPO/third_party/qjs_shim/malloc_np.h" "$REPO/third_party/qjs_shim/psb_qjs_shim.c" "$work/$proj/"
  FLAGS="$FLAGS -I$proj"
else
  FLAGS="$FLAGS -DPSB_NO_JS"
fi
sed -i -E \
  -e 's/^TITLE +:=.*/TITLE       := PS Browser/' \
  -e 's/^TITLE_ID +:=.*/TITLE_ID    := PSBR00001/' \
  -e 's/^CONTENT_ID +:=.*/CONTENT_ID  := IV0000-PSBR00001_00-PSBROWSER0000000/' \
  -e 's/^COMMONFILES +:=.*/COMMONFILES :=/' \
  -e 's/^(LIBS +:=.*)$/\1 -lSceNet -lSceSsl -lSceHttp -lSceKeyboard -lSceAvPlayer -lSceAudioOut -lSceUserService/' \
  "$work/Makefile"
cd "$work"
make eboot.bin EXTRAFLAGS="$FLAGS" 2>&1 | tee "$T/build.log" | tail -${TAIL:-40}
ls -la "$work"/eboot.bin 2>/dev/null || { echo "SIN eboot.bin"; exit 1; }
