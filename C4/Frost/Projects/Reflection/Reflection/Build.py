# Reflection build script

import os
import inspect
import ProjectManager
import Config

projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")

moduleLinks = [
	"Core",
]

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.Reflection"
	p.moduleLinks = moduleLinks
	p.buildStage = Config.BuildStage.EARLY
	p.genReflection = False
	return p
