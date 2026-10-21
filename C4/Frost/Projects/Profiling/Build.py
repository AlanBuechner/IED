# Profiling build script: the IED Trace Protocol client (src/Profiling/Itp.h, Itp.cpp; IED docs/16) and the
# Performance.h macros over it. Programs built with ENABLE_PROFILING stream zones to the IED profiler when run with
# IED_PROFILER set.
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager
import Config

includes = [
    "src",
]

def GetProject():
    p = ProjectManager.Project()
    p.group = "Frost.Profiling"
    p.includes = includes
    p.moduleLinks = ["Core"]
    p.buildStage = Config.BuildStage.EARLY
    p.genReflection = False
    return p
