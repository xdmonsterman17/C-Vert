# Debugging and testing

Bugs live in `BUGS.md`; this is the process and the tools.

## The short version
1. `tests/run_all.sh` (about 30 seconds).
2. Read what failed: each check prints a one-line `FAIL:` and the log it checked (`tests/reports/*.log`).
3. Look at pictures: `tests/reports/surface.png`, `post.png`, `broken.png`, `hot.png` (Read them).
4. Found a bug? Add a check that shows it, put it in `BUGS.md` Open, fix, move it to Fixed with the guard.

## The layers
- **Strict build:** `cbricks build . strict` (`-std=c99 -Wall -Wextra -pedantic`): warnings fail the run.
- **Screenshots:** the program with `--shot FILE --frames N` on a private virtual display (C-Bricks' `tests/offscreen.sh`); `tests/png_stats.py` counts the colours so a blank picture fails; the surface and broken pictures must differ.
- **Hot reload:** the shader file is rewritten 40 times while the program runs; the log must count at least 36 loads (more than the 32 shader slots, so freeing works).
- **Not covered:** the mouse / keys (drag, wheel, SPACE, R) and a real display.
