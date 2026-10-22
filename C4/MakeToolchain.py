# Assembles a C4 toolchain - the install Nuke compiles C4 projects with - from a Nuke build of this folder's projects:
#
#   python MakeToolchain.py --out <dir> [--config Development] [--bin <the configuration's build/bin folder>]
#
#   <dir>/
#     toolchain.json        name, version (C4.exe --version), git commit, modified flag, build time
#     C4.exe, *.dll, *.pdb  the compiler; it finds everything below relative to itself (docs/status/plan.md D-56)
#     Bedrock/  C4Runtime/  toolchain/{bin,lib}   default library, runtime sources, clang + lld-link + llvm-rc
#     CBind/    C4Script/                         binding generator, script host
#     tools/C4LSP/  tools/C4DAP/                  language server (+ the LLVM dlls it loads), debug adapter
#     docs/                 the user-facing language, library, runtime and build docs
#     ied/                  the IED plugin for this version of the language, its AI skills included (IedPlugin/)
#
# The folder is mirrored: unchanged files are not copied again and files gone from the build are removed.
# C4/nuke.toml runs this after every Development build to refresh the "dev" toolchain.

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
REPOSITORY = os.path.dirname(HERE)
COMPILER_FILES = (".exe", ".dll", ".pdb")
COMPILER_FOLDERS = ("Bedrock", "C4Runtime", "toolchain")
TOOLS = {
	"CBind": ("CBind", ["CBind.exe", "CBind.pdb"]),
	"C4Script": ("C4Script", ["C4Script.dll", "C4Script.lib"]),
	"tools/C4LSP": ("C4LSP", ["C4LSP.exe", "C4LSP.pdb"]),
	"tools/C4DAP": ("C4DAP", ["C4DAP.exe", "C4DAP.pdb"]),
}
LSP_DLLS = ("libclang.dll", "LLVM-C.dll", "LTO.dll")
REQUIRED = ["C4.exe", "Bedrock/bedrock.ch", "C4Runtime", "toolchain/bin/clang.exe", "toolchain/bin/lld-link.exe", "toolchain/bin/llvm-rc.exe",
	"CBind/CBind.exe", "C4Script/C4Script.dll", "ied/ied-plugin.json"]
DOCS = [
	"01-what-is-c4.md",
	"language/01-program-model.md", "language/02-preprocessor.md", "language/03-lexical-structure.md",
	"language/04-types.md", "language/05-objects.md", "language/06-lifecycle-and-operators.md",
	"language/07-functions-and-lambdas.md", "language/08-templates.md", "language/09-statements-and-control-flow.md",
	"language/10-expressions.md", "language/11-vectors-and-swizzling.md", "language/12-annotations.md",
	"language/13-interop.md", "language/14-builtins.md", "language/15-projects.md",
	"library/bedrock.md",
	"runtime/01-memory-and-gc.md", "runtime/02-reflection.md", "runtime/03-testing.md", "runtime/04-scripting.md",
	"tools/01-compiler-cli.md", "tools/02-nuke.md", "tools/03-editor-lsp-dap.md",
]


def files_below(folder):
	"""Every regular file below `folder`, relative; symbolic links and junctions are not followed."""
	found = []
	for base, dirs, files in os.walk(folder):
		dirs[:] = [d for d in dirs if not os.path.islink(os.path.join(base, d)) and not _is_junction(os.path.join(base, d))]
		for name in files:
			path = os.path.join(base, name)
			if not os.path.islink(path):
				found.append(os.path.relpath(path, folder))
	return found


def _is_junction(path):
	return hasattr(os.path, "isjunction") and os.path.isjunction(path)


def plan(build, repo):
	"""destination (relative to the toolchain) -> source file."""
	files = {}
	compiler = os.path.join(build, "C4")
	for name in sorted(os.listdir(compiler)):
		if os.path.isfile(os.path.join(compiler, name)) and name.lower().endswith(COMPILER_FILES):
			files[name] = os.path.join(compiler, name)
	for folder in COMPILER_FOLDERS:
		for rel in files_below(os.path.join(compiler, folder)):
			files[os.path.join(folder, rel)] = os.path.join(compiler, folder, rel)
	for dest, (project, names) in TOOLS.items():
		for name in names:
			source = os.path.join(build, project, name)
			if os.path.isfile(source):
				files[os.path.join(dest, name)] = source
	if os.path.isfile(os.path.join(build, "C4LSP", "C4LSP.exe")):
		for dll in LSP_DLLS:
			if os.path.isfile(os.path.join(compiler, dll)):
				files[os.path.join("tools", "C4LSP", dll)] = os.path.join(compiler, dll)
	for doc in DOCS:
		source = os.path.join(repo, "docs", *doc.split("/"))
		if os.path.isfile(source):
			files[os.path.join("docs", *doc.split("/"))] = source
	for rel in files_below(os.path.join(repo, "IedPlugin")):
		files[os.path.join("ied", rel)] = os.path.join(repo, "IedPlugin", rel)
	return {os.path.normpath(k): v for k, v in files.items()}


def copy_if_newer(source, dest):
	if os.path.exists(dest) and os.path.getmtime(dest) >= os.path.getmtime(source) and os.path.getsize(dest) == os.path.getsize(source):
		return False
	os.makedirs(os.path.dirname(dest), exist_ok=True)
	if os.path.exists(dest):
		os.remove(dest)
	shutil.copy2(source, dest)
	return True


def mirror(files, out):
	copied = sum(copy_if_newer(source, os.path.join(out, dest)) for dest, source in files.items())
	wanted = {os.path.normcase(d) for d in files} | {os.path.normcase("toolchain.json")}
	removed = 0
	for rel in files_below(out) if os.path.isdir(out) else []:
		if os.path.normcase(rel) not in wanted:
			os.remove(os.path.join(out, rel))
			removed += 1
	for base, dirs, _ in os.walk(out, topdown=False):
		if base != out and not os.listdir(base):
			os.rmdir(base)
	return copied, removed


def git(*args):
	try:
		result = subprocess.run(["git", "-C", HERE] + list(args), capture_output=True, text=True, timeout=60)
	except (OSError, subprocess.TimeoutExpired):
		return None
	return result.stdout.strip() if result.returncode == 0 else None


def version_of(compiler):
	output = subprocess.run([compiler, "--version"], capture_output=True, text=True, timeout=60).stdout.split()
	return output[-1] if output else None


def metadata(out, configuration):
	status = git("status", "--porcelain", "--", ".")
	return {
		"name": "c4",
		"version": version_of(os.path.join(out, "C4.exe")),
		"commit": git("rev-parse", "HEAD"),
		"dirty": bool(status) if status is not None else None,
		"configuration": configuration,
	}


def write_metadata(out, fields, changed):
	path = os.path.join(out, "toolchain.json")
	previous = {}
	if os.path.isfile(path):
		with open(path, encoding="utf-8") as f:
			previous = json.load(f)
	if not changed and {k: previous.get(k) for k in fields} == fields:
		return False
	fields = dict(fields, built=datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat())
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(json.dumps(fields, indent=2) + "\n")
	return True


def main(argv):
	parser = argparse.ArgumentParser(description="Assemble a C4 toolchain from a Nuke build of the compiler")
	parser.add_argument("--out", required=True, help="the toolchain folder to create or refresh")
	parser.add_argument("--config", default="Development", help="the configuration that was built")
	parser.add_argument("--bin", help="the build's bin folder of that configuration (default: <repository>/build/bin/<config>-windows-x64)")
	options = parser.parse_args(argv)
	build = os.path.abspath(options.bin or os.path.join(REPOSITORY, "build", "bin", f"{options.config}-windows-x64"))
	if not os.path.isfile(os.path.join(build, "C4", "C4.exe")):
		print(f"MakeToolchain: no compiler build at {build}/C4 (build it first: nuke build C4 {options.config})")
		return 1
	out = os.path.abspath(options.out)
	copied, removed = mirror(plan(build, HERE), out)
	missing = [f for f in REQUIRED if not os.path.exists(os.path.join(out, *f.split("/")))]
	if missing:
		print(f"MakeToolchain: {out} is incomplete, missing {', '.join(missing)} (did every tool build?)")
		return 1
	fields = metadata(out, options.config)
	write_metadata(out, fields, copied or removed)
	print(f"MakeToolchain: c4 {fields['version']} -> {out} ({copied} file(s) copied, {removed} removed)")
	return 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
