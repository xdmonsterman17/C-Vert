#!/usr/bin/env sh
# Builds and runs the mesh core's tests with the strictest flags C-Bricks itself uses
# (C99, -Wall -Wextra -pedantic, warnings are failures), plus ASan/UBSan when the compiler has them.
#   tests/run_core_tests.sh
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/meshforge-core-tests"
mkdir -p "$OUT"
SRC="core/mf_mesh.c core/mf_ops.c core/mf_geom.c core/mf_edit.c core/mf_trs.c core/mf_prim.c core/mf_mod.c core/mf_tools.c core/mf_bevel.c core/mf_obj.c tests/test_mesh.c"
FLAGS="-std=c99 -O1 -g -Wall -Wextra -pedantic -Werror -Icore"

echo "== strict build =="
cc $FLAGS $SRC -o "$OUT/test_mesh" -lm
(cd "$OUT" && ./test_mesh)

# Sanitizers: the probe must RUN, not just link (on this Pi's kernel AddressSanitizer links but
# dies at start-up with "kSpaceBeg" in its allocator; UBSan alone works).
SAN=""
for want in "address,undefined" "undefined"; do
    if echo 'int main(void){return 0;}' | cc -fsanitize=$want -x c - -o "$OUT/probe" 2>/dev/null && "$OUT/probe" >/dev/null 2>&1; then
        SAN="$want"; break
    fi
done
if [ -n "$SAN" ]; then
    echo "== sanitizers ($SAN) =="
    cc $FLAGS -fsanitize=$SAN -fno-sanitize-recover=undefined $SRC -o "$OUT/test_mesh_san" -lm
    (cd "$OUT" && ./test_mesh_san)
else
    echo "(no working sanitizer here: skipped)"
fi
