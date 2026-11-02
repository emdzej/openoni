#!/usr/bin/env bash
# Applies tools/ghidra/names.tsv to the Ghidra project `oni` (program oni.exe) and regenerates the
# grep dumps in re/ (git-ignored): all.c (decompiled, named), funcs.txt, meta.tsv (sizes, calls, strings).
#   tools/ghidra/apply-names.sh [names.tsv]
# First run on a fresh project (oni.exe = a copy of game/Oni.exe): import with Ghidra's analyzeHeadless,
# not ghidra-cli's import (see AGENTS.md). FillGaps.java creates the functions the auto-analysis misses
# (vtable methods, callbacks); it is idempotent.
set -euo pipefail
cd "$(dirname "$0")/../.."
NAMES=$(cd "$(dirname "${1:-tools/ghidra/names.tsv}")" && pwd)/$(basename "${1:-tools/ghidra/names.tsv}")
P=(--project oni --program oni.exe)
ghidra script run tools/ghidra/FillGaps.java "${P[@]}"
ghidra script run tools/ghidra/ApplyNames.java "${P[@]}" -- "$NAMES"
mkdir -p re
ghidra script run tools/ghidra/DumpAllNamed.java "${P[@]}" -- "$PWD/re/all.c"
ghidra script run tools/ghidra/DumpMeta.java "${P[@]}" -- "$PWD/re/meta.tsv"
grep -o '^// ==== [^ ]* @ [0-9a-f]*' re/all.c | awk '{print $5, $3}' | sort > re/funcs.txt
echo "re/all.c: $(wc -l < re/all.c) lines, $(wc -l < re/funcs.txt) functions"
