"""Go through the go tool (IED docs/26 section 6). An executable is `go build -o` straight into its build/bin folder,
so nothing is copied; a library is compiled to check it (`go build ./...`). A configuration that does not optimize
builds with `-gcflags=all=-N -l` (what a debugger needs), one without debug info strips it (`-ldflags=-s -w`); the
platform is GOARCH and GOOS for the run. go.mod, go.sum and Go's build and module caches stay Go's.

[project.driver]:
package = "./cmd/game"  the main package an executable builds (default: the project's folder, ".")
name = "game"           the program's name (default: the project's)
"""

import os

GOARCH = {"x64": "amd64", "arm64": "arm64", "x86": "386"}


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


def program(ctx):
	if ctx.project.kind == "library":
		return None
	name = ctx.options.get("name") or ctx.project.name
	return f"{name}.exe" if ctx.system == "windows" else name


def build(ctx):
	if ctx.project.kind == "library":
		ctx.run([ctx.tool("go"), "build"] + flags(ctx.configuration) + ["./..."], env=platform_env(ctx))
		return []
	os.makedirs(ctx.bin_dir, exist_ok=True)
	output = os.path.join(ctx.bin_dir, program(ctx))
	ctx.run([ctx.tool("go"), "build", "-o", output] + flags(ctx.configuration) + [ctx.options.get("package", ".")], env=platform_env(ctx))
	return []


def test(ctx):
	argv = [ctx.tool("go"), "test"] + flags(ctx.configuration)
	if ctx.test_filter:
		argv += ["-run", ctx.test_filter]
	argv.append("./...")
	if ctx.args:
		argv += ["-args"] + list(ctx.args)
	return ctx.run(argv, env=platform_env(ctx), check=False)
