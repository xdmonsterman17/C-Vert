#!/usr/bin/env bash
# C-Shader's tests: strict build, then the program on a surface shader, a post shader and a broken one (virtual display,
# screenshots checked), then hot reload (the file rewritten while it runs, 40 times: more than the runtime's 32 slots).
# Reports (screenshots, logs) go to tests/reports/. Exit code = failures.
cd "$(dirname "$0")/.." || exit 1
export PATH="$HOME/.local/bin:$PATH"
CBRICKS_DIR="${CBRICKS_DIR:-$HOME/Documents/Projects/Godot/C-Bricks}"
R=tests/reports; mkdir -p $R
# The program writes itself into the app registry: a throwaway one here, never the real config.
rm -rf "$PWD/$R/config"; export CBRICKS_CONFIG="$PWD/$R/config"
fail=0
bad() { echo "FAIL: $1"; fail=$((fail + 1)); }

cbricks build . strict > $R/build.log 2>&1 || { tail -5 $R/build.log; bad "build (strict flags)"; exit 1; }
grep -qi "warning" $R/build.log && bad "warnings in the build (see $R/build.log)"
BIN=.build/native/c_shader
OFF="$CBRICKS_DIR/tests/offscreen.sh"

shot() {   # name file expect-status-text
	"$OFF" "$BIN" --shader "$2" --shot "$R/$1.png" --frames 30 > "$R/$1.log" 2>&1
	grep -q "$3" "$R/$1.log" || { bad "$1: status should contain '$3' (see $R/$1.log)"; return; }
	[ -f "$R/$1.png" ] || { bad "$1: no screenshot"; return; }
	out=$(python3 tests/png_stats.py "$R/$1.png")
	colours=$(echo "$out" | grep -o 'colours [0-9]*' | grep -o '[0-9]*')
	[ "${colours:-0}" -ge 4 ] && echo "ok   $1 ($out)" || bad "$1: the picture looks blank ($out)"
}
shot surface tests/data/checker.cbshader "surface shader loaded"
shot post tests/data/lift.glsl "post shader loaded"
shot broken tests/data/broken.glsl "did not compile"
# the node canvas read the graph: 5 nodes, 4 wires (checker.cbshader); a GLSL file has no graph
grep -q "nodes 5 wires 4" $R/surface.log && echo "ok   canvas (5 nodes, 4 wires)" || bad "the canvas did not read checker.cbshader (see $R/surface.log)"
grep -q "nodes 0 wires 0" $R/post.log && echo "ok   canvas empty for a GLSL file" || bad "a GLSL file should leave the canvas empty (see $R/post.log)"
# the shipped node table is the same file C-Bricks compiles with
cmp -s shader_nodes.json "$CBRICKS_DIR/scripts/shader_nodes.json" && echo "ok   shader_nodes.json matches C-Bricks'" || bad "shader_nodes.json differs from C-Bricks' copy: copy it over"
cmp -s $R/surface.png $R/broken.png && bad "the surface shader and the broken one drew the same picture"

# app registry: running the program leaves its path where the editor's Preview button finds it
reg=$(cat "$CBRICKS_CONFIG/apps/c-shader" 2>/dev/null)
[ -n "$reg" ] && [ -x "$reg" ] && [ "$(basename "$reg")" = "c_shader" ] && echo "ok   registered ($reg)" || bad "the program did not register itself in $CBRICKS_CONFIG/apps/c-shader"

# hot reload
LIVE=$R/live.cbshader; cp tests/data/checker.cbshader $LIVE
"$OFF" bash -c "
	$BIN --shader $LIVE --shot $R/hot.png --frames 700 > $R/hot.log 2>&1 &
	P=\$!
	for i in \$(seq 1 40); do sleep 0.3; python3 -c \"f='$LIVE'; s=open(f).read(); open(f,'w').write(s+' ')\"; done
	wait \$P"
n=$(grep -o 'loaded ([0-9]*)' $R/hot.log | grep -o '[0-9]*')
[ "${n:-0}" -ge 36 ] && echo "ok   hot reload ($n loads)" || bad "hot reload: only ${n:-0} loads (see $R/hot.log)"

[ $fail -eq 0 ] && echo "All C-Shader tests passed." || echo "$fail failure(s)."
exit $fail
