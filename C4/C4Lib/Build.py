import os
import inspect
import glob
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
	p = ProjectManager.Project()
	p.group = "C4"
	p.includes = [f"{projDir}/src", f"{os.path.dirname(projDir)}/C4Runtime"]
	# LLVM headers from pre-built distribution (clang+llvm includes LLVM headers)
	p.sysIncludes = [f"{Config.compilerFolder}/include"]
	p.externIncludes = [f"{Config.compilerFolder}/include"]
	p.moduleLinks = ["Core", "Reflection", "ObjectDescription", "Profiling"]
	
	# Auto-discover all LLVM libraries for full target support
	compilerLibDir = Config.compilerFolder + "/lib"
	p.links = []
	if os.path.exists(compilerLibDir):
		for libPath in glob.glob(os.path.join(compilerLibDir, "LLVM*.lib")):
			# LLVM-C.lib imports LLVM-C.dll: its C API functions (LLVMInitializeX86Target, ...) would bind to the DLL's
			# copy of LLVM, whose target registry the statically linked compiler never sees.
			if os.path.basename(libPath).lower() == "llvm-c.lib": continue
			p.links.append(os.path.basename(libPath))
		for libPath in glob.glob(os.path.join(compilerLibDir, "Remarks.lib")):
			p.links.append(os.path.basename(libPath))
		for libPath in glob.glob(os.path.join(compilerLibDir, "LTO.lib")):
			p.links.append(os.path.basename(libPath))
	
	# Windows API libs needed by rpmalloc and runtime
	p.links.extend([
		"advapi32.lib",
		"kernel32.lib",
		"user32.lib",
		"gdi32.lib",
	])
	
	p.buildType = Config.BuildType.STATIC_LIBRARY
	p.genReflection = False
	p.buildStage = Config.BuildStage.EARLY
	return p
