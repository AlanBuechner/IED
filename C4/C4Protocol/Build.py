# C4Protocol build script - JSON values, Content-Length framing and the stdio channel the language server and
# the debug adapter share
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "C4"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core", "ObjectDescription"]
    p.buildType = Config.BuildType.STATIC_LIBRARY
    p.genReflection = False
    p.buildStage = Config.BuildStage.EARLY
    return p
