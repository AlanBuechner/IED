import os
from enum import Enum
location = os.path.dirname(__file__).replace("\\", "/")
projectLocation = ""

tools = [
	"tools/Socrates"
]

configurations = [
	"Debug",
	"Development",
	"Dist",
]
configuration = configurations[1] # default to Development configuration

architectures = [
	"x64",
]
architecture = architectures[0]
arcAliases = {
	"x86_64": architectures[0],
}

systems = [
	"windows",
]
system = systems[0] # default to windows

globalDefines: list[str] = []

useAsan = False
# Parallel compiles: 0 is one per processor. `-j N` or the FROST_JOBS environment variable sets it.
jobs = int(os.environ.get("FROST_JOBS", "0") or "0")
useDistPDBs = False
printCompileInfo = False

binDir = "bin/{config}-{system}-{arc}/{projName}"
intDir = "bin-int/{config}-{system}-{arc}/{projName}"

toolsBinDir = "bin/tools/{projName}"
toolsIntDir = "bin-int/tools/{projName}" 

cppVersion = "c++20"

compilerFolder = f"{location}/vendor/Compiler"
compiler = f"{compilerFolder}/bin/clang.exe"
compilerInclude = f"{compilerFolder}/include"
compilerLib = f"{compilerFolder}/lib"


def GetClangLibDir():
    """Return the clang runtime library directory (e.g., lib/clang/22/lib/windows/)."""
    clangLibRoot = f"{compilerFolder}/lib/clang"
    if os.path.isdir(clangLibRoot):
        versions = sorted([d for d in os.listdir(clangLibRoot) if d[0].isdigit()])
        latest = versions[-1] if versions else "22"
    else:
        latest = "22"
    return f"{clangLibRoot}/{latest}/lib/windows"


llvmFolder = f"{location}/vendor/LLVM"

core = f"{location}/Projects/Core"
threading = f"{location}/Projects/Threading"
profiling = f"{location}/Projects/Profiling"
codeGen = f"{location}/Projects/Reflection/CodeGen"
reflection = f"{location}/Projects/Reflection/Reflection"
reflectionHeaderTool = f"{location}/Projects/Reflection/HeaderTool"
staticReflection = f"{location}/Projects/Reflection/StaticReflection"
objectDescription = f"{location}/Projects/ObjectDescription"
changeNotification = f"{location}/Projects/ChangeNotification"
unitTesting = f"{location}/Projects/UnitTesting"
coreTesting = f"{location}/Projects/CoreTesting"
reflectionTesting = f"{location}/Projects/ReflectionTesting"


class BuildType(Enum):
	EXECUTABLE = 0
	STATIC_LIBRARY = 1
	DYNAMIC_LIBRARY = 2
	UNBUILT_LIBRARY = 3

class BuildStage(Enum):
	EARLY = 0
	NORMAL = 1