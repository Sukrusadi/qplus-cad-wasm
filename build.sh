#!/usr/bin/env bash
# qpcad'i bağlar: out/qpcad.mjs + out/qpcad.wasm. Önce scripts/build-occt-wasm.sh (README.md).
# QP_WASM_NODE=1 → Node'da da çalışır (test/test-node.mjs; çıktı out-node/). QP_OCCT_ROOT → başka OCCT klasörü.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
if ! command -v emcmake >/dev/null && [ -f "$ROOT/emsdk/emsdk_env.sh" ]; then
  source "$ROOT/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
command -v emcmake >/dev/null || { echo "emcmake yok: emsdk kurun (README.md)" >&2; exit 1; }
NODE="${QP_WASM_NODE:-0}"
OUT="$ROOT/out"; [ "$NODE" = 1 ] && OUT="$ROOT/out-node"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN=Ninja
NODE_FLAG=OFF; [ "$NODE" = 1 ] && NODE_FLAG=ON
EXTRA=(); [ -n "${QP_OCCT_ROOT:-}" ] && EXTRA=(-DQP_OCCT_ROOT="$QP_OCCT_ROOT")
emcmake cmake -S "$ROOT" -B "$OUT/obj" -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DQP_WASM_NODE="$NODE_FLAG" "${EXTRA[@]}"
cmake --build "$OUT/obj" --parallel
cp "$OUT/obj/qpcad.mjs" "$OUT/obj/qpcad.wasm" "$OUT/"
ls -l "$OUT/qpcad.mjs" "$OUT/qpcad.wasm"
