"""compile_commands.json for clangd and other C++ tools (docs/BuildSystem.md "Compile database").

Frost compiles with clang's cc1 frontend, whose flags mean nothing to a tool that reads a compile database, so each
source's entry is the clang driver's form of the same compile: its language and standard, target, the Microsoft
compatibility and exception flags, the project's include folders (system ones as -isystem), defines and its own
driver flags. Every build records the sources it saw and, as the build process ends, merges them into the database
at the root project's folder, so building one project keeps the others' entries."""

import atexit
import json
import os

import Config
from Utils.Utils import GetTarget
from Utils.Windows import GetFMSVersion

_recorded = {}
_registered = False


def DriverArguments(sourceFile, includes, sysIncludes, defines, compilerArgs):
	"""The clang driver's command for compiling `sourceFile` the way Frost does."""
	isC = sourceFile.endswith(".c")
	args = [Config.compiler, f"--target={GetTarget()}"]
	args += ["-std=c11", "-x", "c"] if isC else [f"-std={Config.cppVersion}", "-x", "c++"]
	args += ["-fms-extensions", "-fms-compatibility", f"-fms-compatibility-version={GetFMSVersion()}", "-fexceptions"]
	if not isC:
		args.append("-fcxx-exceptions")
	args += [a for a in compilerArgs if a.startswith(("-W", "-f", "-m", "-D", "-U"))]
	for folder in includes:
		args += ["-I", folder]
	for folder in sysIncludes:
		args += ["-isystem", folder]
	args += [f"-D{d}" for d in defines]
	args += ["-c", sourceFile]
	return args


def Record(workingDir, sourceFile, includes, sysIncludes, defines, compilerArgs):
	"""Remembers a source's entry; the database is written when the build process ends."""
	global _registered
	path = os.path.abspath(os.path.join(workingDir, sourceFile)).replace("\\", "/")
	_recorded[os.path.normcase(path)] = {
		"directory": os.path.abspath(workingDir).replace("\\", "/"),
		"file": path,
		"arguments": DriverArguments(path, includes, sysIncludes, defines, compilerArgs),
	}
	if not _registered:
		_registered = True
		atexit.register(Write, Config.projectLocation)


def Merged(existing, recorded):
	"""The entries of `existing` with `recorded` replacing those of the same files, sorted by file."""
	byFile = {os.path.normcase(e["file"]): e for e in existing if isinstance(e, dict) and "file" in e}
	byFile.update(recorded)
	return [byFile[key] for key in sorted(byFile)]


def Write(folder):
	if not _recorded or not folder:
		return
	path = os.path.join(folder, "compile_commands.json")
	existing = []
	if os.path.exists(path):
		try:
			with open(path, encoding="utf-8") as f:
				existing = json.load(f)
		except (OSError, ValueError):
			existing = []
	entries = Merged(existing if isinstance(existing, list) else [], _recorded)
	temporary = path + ".tmp"
	with open(temporary, "w", encoding="utf-8", newline="\n") as f:
		json.dump(entries, f, indent="\t")
	os.replace(temporary, path)
