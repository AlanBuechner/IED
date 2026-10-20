# C4DAPLib build script - the debug adapter (DAP requests, events and the debugger over the Win32 debug API and
# DbgHelp) as a library, so the C4DAP executable and C4Testing link the same code
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "C4"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core", "ObjectDescription", "C4Protocol"]
    p.links = ["dbghelp.lib"]
    p.defines = ["NOMINMAX", "WIN32_LEAN_AND_MEAN"]
    p.buildType = Config.BuildType.STATIC_LIBRARY
    p.genReflection = False
    p.buildStage = Config.BuildStage.EARLY
    return p
