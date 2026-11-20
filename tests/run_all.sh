#!/usr/bin/env sh
# Everything: core unit tests, OBJ sample checks, the app end-to-end test (scripted demo), then the real-input test.
#   tests/run_all.sh          (add  --core  to stop after the fast part)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$HERE/tests/reports"
"$HERE/tests/run_core_tests.sh"
echo "== sample meshes =="
for f in "$HERE"/assets/*.obj; do "$HERE/tools/objinfo.sh" "$f"; done
[ "$1" = "--core" ] && exit 0
"$HERE/tests/run_app_test.sh"
"$HERE/tests/run_input_test.sh"
"$HERE/tests/run_ops_test.sh"
"$HERE/tests/run_ui_test.sh"
"$HERE/tests/run_polish_test.sh"
"$HERE/tests/run_stress_test.sh"
"$HERE/tests/run_shader_test.sh"
"$HERE/tests/run_shader_edit_test.sh"
"$HERE/tests/run_shader_graph_test.sh"
"$HERE/tests/run_shader_history_test.sh"
"$HERE/tests/run_shader_palette_test.sh"
echo "ALL OK"
