#!/usr/bin/env bash
# Copy the browser runtime of the play page from the pinned npm packages
# (@emdzej/gasm-host, @emdzej/csfs-*) into docs/public/play/vendor/.
# Run after `pnpm install` in docs/; the output is git-ignored. Bump the pins in docs/package.json,
# never edit the copies.
set -euo pipefail
cd "$(dirname "$0")/.."
nm=node_modules/@emdzej
out=public/play/vendor
[ -f "$nm/gasm-host/gasm-host.js" ] || { echo "no $nm/gasm-host: run pnpm install in docs/ first" >&2; exit 1; }
rm -rf "$out"
mkdir -p "$out/gasm/lib" "$out/csfs/core" "$out/csfs/fsa" "$out/csfs/opfs"
# gasm-host.js re-exports ./lib/*.js (0.16.1; lib/threads.js is imported by gasm-worker.js); gasm-worker.js imports ./gasm-host.js and ./webgpu-gfx.js;
# gasm-present.js is the WebGL 2 upscaler, input-script.mjs the --input parser (the page's hash runs).
cp "$nm/gasm-host/gasm-host.js" "$nm/gasm-host/gasm-worker.js" "$nm/gasm-host/webgpu-gfx.js" \
   "$nm/gasm-host/gasm-present.js" "$out/gasm/"
cp "$nm/gasm-host/input-script.mjs" "$out/gasm/input-script.js"   # .js: served as JavaScript everywhere
cp "$nm/gasm-host/lib/"*.js "$out/gasm/lib/"
cp "$nm/gasm-host/LICENSE" "$out/gasm/LICENSE"
cp "$nm/csfs-core/dist/"*.js "$out/csfs/core/"
cp "$nm/csfs-fsa/dist/index.js" "$out/csfs/fsa/"
cp "$nm/csfs-opfs/dist/index.js" "$out/csfs/opfs/"
cp "$nm/csfs-core/LICENSE" "$out/csfs/LICENSE"
node -e "const p=require('./$nm/gasm-host/package.json');console.log(p.name+'@'+p.version+' ('+p.license+')')" > "$out/gasm/VERSION"
echo "vendored $(cat "$out/gasm/VERSION") and csfs $(node -p "require('./$nm/csfs-core/package.json').version")"
