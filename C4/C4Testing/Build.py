# C4Testing build script - test executable for C4 language features
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
	p = ProjectManager.Project()
	p.group = "C4"
	p.moduleLinks = ["Core", "UnitTesting", "C4Lib", "ObjectDescription", "C4Protocol", "C4LSPLib", "C4DAPLib"]
	# ntdll: LLVMSupport (RtlGetLastNtStatus); dbghelp: the debug adapter; shell32: its command-line test
	p.links = ["ntdll.lib", "dbghelp.lib", "shell32.lib"]
	p.defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN"] # as the debug adapter, whose headers include windows.h
	p.buildType = Config.BuildType.EXECUTABLE
	p.genReflection = True
	p.buildStage = Config.BuildStage.NORMAL
	p.isUnitTestProject = True

	# The codegen tests include the vendored LLVM headers directly (they drive CodeGenDriver in-process),
	# so they need the same include path the root build gives C4Lib's backend.
	compilerIncludeDir = (Config.compilerFolder + "/include").replace("\\", "/")
	if os.path.exists(compilerIncludeDir):
		p.includes = [compilerIncludeDir]

	return p

