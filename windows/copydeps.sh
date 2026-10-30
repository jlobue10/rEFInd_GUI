#!/bin/bash
# Copies the MSYS2 DLL dependency closure of the exe (and its Qt plugins) into
# the target directory, so the app runs on a machine without MSYS2 installed.
# windeployqt handles the Qt DLLs/plugins; this covers the MinGW/clang runtime,
# ICU, glib and friends it leaves behind. Usage: copydeps.sh <dir>
#
# The import tables are read with objdump (binutils in UCRT64, llvm-objdump in
# CLANGARM64) and resolved against $MSYSTEM_PREFIX/bin, then the copied DLLs
# are scanned in turn until the closure is complete. (The previous ldd-based
# version could not load ARM64 images from MSYS2's emulated x64 shell.)
# System DLLs (kernel32, ...) are not under the prefix and are skipped.
# Best-effort: no set -e; a DLL objdump cannot read is just skipped.
cd "$1" || exit 1
PREFIX="${MSYSTEM_PREFIX:-/ucrt64}"
if command -v objdump >/dev/null 2>&1; then
    OBJDUMP=objdump
elif command -v llvm-objdump >/dev/null 2>&1; then
    OBJDUMP=llvm-objdump
else
    echo "copydeps: neither objdump nor llvm-objdump is available" >&2
    exit 1
fi

declare -A seen
queue=()
while IFS= read -r -d '' f; do
    queue+=("$f")
done < <(find . \( -name '*.exe' -o -name '*.dll' \) -print0)

while [ ${#queue[@]} -gt 0 ]; do
    f="${queue[0]}"
    queue=("${queue[@]:1}")
    while read -r dep; do
        [ -n "$dep" ] || continue
        key="${dep,,}"
        [ -n "${seen[$key]:-}" ] && continue
        seen[$key]=1
        if [ ! -f "$dep" ] && [ -f "$PREFIX/bin/$dep" ]; then
            cp "$PREFIX/bin/$dep" .
            echo "copied $dep"
            queue+=("./$dep")
        fi
    done < <("$OBJDUMP" -p "$f" 2>/dev/null | grep -i 'DLL Name:' | awk '{print $3}')
done
