# ChangeNotification build script
import os
import inspect
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager
import Config

def GetProject():
    p = ProjectManager.Project()
    p.group = "Frost.ChangeNotification"
    p.includes = [f"{projDir}/src"]
    p.moduleLinks = ["Core"]
    p.buildStage = Config.BuildStage.EARLY
    p.genReflection = False
    return p
