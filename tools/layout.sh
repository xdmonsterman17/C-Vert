#!/usr/bin/env sh
# Regenerate nothing: opens the project (the workspace root) in the real C-Bricks editor OFFSCREEN (private Xvfb display, lowest
# priority, nothing appears on your screen), applies the editor's Arrange to main.cbricks, saves it,
# and writes a screenshot to tests/reports/layout_main.png (Read the PNG to see the graph).
#   tools/make_main.py && tools/layout.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
CBRICKS_DIR="${CBRICKS_DIR:-$HOME/Documents/Projects/Godot/C-Bricks}"
export PATH="$HOME/.local/bin:$PATH"
cd "$CBRICKS_DIR"
exec tests/offscreen.sh godot --path "$CBRICKS_DIR" --resolution 1600x900 -s "$HERE/tools/layout.gd" -- "$HERE" "$HERE/tests/reports/layout_main.png"
