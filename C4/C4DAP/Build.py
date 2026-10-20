# C4DAP build script - the debug adapter executable: a stdio loop over C4DAPLib
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "C4"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core", "ObjectDescription", "C4Protocol", "C4DAPLib"]
    p.links = ["dbghelp.lib"]
    p.buildType = Config.BuildType.EXECUTABLE
    p.genReflection = False
    p.buildStage = Config.BuildStage.NORMAL
    return p
