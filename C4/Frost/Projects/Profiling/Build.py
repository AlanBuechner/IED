# Profiling build script
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager
import Config

# Tracy client source is compiled directly as part of this project (tracy/ folder contains only upstream Tracy)
externalSources = [
    "tracy/public/TracyClient.cpp",
]

includes = [
    "src",
    os.path.join(projDir, "tracy/public").replace("\\", "/"),
]

defines = [
    "TRACY_ENABLE",
]

def GetProject():
    p = ProjectManager.Project()
    p.group = "Frost.Profiling"
    p.externalSources = externalSources
    p.includes = includes
    p.defines = defines
    p.moduleLinks = ["Core"]
    p.buildStage = Config.BuildStage.EARLY
    p.genReflection = False
    return p
