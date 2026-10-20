# CBind - C/C++ interop binding generator tool.
# Parses a foreign C/C++ header with in-process ClangTool and emits C4 declaration text.
import os
from pathlib import Path
import ProjectManager
import Config

compilerLibDir = (Config.compilerFolder + "/lib").replace("\\", "/")

links = [
	"ntdll.lib",
	"version.lib",
	"advapi32.lib",
	"kernel32.lib",
	"user32.lib",
]
if os.path.exists(compilerLibDir):
	for file in sorted(os.listdir(compilerLibDir)):
		if Path(file).suffix == ".lib":
			links.append(file)


def GetProject():
	p = ProjectManager.Project()
	p.projectName = "CBind"
	p.group = "C4"
	p.sysIncludes = [Config.compilerInclude]
	p.externIncludes = [Config.compilerInclude]
	p.links = links
	# lld-link deterministically fails on this toolchain with "relocation against symbol in
	# discarded section: protected ... clang::interp::ByteCodeEmitter::emitIncPtr" when CBind is
	# built at -O3 with CodeView debug info (a known lld GC bug on protected-symbol COMDATs;
	# reproduces on clean source). Building at -O0 avoids the optimized protected-COMDAT pattern,
	# linking cleanly. The optimization level is irrelevant for a one-shot binding generator.
	p.compilerArgs = ["-O0"]
	p.buildType = Config.BuildType.EXECUTABLE
	p.genReflection = False
	p.buildStage = Config.BuildStage.EARLY
	return p
