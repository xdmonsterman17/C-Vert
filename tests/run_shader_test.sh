#!/usr/bin/env bash
# The Shader workspace (F4 / --shader): a surface shader, a post shader and a broken one on a virtual display (screenshots
# checked), the node canvas reading the graph, the node table matching C-Bricks', hot reload (the file rewritten while it runs,
# 40 times: more than the runtime's 32 shader slots). Reports go to tests/reports/shader_*. Needs a built program (run_app_test.sh builds it).
#   tests/run_shader_test.sh
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CBRICKS_DIR="${CBRICKS_DIR:-$HOME/Documents/Projects/Godot/C-Bricks}"
OFF="$CBRICKS_DIR/tests/offscreen.sh"
BIN=.build/native/meshforge
R=tests/reports; cd "$HERE" || exit 1; mkdir -p $R
# the program writes itself into the app registry: a throwaway one, never the real config
rm -rf "$PWD/$R/shader_config"; export CBRICKS_CONFIG="$PWD/$R/shader_config"
fail=0
bad() { echo "FAIL: $1"; fail=$((fail + 1)); }
[ -x $BIN ] || { echo "no program: run tools/build.sh first"; exit 1; }

shot() {   # name file expect-text
    "$OFF" $BIN --cd "$PWD" --no-maximize --shader "$2" --quit-after 3 --shot "$R/shader_$1.png" > "$R/shader_$1.log" 2>&1
    grep -q "$3" "$R/shader_$1.log" || { bad "$1: the log should contain '$3' (see $R/shader_$1.log)"; return; }
    [ -f "$R/shader_$1.png" ] || { bad "$1: no screenshot"; return; }
    out=$(python3 tests/png_stats.py "$R/shader_$1.png")
    colours=$(echo "$out" | grep -o 'colours [0-9]*' | grep -o '[0-9]*')
    [ "${colours:-0}" -ge 4 ] && echo "ok   $1 ($out)" || bad "$1: the picture looks blank ($out)"
}
shot surface tests/data/checker.cbshader "surface shader loaded (1), nodes 5 wires 4"
shot post tests/data/lift.glsl "post shader loaded (1), nodes 0 wires 0"
shot broken tests/data/broken.glsl "did not compile"
cmp -s $R/shader_surface.png $R/shader_broken.png && bad "the surface shader and the broken one drew the same picture"

if [ ! -f "$CBRICKS_DIR/scripts/shader_nodes.json" ]; then echo "skip shader_nodes.json sync (no C-Bricks checkout at $CBRICKS_DIR)"
elif cmp -s assets/shader_nodes.json "$CBRICKS_DIR/scripts/shader_nodes.json"; then echo "ok   shader_nodes.json matches C-Bricks'"
else bad "assets/shader_nodes.json differs from C-Bricks' scripts/shader_nodes.json: copy it over"; fi

reg=$(cat "$CBRICKS_CONFIG/apps/c-vert" 2>/dev/null)
[ -n "$reg" ] && [ -x "$reg" ] && [ "$(basename "$reg")" = "meshforge" ] && echo "ok   registered ($reg)" || bad "the program did not register itself in $CBRICKS_CONFIG/apps/c-vert"

LIVE=$R/shader_live.cbshader; cp tests/data/checker.cbshader $LIVE
"$OFF" bash -c "
    $BIN --cd $PWD --no-maximize --shader $LIVE --quit-after 22 > $R/shader_hot.log 2>&1 &
    P=\$!
    for i in \$(seq 1 40); do sleep 0.3; python3 -c \"f='$LIVE'; s=open(f).read(); open(f,'w').write(s+' ')\"; done
    wait \$P"
n=$(grep -o 'shader loaded ([0-9]*)' $R/shader_hot.log | tail -1 | grep -o '[0-9]*')
[ "${n:-0}" -ge 36 ] && echo "ok   hot reload ($n loads)" || bad "hot reload: only ${n:-0} loads (see $R/shader_hot.log)"

[ $fail -eq 0 ] && echo "Shader workspace tests passed." || echo "$fail failure(s)."
exit $fail
