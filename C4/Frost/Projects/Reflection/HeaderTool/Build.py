# Reflection Header Tool build script

import ProjectManager
import Config
import Utils.Utils as Utils
import os
from pathlib import Path

externIncludes = [
	Config.compilerInclude,
]

moduleLinks = [
	"StaticReflection",
]

links = [
	"ntdll.lib",
	"version.lib",
]
for file in os.listdir(Config.compilerLib):
	if Path(file).suffix == ".lib":
		links.append(file)



def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.Reflection"
	p.externIncludes = externIncludes
	p.moduleLinks = moduleLinks
	p.links = links
	p.buildType = Config.BuildType.EXECUTABLE
	p.genReflection = False
	p.buildStage = Config.BuildStage.EARLY
	return p
