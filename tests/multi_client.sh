#!/usr/bin/env sh
# How many C-Vert windows can this machine run at once? Starts N copies of the built program side by side,
# lets each run for SECONDS, then prints every copy's average frame rate (the program logs it on exit).
#   tests/multi_client.sh [N=2] [SECONDS=20] [--virtual] [-- extra program args, e.g. --obj assets/house.obj]
# By default the windows open on your REAL screen (that is the point: real GPU numbers). They will steal focus and
# overlap: leave the mouse alone while it runs. --virtual runs on a private X display instead (llvmpipe: CPU
# rendering, says nothing about the V3D GPU). Close the editor first for a fair number, then repeat with it open.
# Needs a built program: tools/build.sh. Results also go to tests/reports/multi_client.txt.
HERE="$(cd "$(dirname "$0")/.." && pwd)"
N=${1:-2}; SECS=${2:-20}; VIRT=0
shift 2>/dev/null; shift 2>/dev/null
[ "$1" = "--virtual" ] && { VIRT=1; shift; }
[ "$1" = "--" ] && shift
BIN="$HERE/.build/native/meshforge"
[ -x "$BIN" ] || { echo "no built program: run tools/build.sh first"; exit 1; }
mkdir -p "$HERE/tests/reports"; OUT="$HERE/tests/reports/multi_client.txt"; : > "$OUT"
run_all() {
  i=1
  while [ "$i" -le "$N" ]; do
    "$BIN" --cd "$HERE" --no-maximize --quit-after "$SECS" "$@" > "$HERE/.build/mc_$i.log" 2>&1 &
    i=$((i + 1))
  done
  wait
}
[ -n "$MC_INNER" ] || echo "starting $N client(s) for $SECS s..."
if [ "$VIRT" = 1 ] && [ -z "$MC_INNER" ]; then
  MC_INNER=1 xvfb-run -a -s "-screen 0 1920x1080x24 -nolisten tcp" "$0" "$N" "$SECS" -- "$@"
  exit $?
fi
run_all "$@"
total=0
for i in $(seq 1 "$N"); do
  f=$(grep -o 'avg fps [0-9.]*' "$HERE/.build/mc_$i.log" | awk '{print $3}')
  echo "client $i: ${f:-no result (see .build/mc_$i.log)} fps" | tee -a "$OUT"
  total=$(awk -v a="$total" -v b="${f:-0}" 'BEGIN{print a+b}')
done
echo "total $total fps across $N client(s), mean $(awk -v t="$total" -v n="$N" 'BEGIN{printf "%.1f", t/n}') each" | tee -a "$OUT"
