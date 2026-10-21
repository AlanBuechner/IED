# Frost checks for ToolsTest/RunTests.ps1, run against the repository's Frost:
#   FrostProbes.py long-link <folder>     links 400 objects whose paths outgrow a Windows command line (BUG-260);
#                                          prints the link's exit code and the command's length
#   FrostProbes.py project-name <folder>  loads a project whose folder is no identifier and whose Build.py names it
#                                          (BUG-262); prints its name and its PROJECT_NAME define
#   FrostProbes.py build-order             orders early projects loaded before what they link (BUG-265); prints the
#                                          order
#   FrostProbes.py failed-compile <folder> compiles a broken file whose object and dependency file a crashed compile
#                                          left (BUG-255); prints the exit code and whether they are gone
#   FrostProbes.py compile-database <folder> records a C and a C++ source into a database that has another file's
#                                          entry and a stale one of the C++ source; prints the files it holds, the
#                                          C++ entry's flags of note, and the exit code of the clang driver checking
#                                          the C++ source with the recorded arguments
import contextlib
import io
import os
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Frost"))

import Config
import ProjectManager
from Builder.Linker import LinkObjects
from Builder.ObjectBuilder import ObjectBuilder
import Utils.CompileDatabase as CompileDatabase
import json

OBJECTS = 400
WINDOWS_COMMAND_LIMIT = 32767

PROJECT_SCRIPT = """import ProjectManager
import Config

def GetProject():
	p = ProjectManager.Project()
	p.projectName = "Named"
	p.buildType = Config.BuildType.STATIC_LIBRARY
	return p
"""


def LongLink(folder):
	os.makedirs(folder, exist_ok=True)
	source = os.path.join(folder, "empty.c")
	with open(source, "w") as f:
		f.write("int c4_probe_unused(void);\n")
	first = os.path.join(folder, "an_object_with_a_long_name_000.obj")
	subprocess.run([Config.compiler, "-c", "-target", "x86_64-pc-windows-msvc", source, "-o", first], check=True)
	for i in range(1, OBJECTS):
		shutil.copy(first, os.path.join(folder, f"an_object_with_a_long_name_{i:03}.obj"))
	length = sum(len(os.path.join(folder, name)) + 1 for name in os.listdir(folder) if name.endswith(".obj"))
	output = os.path.join(folder, "probe.lib")
	with contextlib.redirect_stdout(io.StringIO()):   # Frost's own log
		code = LinkObjects(folder, [], folder, output, [], Config.BuildType.STATIC_LIBRARY, True)[0]
	print(code if os.path.isfile(output) else "no library")
	print("long" if length > WINDOWS_COMMAND_LIMIT else f"short ({length})")


def ProjectName(folder):
	os.makedirs(folder, exist_ok=True)
	with open(os.path.join(folder, "Build.py"), "w") as f:
		f.write(PROJECT_SCRIPT)
	path = folder.replace("\\", "/")
	with contextlib.redirect_stdout(io.StringIO()):
		ProjectManager.LoadProject(path)
	project = ProjectManager.projects[path]
	print(project.projectName)
	print(next(d for d in project.defines if d.startswith("PROJECT_NAME=")))


def Fake(name, stage, links):
	project = ProjectManager.Project()
	project.projectName = name
	project.folder = f"probe/{name}"
	project.buildStage = stage
	project.moduleLinks = links
	ProjectManager.projects[project.folder] = project
	return project


def BuildOrder(_):
	early = Config.BuildStage.EARLY
	loaded = [Fake("Tool", early, ["Reflect"]), Fake("Reflect", early, ["Base", "Text"]), Fake("Text", early, ["Base"]),
		Fake("Base", early, []), Fake("Game", Config.BuildStage.NORMAL, ["Tool"])]
	print(",".join(project.projectName for project in ProjectManager.InBuildOrder(loaded, early)))


def FailedCompile(folder):
	os.makedirs(folder, exist_ok=True)
	builder = ObjectBuilder()
	builder.sourceFile = os.path.join(folder, "broken.c")
	builder.objFile = os.path.join(folder, "broken.obj")
	builder.treeFile = os.path.join(folder, "broken.d")
	builder.argsFile = os.path.join(folder, "broken.args")
	builder.workingDir = folder
	builder.intDir = folder
	builder.reflect = False
	with open(builder.objFile, "w") as f:
		f.write("left by a compile that crashed after writing it")
	with open(builder.treeFile, "w") as f:
		f.write(f"{builder.treeFile}: {builder.sourceFile}\n")
	with open(builder.sourceFile, "w") as f:
		f.write("int broken(void) { return 1 }\n")
	with contextlib.redirect_stdout(io.StringIO()):
		code = builder.Build()
	print(code)
	print("kept" if os.path.exists(builder.objFile) or os.path.exists(builder.treeFile) else "gone")


def CompileDatabaseProbe(folder):
	include = os.path.join(folder, "include")
	os.makedirs(include, exist_ok=True)
	with open(os.path.join(include, "probe.h"), "w") as f:
		f.write("#pragma once\n#include <vector>\ninline int Probe() { return PROBE_VALUE; }\n")
	with open(os.path.join(folder, "probe.cpp"), "w") as f:
		f.write('#include "probe.h"\nstd::vector<int> g_values = { Probe() };\n')
	with open(os.path.join(folder, "probe.c"), "w") as f:
		f.write("int c_probe(void) { return 1; }\n")
	other = os.path.join(folder, "other.cpp").replace("\\", "/")
	stale = {"directory": folder, "file": os.path.join(folder, "probe.cpp").replace("\\", "/"), "arguments": ["stale"]}
	with open(os.path.join(folder, "compile_commands.json"), "w") as f:
		json.dump([{"directory": folder, "file": other, "arguments": ["other"]}, stale], f)
	from Utils.Utils import GetSysIncludes
	CompileDatabase.Record(folder, "probe.cpp", [include], GetSysIncludes(), ["PROBE_VALUE=7"], ["-O0"])
	CompileDatabase.Record(folder, "probe.c", [], GetSysIncludes(), [], [])
	CompileDatabase.Write(folder)
	with open(os.path.join(folder, "compile_commands.json"), encoding="utf-8") as f:
		entries = json.load(f)
	print(",".join(os.path.basename(e["file"]) for e in entries))
	cpp = next(e for e in entries if e["file"].endswith("probe.cpp"))["arguments"]
	c = next(e for e in entries if e["file"].endswith("probe.c"))["arguments"]
	notes = ["cc1" if "-cc1" in cpp else "driver", "-isystem" if "-isystem" in cpp else "no-isystem", "-O0" if "-O0" in cpp else "no-O",
		"c-as-c" if "-x" in c and c[c.index("-x") + 1] == "c" and "-fcxx-exceptions" not in c else "c-wrong"]
	print(",".join(notes))
	check = [arg for arg in cpp if arg != "-c"] + ["-fsyntax-only"]
	print(subprocess.run(check, cwd=folder, capture_output=True, text=True).returncode)


if __name__ == "__main__":
	{"long-link": LongLink, "project-name": ProjectName, "build-order": BuildOrder, "failed-compile": FailedCompile,
		"compile-database": CompileDatabaseProbe}[sys.argv[1]](sys.argv[2] if len(sys.argv) > 2 else "")
