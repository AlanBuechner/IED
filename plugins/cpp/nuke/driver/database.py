"""compile_commands.json for clangd (IED's C/C++ plugin) and other C++ tools: the driver compiles with clang's -cc1
frontend, whose flags mean nothing to them, so each source's entry is the clang driver's form of the same compile.
It is kept at <workspace>/build/compile_commands.json, which clangd finds for every file of the workspace (it looks in
a build/ folder beside each folder above a file); each build replaces the entries of the sources it compiled and keeps
the others."""

import json
import os

DATABASE = "build/compile_commands.json"
DRIVER_FLAG_PREFIXES = ("-W", "-f", "-m", "-D", "-U")


def driver_arguments(toolchain, source, includes, system_includes, defines, compiler_args):
	"""The clang driver's command for compiling `source` as the build does."""
	is_c = source.lower().endswith(".c")
	args = [toolchain.clang, f"--target={toolchain.target}"]
	args += ["-std=" + toolchain.settings.c_standard, "-x", "c"] if is_c else ["-std=" + toolchain.settings.standard, "-x", "c++"]
	args += ["-fms-extensions", "-fms-compatibility", f"-fms-compatibility-version={toolchain.ms_version}", "-fexceptions"]
	if not is_c:
		args.append("-fcxx-exceptions")
	args += [a for a in compiler_args if a.startswith(DRIVER_FLAG_PREFIXES)]
	for folder in includes:
		args += ["-I", folder]
	for folder in system_includes:
		args += ["-isystem", folder]
	args += [f"-D{d}" for d in defines]
	return args + ["-c", source]


def entry(toolchain, target, source, includes, system_includes):
	return {"directory": target.dir, "file": source,
		"arguments": driver_arguments(toolchain, source, includes, system_includes, target.defines, target.compiler_args)}


def merged(existing, entries):
	"""`existing` with `entries` replacing those of the same files, sorted by file."""
	by_file = {os.path.normcase(e["file"]): e for e in existing if isinstance(e, dict) and "file" in e}
	by_file.update({os.path.normcase(e["file"]): e for e in entries})
	return [by_file[key] for key in sorted(by_file)]


def update(workspace_dir, entries):
	if not entries:
		return
	path = os.path.join(workspace_dir, DATABASE)
	try:
		with open(path, encoding="utf-8") as f:
			existing = json.load(f)
	except (OSError, ValueError):
		existing = []
	text = json.dumps(merged(existing if isinstance(existing, list) else [], entries), indent="\t") + "\n"
	try:
		with open(path, encoding="utf-8") as f:
			if f.read() == text:
				return
	except OSError:
		pass
	os.makedirs(os.path.dirname(path), exist_ok=True)
	temporary = path + ".tmp"
	with open(temporary, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)
	os.replace(temporary, path)
