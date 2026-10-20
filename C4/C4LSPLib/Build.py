# C4LSPLib build script - the language server (protocol, JSON, session, features) as a library, so the
# C4LSP executable and C4Testing link the same code
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "C4"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core", "C4Lib", "ObjectDescription", "C4Protocol"]
    p.buildType = Config.BuildType.STATIC_LIBRARY
    p.genReflection = False
    p.buildStage = Config.BuildStage.EARLY
    return p
