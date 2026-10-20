# yaml build script. The sources are the yaml-cpp submodule (yaml-cpp/), compiled as part of this project.

import ProjectManager as ProjectManager
import Config

externalSources = [
	"yaml-cpp/src/**/*.cpp",
]

includes = [
	"yaml-cpp/include",
]

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.Core"
	p.externalSources = externalSources
	p.includes = includes
	p.buildStage = Config.BuildStage.EARLY
	p.genReflection = False
	return p
