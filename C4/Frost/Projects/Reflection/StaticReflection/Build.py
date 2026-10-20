# Static Reflection build script

import ProjectManager
import Config
import Utils.Utils as Utils
import os
from pathlib import Path

moduleLinks = [
	"Core",
	"ObjectDescription",
]

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.Reflection"
	p.moduleLinks = moduleLinks
	p.buildType = Config.BuildType.STATIC_LIBRARY
	p.genReflection = False
	p.buildStage = Config.BuildStage.EARLY
	return p
