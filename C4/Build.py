# test project build script
import os
import sys
import glob
import ProjectManager as ProjectManager
import Config

sys.dont_write_bytecode = True  # never drop __pycache__ into the caller's directory

location = os.path.dirname(__file__).replace("\\", "/")
compilerLibDir = (Config.compilerFolder + "/lib").replace("\\", "/")

enableProfiling = False
enableMemoryTracker = False

includes = [
	location + "/src",
]

# The vendored LLVM headers (the backend #include <llvm/IR/...>). compilerFolder is the same path whose
# /lib holds the LLVM*.lib files linked below; its /include holds the C++ API headers.
compilerIncludeDir = (Config.compilerFolder + "/include").replace("\\", "/")
if os.path.exists(compilerIncludeDir):
	includes.append(compilerIncludeDir)

defines = [
	"NOMINMAX",
	"WIN32_LEAN_AND_MEAN",
]

globalDefines = [
]

vsDefines = [
	"EDITOR",
]

modulePaths = [
	"C4Lib",
	"C4Testing",
	"C4Protocol",
	"C4LSPLib",
	"C4LSP",
	"C4DAPLib",
]

# Binaries of this workspace that build with C4 but are not part of it: the interop binding generator, the debug
# adapter and the script host (C4Script.dll).
toolPaths = [
	"CBind",
	"C4DAP",
	"C4Script",
]

moduleLinks = [
	"C4Lib",
	"Core",
	"Reflection",
	"ObjectDescription",
	"Profiling",
	"UnitTesting",
	"C4LSP",
]

links = [
	"kernel32.lib",
	"user32.lib",
	"comdlg32.lib",
	"shell32.lib",
	"bcrypt.lib",
	"ntdll.lib",
]

# Add all LLVM libraries automatically from the compiler lib directory
llvmLibs = []
if os.path.exists(compilerLibDir):
	for libPath in glob.glob(os.path.join(compilerLibDir, "LLVM*.lib")):
		# LLVM-C.lib imports LLVM-C.dll: its C API functions (LLVMInitializeX86Target, ...) would bind to the DLL's
		# copy of LLVM, whose target registry the statically linked compiler never sees.
		if os.path.basename(libPath).lower() == "llvm-c.lib": continue
		llvmLibs.append(os.path.basename(libPath))
	for libPath in glob.glob(os.path.join(compilerLibDir, "Remarks.lib")):
		llvmLibs.append(os.path.basename(libPath))
	for libPath in glob.glob(os.path.join(compilerLibDir, "LTO.lib")):
		llvmLibs.append(os.path.basename(libPath))
links.extend(llvmLibs)

if(Config.configuration == "Dist"):
	enableProfiling = False
	enableGPUProfiling = False
	enableGPUDebugging = False

if(enableProfiling):
	globalDefines.append("ENABLE_PROFILING=1")
if(enableMemoryTracker):
	globalDefines.append("ENABLE_MEMORY_TRACKER=1")


def GetProject():
	p = ProjectManager.Project()
	p.projectName = "C4"   # C4.exe wherever the checkout is (a git worktree's folder has another name)
	p.group = "C4"
	p.includes = includes
	p.modulePaths = modulePaths
	p.moduleLinks = moduleLinks
	p.toolPaths = toolPaths
	p.defines = defines
	p.globalDefines = globalDefines
	p.links = links
	p.buildType = Config.BuildType.EXECUTABLE
	p.isRootProject = True
	p.copies = []
	
	# Copy LLVM DLLs to output directory so the compiler can find them at runtime
	llvmBinDir = Config.compilerFolder + "/bin"
	if os.path.exists(llvmBinDir):
		for dll in ["LLVM-C.dll", "libclang.dll", "LTO.dll"]:
			src = os.path.join(llvmBinDir, dll)
			if os.path.exists(src):
				p.copies.append(src)

	# Ship the Bedrock default library next to C4.exe. The compiler resolves its default library at
	# <exeDir>/Bedrock/bedrock.ch (see Source.cpp RegisterSources); without this a clean build leaves
	# an exe that cannot find bedrock.ch and every standalone compile fails with "include not found".
	bedrockDir = location + "/Bedrock"
	if os.path.isdir(bedrockDir):
		p.copies.append(bedrockDir)

	# The rest of the install layout C4.exe finds relative to itself (D-56): the runtime's sources in C4Runtime/
	# and the clang + lld-link it compiles and links with, with clang's own headers, in toolchain/. A copy of
	# the folder (MakeToolchain.py assembles the toolchain from it) then builds on any machine.
	runtimeDir = location + "/C4Runtime"
	if os.path.isdir(runtimeDir):
		p.copies.append(runtimeDir)
	toolchainBin = [os.path.join(llvmBinDir, tool) for tool in ["clang.exe", "lld-link.exe"]]
	if all(os.path.exists(tool) for tool in toolchainBin):
		p.copies.append({ "folder": "toolchain/bin", "files": toolchainBin })
	clangHeaders = Config.compilerFolder + "/lib/clang"
	if os.path.isdir(clangHeaders):
		p.copies.append({ "folder": "toolchain/lib", "files": [clangHeaders] })

	return p
