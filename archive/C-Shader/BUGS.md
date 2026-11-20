# Bugs

Found, fixed and suspected bugs in C-Shader. Same format as C-Bricks' BUGS.md.
- **Open:** seen happening, not fixed yet.
- **Fixed:** what went wrong, why, the fix, and what guards against it coming back.
- **Potential:** risks we know about but haven't seen go wrong. Check these when touching the area.

When a bug is found: add it to Open (what you did, what happened, what you expected). When it's fixed: move it to Fixed with cause, fix and guard. Prefer a test that reproduces it *before* fixing.

---

## Open
(none)

## Fixed
- **2026-11-19, hot reload stopped after 31 changes:** the runtime had 32 shader slots and no way to free one. Fix: `cb_unload_shader` in C-Bricks (slot reuse); guard: `tests/run_all.sh` hot reload (40 rewrites) and C-Bricks' `shader_api` case.

## Potential
- A file written in two steps (truncate, then write) can be read half-written: an empty file is skipped, a partial one fails to compile and the next poll fixes it (a "did not compile" flash).
- Shaders that read `uniform`s the preview never sets draw with zeros.
