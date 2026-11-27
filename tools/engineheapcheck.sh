#!/bin/bash
# Checks that the RAM meter's heap start is the bundled engine's.
# usage: engineheapcheck.sh <psxsplash.elf> <editor core/budget.hh>
# The engine's is the __heap_start symbol of the ELF (nm from $NM, default
# mipsel-linux-gnu-nm); the editor's is kEngineHeapStart in budget.hh. The
# PS-X EXE does not record it, so this runs where the engine is built. Exit 0
# if they are the same, 1 if they differ, 2 if either cannot be read.
set -u
if [ $# -ne 2 ]; then echo "usage: $0 <psxsplash.elf> <budget.hh>" >&2; exit 2; fi
elf=$1; hh=$2
err() { echo "engineheapcheck: error: $*" >&2; exit 2; }
[ -f "$elf" ] || err "no $elf"
[ -f "$hh" ] || err "no $hh"
engine=$("${NM:-mipsel-linux-gnu-nm}" "$elf" | awk '$3 == "__heap_start" { print tolower($1) }')
[[ $engine =~ ^[0-9a-f]{8}$ ]] || err "no __heap_start in $elf"
editor=$(sed -n 's/^constexpr uint32_t kEngineHeapStart = 0x\([0-9a-fA-F]\{8\}\);.*/\1/p' "$hh" | tr A-F a-f)
[[ $editor =~ ^[0-9a-f]{8}$ ]] || err "no kEngineHeapStart = 0x........ in $hh"
if [ "$engine" != "$editor" ]; then
    echo "engineheapcheck: the engine's heap starts at 0x$engine, budget.hh says 0x$editor: set kEngineHeapStart to 0x$engine"
    exit 1
fi
echo "engineheapcheck: heap start 0x$engine matches"
