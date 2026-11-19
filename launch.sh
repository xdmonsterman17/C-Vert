#!/usr/bin/env sh
# Opens C-Vert straight away: runs the built program, no C-Bricks editor and no rebuild.
#   ./launch.sh [--obj FILE] [...]   extra arguments go to the program (default: assets/house.obj)
#   ./launch.sh --build ...          builds first (tools/build.sh, needs the C-Bricks checkout)
# Builds once if there is no program yet. Files are read relative to this folder.
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/.build/native/meshforge"
cd "$HERE" || exit 1
if [ "$1" = "--build" ]; then shift; "$HERE/tools/build.sh" || exit 1; fi
[ -x "$BIN" ] || "$HERE/tools/build.sh" || exit 1
case " $* " in *" --obj "*) exec "$BIN" --cd "$HERE" "$@" ;; *) exec "$BIN" --cd "$HERE" --obj assets/house.obj "$@" ;; esac
