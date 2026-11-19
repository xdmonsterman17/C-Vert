#!/usr/bin/env sh
# Builds the C-Bricks project headless (no editor window): graph -> C -> native program.
#   tools/build.sh            normal build            -> .build/native/meshforge
#   tools/build.sh strict     -std=c99 -Wall -Wextra -pedantic (the flags C-Bricks' own tests use)
# Needs the C-Bricks checkout (its Godot classes do the work): CBRICKS_DIR overrides the path.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CBRICKS_DIR="${CBRICKS_DIR:-$HOME/Documents/Projects/Godot/C-Bricks}"
export PATH="$HOME/.local/bin:$PATH"
export LP_NUM_THREADS=2
exec nice -n 10 godot --headless --path "$CBRICKS_DIR" -s "$HERE/tools/build.gd" -- "$HERE" "$@"
