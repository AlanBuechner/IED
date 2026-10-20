# C4Script - the script host (docs/runtime/04): a DLL a program built with --script-host loads to compile, link and
# load C4 scripts while it runs. It carries the compiler (C4Lib) and LLVM, so it links every LLVM library.
import os
import inspect
import glob
import ProjectManager
import Config

projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
compilerLibDir = (Config.compilerFolder + "/lib").replace("\\", "/")

links = ["ntdll.lib", "kernel32.lib", "user32.lib", "advapi32.lib", "shell32.lib", "bcrypt.lib", "ole32.lib", "version.lib"]
# The static libraries only: LLVM-C.lib and LTO.lib import their DLLs, which a program that ships scripting should
# not need beside it.
kImportLibraries = { "LLVM-C.lib", "LTO.lib" }
if os.path.exists(compilerLibDir):
	for name in ["LLVM*.lib", "Remarks.lib"]:
		for path in sorted(glob.glob(os.path.join(compilerLibDir, name))):
			if os.path.basename(path) not in kImportLibraries: links.append(os.path.basename(path))


def GetProject():
	p = ProjectManager.Project()
	p.projectName = "C4Script"
	p.group = "C4"
	p.includes = [f"{projDir}/src"]
	p.sysIncludes = [Config.compilerInclude]
	p.externIncludes = [Config.compilerInclude]
	p.moduleLinks = ["C4Lib", "Core", "Reflection", "ObjectDescription", "Profiling"]
	p.links = links
	p.defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN", "C4SCRIPT_EXPORTS"]
	p.linkerArgs = ["-fuse-ld=lld"] # the vendored lld, as every other C4 binary; it also writes C4Script.lib
	p.buildType = Config.BuildType.DYNAMIC_LIBRARY
	p.genReflection = False
	p.buildStage = Config.BuildStage.NORMAL
	return p
