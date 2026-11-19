extends SceneTree
## Builds a C-Bricks project without the editor window: the graph -> C (ProjectBuild), then the
## native compile (Toolchains, raylib included), the same calls the editor's Build makes.
##   tools/build.sh                   (wraps this; see it for the command)
## Output: <project>/.build/src (the generated C), <project>/.build/native/<name> (the program).
## Exit code: 0 built, 1 problems / didn't compile.
## It only writes inside the project's own .build/ folder.

func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("usage: -- <project dir> [strict]")
		quit(2)
		return
	var dir := args[0].simplify_path()
	var strict := args.size() > 1 and args[1] == "strict"
	if not CBProject.is_project(dir):
		printerr("not a C-Bricks project (no project.cbproj): %s" % dir)
		quit(2)
		return
	var r: Dictionary = ProjectBuild.generate(dir)
	for w in r.warnings:
		print("warning: ", w.msg if w is Dictionary else str(w))
	if not r.errors.is_empty():
		for e in r.errors:
			printerr("problem: ", e.msg)
		quit(1)
		return
	var build := dir.path_join(".build")
	var srcs := ProjectBuild.write(r, build.path_join("src"))
	var rl: Dictionary = {}
	if r.graphics:
		print("building raylib for this computer (cached after the first time)...")
		rl = Toolchains.build_raylib("native")
		if not rl.ok:
			printerr("raylib didn't build:\n", str(rl.output))
			quit(1)
			return
	var base := "meshforge"
	var template := ""
	if strict:
		template = "cc -std=c99 -O1 -Wall -Wextra -pedantic {src} -o {out} -lm"
	var built: Dictionary = Toolchains.compile("native", srcs, build.path_join("native"), base, template, rl)
	if not built.ok:
		printerr("didn't compile:\n", str(built.output))
		quit(1)
		return
	var warnings := str(built.output).strip_edges()
	if warnings != "":
		print("compiler output:\n", warnings)
	print("built: ", built.out)
	quit(0)
