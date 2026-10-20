# ReflectionTesting build script
import os
import inspect
import subprocess
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager
import Config

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.ReflectionTesting"
	p.includes = [f"{projDir}/src"]
	p.moduleLinks = ["Core", "UnitTesting", "Reflection", "ObjectDescription", "StaticReflection", "ChangeNotification"]
	p.buildType = Config.BuildType.EXECUTABLE
	p.buildStage = Config.BuildStage.EARLY
	p.genReflection = True
	p.isUnitTestProject = True
	p.compilerArgs = []
	return p
