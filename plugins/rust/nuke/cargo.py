"""Rust through cargo (IED docs/26 section 6). Nuke's configurations map onto cargo's profiles - one that does not
optimize is the dev profile, any other `--release`, with debug info when the configuration keeps it
(CARGO_PROFILE_RELEASE_DEBUG for the run, never written into Cargo.toml) - and the artifacts cargo reports (executables,
dynamic and static libraries, their .pdb) are what Nuke copies into build/bin. cargo keeps its own target/ folder,
Cargo.lock and registry cache, so building in IED and outside it share them.

[project.driver]:
package = "name"  a member of a Cargo workspace (default: the crate in the project's folder)
bin = "name"      the program `nuke run` starts (default: the package's name)
"""

import json
import os
import tomllib

STAGED_KINDS = {"bin", "cdylib", "dylib", "staticlib"}
NOT_STAGED = (".rlib", ".rmeta", ".d")


def profile_args(configuration):
	return [] if configuration.optimize == 0 else ["--release"]


def profile_env(configuration):
	return {"CARGO_PROFILE_RELEASE_DEBUG": "true"} if configuration.optimize != 0 and configuration.debug_info else None


def selection(ctx):
	args = ["--manifest-path", os.path.join(ctx.root, "Cargo.toml")]
	if ctx.options.get("package"):
		args += ["--package", ctx.options["package"]]
	return args


def artifacts(stdout):
	"""The files of cargo's compiler-artifact messages (one JSON object per line) that a program or another language
	links or runs: executables and dynamic or static libraries, with their debug files; Rust's own libraries are not."""
	found = []
	for line in stdout.splitlines():
		if not line.startswith("{"):
			continue
		message = json.loads(line)
		if message.get("reason") != "compiler-artifact" or not set(message.get("target", {}).get("kind", [])) & STAGED_KINDS:
			continue
		found += [path for path in message.get("filenames", []) if not path.endswith(NOT_STAGED)]
	return found


def build(ctx):
	argv = [ctx.tool("cargo"), "build", "--message-format=json-render-diagnostics"] + selection(ctx) + profile_args(ctx.configuration)
	_, stdout = ctx.capture(argv, env=profile_env(ctx.configuration))
	return artifacts(stdout)


def package_name(ctx):
	if ctx.options.get("package"):
		return ctx.options["package"]
	manifest = os.path.join(ctx.root, "Cargo.toml")
	try:
		with open(manifest, "rb") as f:
			return tomllib.load(f).get("package", {}).get("name") or ctx.project.name
	except (OSError, tomllib.TOMLDecodeError):
		return ctx.project.name


def program(ctx):
	if ctx.project.kind == "library":
		return None
	name = ctx.options.get("bin") or package_name(ctx)
	return f"{name}.exe" if ctx.system == "windows" else name


def test(ctx):
	argv = [ctx.tool("cargo"), "test"] + selection(ctx) + profile_args(ctx.configuration)
	if ctx.test_filter:
		argv.append(ctx.test_filter)
	if ctx.args:
		argv += ["--"] + list(ctx.args)
	return ctx.run(argv, env=profile_env(ctx.configuration), check=False)
