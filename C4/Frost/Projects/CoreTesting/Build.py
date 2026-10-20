# CoreTesting build script
import os
import inspect
import subprocess
projDir = os.path.dirname(inspect.getfile(lambda: None)).replace("\\", "/")
projName = os.path.basename(projDir)

import ProjectManager
import Config

def GetProject():
	p = ProjectManager.Project()
	p.group = "Frost.CoreTesting"
	p.includes = [f"{projDir}/src", f"{projDir}/src/glm"]
	p.moduleLinks = ["Core", "Threading", "UnitTesting", "ObjectDescription"]
	p.buildType = Config.BuildType.EXECUTABLE
	p.buildStage = Config.BuildStage.EARLY
	p.genReflection = True
	p.isUnitTestProject = True
	p.compilerArgs = []
	return p