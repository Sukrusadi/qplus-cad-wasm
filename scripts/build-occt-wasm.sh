#!/usr/bin/env bash
# OCCT'yi tarayıcı için (Emscripten, wasm32) statik derler. Android/iOS ile aynı modül alt kümesi.
# Thread yok; istisnalar açık (-fexceptions), sınır denetimleri açık (bozuk dosya istisnaya düşer).
# emcc PATH'te olmalı: build/emsdk/emsdk_env.sh (README.md).
#
# Kullanım: scripts/build-occt-wasm.sh [kurulum-klasörü]   (varsayılan third_party/occt/wasm)
# Ortam: OCCT_TAG (V7_8_1), OCCT_SRC, OCCT_WORK (build/occt), JOBS, OCCT_WASM_OPT (-O2)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OCCT_TAG="${OCCT_TAG:-V7_8_1}"
PREFIX="${1:-$ROOT/third_party/occt/wasm}"
WORK="${OCCT_WORK:-$ROOT/build/occt}"
SRC="${OCCT_SRC:-$WORK/src-$OCCT_TAG}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"
OPT="${OCCT_WASM_OPT:--O2}"

command -v emcmake >/dev/null || { echo "emcmake yok: önce emsdk_env.sh" >&2; exit 1; }
if [ ! -f "$SRC/CMakeLists.txt" ]; then
  mkdir -p "$(dirname "$SRC")"
  git clone --depth 1 --branch "$OCCT_TAG" https://github.com/Open-Cascade-SAS/OCCT.git "$SRC"
fi
BUILD="$WORK/build-$OCCT_TAG-wasm"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
FLAGS="$OPT -g0 -DNDEBUG -fexceptions -ffunction-sections -fdata-sections"

emcmake cmake -S "$SRC" -B "$BUILD" -G "$GEN" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_C_FLAGS_RELEASE="$FLAGS" -DCMAKE_CXX_FLAGS_RELEASE="$FLAGS" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DINSTALL_DIR="$PREFIX" -DINSTALL_DIR_LAYOUT=Unix \
  -DBUILD_LIBRARY_TYPE=Static \
  -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF \
  -DBUILD_MODULE_ModelingAlgorithms=OFF -DBUILD_MODULE_Visualization=OFF \
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF \
  -DBUILD_MODULE_DETools=OFF -DBUILD_MODULE_Draw=OFF \
  -DBUILD_ADDITIONAL_TOOLKITS="TKDESTEP TKDEIGES TKXCAF TKBinXCAF TKMesh TKBRep" \
  -DBUILD_DOC_Overview=OFF -DBUILD_Inspector=OFF -DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF \
  -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_TK=OFF -DUSE_TCL=OFF -DUSE_OPENGL=OFF \
  -DUSE_GLES2=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF \
  -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF
cmake --build "$BUILD" --parallel "$JOBS"
cmake --install "$BUILD"
echo "$OCCT_TAG" > "$PREFIX/OCCT_VERSION"
echo "OCCT $OCCT_TAG (wasm, statik) -> $PREFIX"
du -sh "$PREFIX/lib" 2>/dev/null || true
