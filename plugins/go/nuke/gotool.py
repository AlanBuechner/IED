"""Go through the go tool (IED docs/26 section 6). An executable is `go build -o` straight into its build/bin folder,
so nothing is copied; a library is compiled to check it (`go build ./...`). A configuration that does not optimize
builds with `-gcflags=all=-N -l` (what a debugger needs), one without debug info strips it (`-ldflags=-s -w`); the
platform is GOARCH and GOOS for the run. go.mod, go.sum and Go's build and module caches stay Go's.

While the workspace develops a package source (`nuke package develop`), the modules of its clone that the project
requires build from the clone: GOWORK names a go.work in build/develop/<project>/ that uses the project and those
module folders (and whatever the project's own go.work uses), so an edit in the clone needs no release (IED docs/31
section 7). Nothing of the project changes, and `nuke package lock` removes the go.work. For IED's gopls, editor()
writes one go.work of every Go project of the workspace in build/develop/, and gopls.json, the gopls profile's options,
naming it.

[project.driver]:
package = "./cmd/game"  the main package an executable builds (default: the project's folder, ".")
name = "game"           the program's name (default: the project's)
"""

import hashlib
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
	used = required(project, clone_modules(ctx.checkouts))
	return render_work([project] + used, own_works(ctx, [ctx.root])) if used else None


def editor(folder, contexts):
	"""gopls's view of the develop checkouts (IED docs/31 section 7): go.work using every Go project of the workspace -
	gopls serves them all with it - and the clones' modules they require, and gopls.json, the options of the go plugin's
	gopls profile, giving gopls that go.work (build.env) with its digest, so a changed go.work starts gopls again. Both
	go when no project requires a module of a clone."""
	projects = [m for m in (read_module(os.path.join(ctx.root, "go.mod")) for ctx in contexts) if m is not None]
	available = clone_modules(contexts[0].checkouts) if contexts else {}
	used = {}
	for project in projects:
		for module in required(project, available):
			used.setdefault(module.path, module)
	if not used:
		return {"go.work": None, "gopls.json": None}
	text = render_work(projects + [used[path] for path in sorted(used)], own_works(contexts[0], [ctx.root for ctx in contexts]))
	env = {"GOWORK": os.path.normpath(os.path.join(folder, "go.work")), "NUKE_DEVELOP": hashlib.sha256(text.encode("utf-8")).hexdigest()[:12]}
	return {"go.work": text, "gopls.json": json.dumps({"build.env": env}, indent=2) + "\n"}


def clone_modules(checkouts):
	"""module path -> Module of every Go module in the develop checkouts."""
	available = {}
	for checkout in checkouts:
		for path in checkout.files("go.mod"):
			module = read_module(path)
			if module is not None and module.path:
				available.setdefault(module.path, module)
	return available


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


def own_works(ctx, folders):
	"""The go.work files Go would use for `folders` without Nuke - GOWORK, else the nearest one at or above each - as
	`go work edit -json` reads them, each with its folder."""
	paths = {}
	for folder in folders:
		path = own_work_path(folder)
		if path is not None:
			paths.setdefault(os.path.normcase(path), path)
	return [(json.loads(ctx.capture([ctx.tool("go"), "work", "edit", "-json", path])[1]), os.path.dirname(path)) for path in paths.values()]


def own_work_path(folder):
	path = os.environ.get("GOWORK", "")
	if path:
		return None if path == "off" else os.path.abspath(path)
	folder = os.path.abspath(folder)
	while not os.path.isfile(os.path.join(folder, "go.work")):
		if os.path.dirname(folder) == folder:
			return None
		folder = os.path.dirname(folder)
	return os.path.join(folder, "go.work")


def render_work(modules, owns):
	"""A go.work of `modules`, keeping what the go.work files `owns` have. Paths are absolute and the system's own (Go
	matches a module's folder only in that spelling)."""
	folders, versions, toolchains, replaces = [m.folder for m in modules], [m.go for m in modules], [], []
	for work, base in owns:
		folders += [os.path.join(base, use["DiskPath"]) for use in work.get("Use") or []]
		versions.append(work.get("Go") or "")
		toolchains += [f"toolchain {work['Toolchain']}"] if work.get("Toolchain") else []
		replaces += [f"replace {replace_line(r, base)}" for r in work.get("Replace") or []]
	unique = {}
	for folder in folders:
		folder = os.path.normpath(os.path.abspath(folder))
		unique.setdefault(os.path.normcase(folder), folder)
	lines = ["// Nuke's while a package source is developed: the clone's modules build in place of their releases",
		"// (IED docs/31 section 7). `nuke package lock` removes this file.", f"go {newest(versions)}"]
	lines += toolchains[:1] + list(dict.fromkeys(replaces))
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
