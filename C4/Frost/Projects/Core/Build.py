# Core build script
import os
import inspect
import Config
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager

includes = []

modulePaths = [
	"yaml",
]

moduleLinks = [
	"yaml",
]

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.Core"
	p.includes = includes
	p.modulePaths = modulePaths
	p.moduleLinks = moduleLinks
	p.buildStage = Config.BuildStage.EARLY
	p.genReflection = False
	return p
