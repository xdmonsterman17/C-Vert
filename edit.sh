#!/usr/bin/env sh
# Quick launch: opens the C-Bricks editor (the `cbricks` command) for the C-Vert project.
#   ./edit.sh            open the editor (re-exports it first if C-Bricks changed)
#   ./edit.sh --run      no editor: headless build, then run the program on the real screen (also --build)
# Other arguments go to `cbricks` (--rebuild, --no-build).
# `cbricks DIR` opens that project (C-Bricks update 2026-11-12, see CBRICKS-UPDATE-NOTE.md).
cd "$(dirname "$0")" || exit 1
export PATH="$HOME/.local/bin:$PATH"
case "$1" in
    --run|--build) exec cbricks run . -- --obj assets/house.obj ;;  # builds first, then runs on the real screen
esac
command -v cbricks >/dev/null 2>&1 || { echo "edit.sh: 'cbricks' not found in PATH" >&2; exit 1; }
exec cbricks "$@" .
