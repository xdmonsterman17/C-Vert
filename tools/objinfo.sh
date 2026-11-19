#!/usr/bin/env sh
# Builds tools/objinfo.c against the core and runs it, from wherever you are (paths are yours):
#   tools/objinfo.sh assets/house.obj
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/meshforge-objinfo"
C="$HERE/core"
cc -std=c99 -O1 -Wall -Wextra -pedantic -I"$C" "$HERE/tools/objinfo.c" "$C/mf_mesh.c" "$C/mf_obj.c" "$C/mf_geom.c" "$C/mf_ops.c" -o "$OUT" -lm
exec "$OUT" "$@"
