#!/bin/sh
# compat_lint.sh
# Scans for C idioms that compile in C but fail (or silently misbehave)
# when the same source is pulled into a C++ translation unit via extern "C".
# See CONVENTIONS.md for why each of these matters.
set -e

FAILED=0

echo "=== Checking for compound-literal address-of (&(Type){...}) ==="
if grep -rn '&([A-Za-z_][A-Za-z0-9_]*)\s*{' src/core src/render 2>/dev/null; then
    echo "!!! Found compound-literal address-of above. C++ treats these as"
    echo "!!! rvalues and rejects taking their address. Use a named local."
    FAILED=1
fi

echo
echo "=== Checking for uncast malloc/calloc/realloc ==="
if grep -rnE '=\s*(malloc|calloc|realloc)\(' src/core src/render 2>/dev/null \
    | grep -vE '=\s*\([A-Za-z_][A-Za-z0-9_ ]*\*\s*\)\s*(malloc|calloc|realloc)\('; then
    echo "!!! Found uncast allocator call(s) above. C allows the implicit"
    echo "!!! void* -> T* conversion; C++ requires an explicit cast."
    FAILED=1
fi

echo
if [ "$FAILED" -eq 0 ]; then
    echo "compat_lint: clean."
else
    echo "compat_lint: FAILED -- fix the issues above before committing."
    exit 1
fi
