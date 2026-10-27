"""Go through the go tool (IED docs/26 section 6). An executable is `go build -o` straight into its build/bin folder,
so nothing is copied; a library is compiled to check it (`go build ./...`). A configuration that does not optimize
builds with `-gcflags=all=-N -l` (what a debugger needs), one without debug info strips it (`-ldflags=-s -w`); the
platform is GOARCH and GOOS for the run. go.mod, go.sum and Go's build and module caches stay Go's.

While the workspace develops a package source (`nuke package develop`), the modules of its clone that the project
requires build from the clone: GOWORK names a go.work in build/develop/<project>/ that uses the project and those
module folders (and whatever the project's own go.work uses), so an edit in the clone needs no release (IED docs/31
section 7). Nothing of the project changes, and `nuke package lock` removes the go.work.

[project.driver]:
package = "./cmd/game"  the main package an executable builds (default: the project's folder, ".")
name = "game"           the program's name (default: the project's)
"""

import json
import os
import re
from dataclasses import dataclass

GOARCH = {"x64": "amd64", "arm64": "arm64", "x86": "386"}
OLDEST_WORK = "1.18"


@dataclass(frozen=True)
class Module:
	"""What a go.mod says that a go.work needs."""
	path: str            # the module path
	folder: str
	go: str              # its go directive ("" without one)
	requires: tuple      # the module paths it requires


def flags(configuration):
	args = []
	if configuration.optimize == 0:
		args.append("-gcflags=all=-N -l")
	if not configuration.debug_info:
		args.append("-ldflags=-s -w")
	return args


def platform_env(ctx):
	env = {"GOOS": ctx.system}
	if ctx.arch in GOARCH:
		env["GOARCH"] = GOARCH[ctx.arch]
	return env


def environment(ctx):
	return dict(platform_env(ctx), **develop_env(ctx))


def develop_env(ctx):
	"""GOWORK naming the go.work that builds the develop checkouts' modules the project requires from their clones; {}
	when it requires none."""
	if not ctx.checkouts:
		return {}
	path = ctx.develop_file("go.work", develop_work(ctx))
	return {"GOWORK": path} if path else {}


def develop_work(ctx):
	"""The go.work's text, or None when the project requires no module of a develop checkout."""
	project = read_module(os.path.join(ctx.root, "go.mod"))
	if project is None:
		return None
	available = {}
	for checkout in ctx.checkouts:
		for path in checkout.files("go.mod"):
			module = read_module(path)
			if module is not None and module.path:
				available.setdefault(module.path, module)
	used = required(project, available)
	return render_work(project, used, own_work(ctx)) if used else None


def read_module(path):
	"""The module a go.mod declares; None when there is no go.mod."""
	try:
		with open(path, encoding="utf-8") as f:
			text = f.read()
	except OSError:
		return None
	values, requires, block = {"module": "", "go": ""}, [], None
	for line in text.splitlines():
		words = line.split("//", 1)[0].split()
		if not words:
			continue
		if block is not None:
			block = None if words == [")"] else block
			if block == "require":
				requires.append(words[0].strip('"`'))
		elif words[1:] == ["("]:
			block = words[0]
		elif words[0] == "require" and len(words) > 1:
			requires.append(words[1].strip('"`'))
		elif words[0] in values and len(words) > 1:
			values[words[0]] = words[1].strip('"`')
	return Module(values["module"], os.path.dirname(os.path.abspath(path)), values["go"], tuple(requires))


def required(project, available):
	"""The modules among `available` the project requires, directly or through another of them."""
	used, pending = {}, list(project.requires)
	while pending:
		path = pending.pop()
		module = available.get(path)
		if module is not None and path not in used and path != project.path:
			used[path] = module
			pending += module.requires
	return [used[path] for path in sorted(used)]


def own_work(ctx):
	"""The go.work Go would use for the project without Nuke - GOWORK, else the nearest one at or above its folder - as
	`go work edit -json` reads it, with its folder; None when there is none."""
	path = os.environ.get("GOWORK", "")
	if path == "off":
		return None
	folder = os.path.abspath(ctx.root)
	while not path:
		if os.path.isfile(os.path.join(folder, "go.work")):
			path = os.path.join(folder, "go.work")
		elif os.path.dirname(folder) == folder:
			return None
		folder = os.path.dirname(folder)
	_, out = ctx.capture([ctx.tool("go"), "work", "edit", "-json", path])
	return json.loads(out), os.path.dirname(os.path.abspath(path))


def render_work(project, used, own):
	"""A go.work of the project and the used modules, keeping what the project's own go.work has. Paths are absolute and
	the system's own (Go matches a module's folder only in that spelling)."""
	folders, versions, extra = [project.folder] + [m.folder for m in used], [project.go] + [m.go for m in used], []
	if own is not None:
		work, base = own
		folders += [os.path.join(base, use["DiskPath"]) for use in work.get("Use") or []]
		versions.append(work.get("Go") or "")
		extra += [f"toolchain {work['Toolchain']}"] if work.get("Toolchain") else []
		extra += [f"replace {replace_line(r, base)}" for r in work.get("Replace") or []]
	unique = {}
	for folder in folders:
		folder = os.path.normpath(os.path.abspath(folder))
		unique.setdefault(os.path.normcase(folder), folder)
	lines = ["// Nuke's while a package source is developed: the clone's modules build in place of their releases",
		"// (IED docs/31 section 7). `nuke package lock` removes this file.", f"go {newest(versions)}"] + extra
	lines += ["", "use ("] + [f"\t{json.dumps(folder)}" for folder in unique.values()] + [")"]
	return "\n".join(lines) + "\n"


def replace_line(replace, base):
	new = replace["New"]
	target = new["Path"]
	if not new.get("Version"):
		target = json.dumps(os.path.normpath(os.path.join(base, target)))
	old = " ".join(filter(None, (replace["Old"]["Path"], replace["Old"].get("Version"))))
	return f"{old} => {' '.join(filter(None, (target, new.get('Version'))))}"


def newest(versions):
	"""The newest Go version among go directives (at least the first that has go.work)."""
	known = [v for v in versions if v] + [OLDEST_WORK]
	return max(known, key=lambda v: tuple(int(n) for n in re.match(r"[\d.]*", v).group(0).split(".") if n))


def program(ctx):
	if ctx.project.kind == "library":
		return None
	name = ctx.options.get("name") or ctx.project.name
	return f"{name}.exe" if ctx.system == "windows" else name


def build(ctx):
	if ctx.project.kind == "library":
		ctx.run([ctx.tool("go"), "build"] + flags(ctx.configuration) + ["./..."], env=environment(ctx))
		return []
	os.makedirs(ctx.bin_dir, exist_ok=True)
	output = os.path.join(ctx.bin_dir, program(ctx))
	ctx.run([ctx.tool("go"), "build", "-o", output] + flags(ctx.configuration) + [ctx.options.get("package", ".")], env=environment(ctx))
	return []


def test(ctx):
	argv = [ctx.tool("go"), "test"] + flags(ctx.configuration)
	if ctx.test_filter:
		argv += ["-run", ctx.test_filter]
	argv.append("./...")
	if ctx.args:
		argv += ["-args"] + list(ctx.args)
	return ctx.run(argv, env=environment(ctx), check=False)
