#!/usr/bin/env bash
# Download what the gasm build (openoni.wasm) needs into .deps/: wasi-sdk (clang + wasi-libc for wasm32),
# gasm's C SDK (gasm.h, gasm_loop.c, <GLES3/gl3.h> + gasm_gl.c, the CMake toolchain) and Binaryen's
# wasm-opt (gasm_add_game LOOP: the game keeps the original's own loops, see docs/re/architecture.md).
# Used by CI; locally a gasm checkout works as well.
#   tools/fetch-gasm-sdk.sh            -> .deps/wasi-sdk, .deps/gasm-c-sdk, .deps/binaryen
# Then:
#   cmake -S . -B build-gasm -DOPENONI_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk" \
#     -DGASM_WASM_OPT="$PWD/.deps/binaryen/bin/wasm-opt"
#   tools/fetch-gasm-sdk.sh --version  -> prints GASM_VERSION and exits (tools/fetch-gasm-runner.sh, release jobs)
set -euo pipefail
cd "$(dirname "$0")/.."
# gasm pins wasi-sdk 34 as well (gasm's scripts/fetch-wasi-sdk.sh).
WASI_SDK_VERSION=${WASI_SDK_VERSION:-34}
# The one place the gasm version is set (CI, release and Pages builds all call this script; the gasm
# bundles' runners too, via --version). Keep it in step with @emdzej/gasm-host in docs/package.json once
# the browser player exists.
GASM_VERSION=${GASM_VERSION:-0.16.0}
# Binaryen: the version and checksums gasm pins (gasm's scripts/fetch-binaryen.sh).
BINARYEN_VERSION=133
if [ "${1:-}" = --version ]; then echo "$GASM_VERSION"; exit 0; fi
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  PLAT=arm64-macos  BPLAT=arm64-macos   BSHA=ad66da82ac13f163e424b1643f16c6dfcccc98b5966296b43e52d3cab04f84a8 ;;
  Darwin-x86_64) PLAT=x86_64-macos BPLAT=x86_64-macos  BSHA=13a9b90be775c6389ce3d1f879cb8627bea56708ba8c122983941d53a8199b95 ;;
  Linux-x86_64)  PLAT=x86_64-linux BPLAT=x86_64-linux  BSHA=2dc9c7813f5375db93d96ead4b78222fcc3e2677bbb832297af4797782a37489 ;;
  Linux-aarch64) PLAT=arm64-linux  BPLAT=aarch64-linux BSHA=89c07ea56faf38d0fbecf36ca8ec0721756716185f265b568e133d427f299bf8 ;;
  *) echo "unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac
sha256_of() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
mkdir -p .deps
if [ ! -x .deps/wasi-sdk/bin/clang ]; then
  url=https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-$WASI_SDK_VERSION/wasi-sdk-$WASI_SDK_VERSION.0-$PLAT.tar.gz
  echo "fetching $url"
  curl -fsSL "$url" | tar xz -C .deps
  rm -rf .deps/wasi-sdk && mv ".deps/wasi-sdk-$WASI_SDK_VERSION.0-$PLAT" .deps/wasi-sdk
  if [ "$(uname -s)" = Darwin ]; then xattr -dr com.apple.quarantine .deps/wasi-sdk 2>/dev/null || true; fi
fi
if [ ! -f .deps/gasm-c-sdk/include/gasm.h ] || [ "$(cat .deps/gasm-c-sdk/VERSION 2>/dev/null)" != "$GASM_VERSION" ]; then
  url=https://github.com/emdzej/gasm/releases/download/$GASM_VERSION/gasm-c-sdk-$GASM_VERSION.zip
  echo "fetching $url"
  curl -fsSL -o .deps/gasm-c-sdk.zip "$url"
  rm -rf .deps/gasm-c-sdk ".deps/gasm-c-sdk-$GASM_VERSION"
  (cd .deps && unzip -q gasm-c-sdk.zip && mv "gasm-c-sdk-$GASM_VERSION" gasm-c-sdk && rm gasm-c-sdk.zip)
  echo "$GASM_VERSION" > .deps/gasm-c-sdk/VERSION    # a later pin bump fetches again
fi
if [ "$(cat .deps/binaryen/.version 2>/dev/null)" != "$BINARYEN_VERSION-$BPLAT" ]; then
  url=https://github.com/WebAssembly/binaryen/releases/download/version_$BINARYEN_VERSION/binaryen-version_$BINARYEN_VERSION-$BPLAT.tar.gz
  echo "fetching $url"
  curl -fsSL -o .deps/binaryen.tar.gz "$url"
  got=$(sha256_of .deps/binaryen.tar.gz)
  if [ "$got" != "$BSHA" ]; then echo "binaryen: checksum mismatch (got $got, want $BSHA)" >&2; rm -f .deps/binaryen.tar.gz; exit 1; fi
  rm -rf .deps/binaryen && tar xzf .deps/binaryen.tar.gz -C .deps && rm .deps/binaryen.tar.gz
  mv ".deps/binaryen-version_$BINARYEN_VERSION" .deps/binaryen
  if [ "$(uname -s)" = Darwin ]; then xattr -dr com.apple.quarantine .deps/binaryen 2>/dev/null || true; fi
  echo "$BINARYEN_VERSION-$BPLAT" > .deps/binaryen/.version
fi
.deps/wasi-sdk/bin/clang --version | head -1
.deps/binaryen/bin/wasm-opt --version
echo "gasm C SDK $GASM_VERSION: .deps/gasm-c-sdk"
