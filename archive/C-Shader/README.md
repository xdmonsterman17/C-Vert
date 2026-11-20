> **ARCHIVED 2026-11-19: merged into C-Vert** (`../C-Vert`, Shader workspace: F4 or `--shader FILE`). Nothing here is developed any more; the code lives on in C-Vert's `app/mf_shader.c`, `app/mf_shed.c` and `core/mf_json.c`. Kept, not deleted.

# C-Shader

A small C-Bricks program that shows a `.cbshader` (or `.glsl`) file on a lit sphere and floor and reloads it whenever the
file's text changes (polled four times a second). The picture is the runtime's own C emitter and the real GPU, so it matches
what a program shows.

    cbricks build .
    .build/native/c_shader --shader path/to/file.cbshader        (surface or post: read from the file)
    .build/native/c_shader --shader f.cbshader --shot out.png --frames 30    (headless check)

Mouse drag turns the camera, wheel zooms, SPACE stops the spin, R resets. The top line says what happened (loaded, did not
compile: the errors are on the console). The C-Bricks editor's Shader screen will launch it (step 2, not built)
Sample shaders for trying it: `tests/data/`. Tests: `tests/run_all.sh`. Docs: CLAUDE.md, PROGRESS.md, DECISIONS.md, BUGS.md, DEBUG.md..

Archived copy inside C-Vert (2026-11-19): not built. Its main.cbricks / project.cbproj carry a .txt suffix so C-Bricks does not load them. The code lives on as app/mf_shader.c, app/mf_shed.c, core/mf_json.c.
