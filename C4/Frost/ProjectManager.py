from Utils.Utils import *

import os
import importlib.util
import Config

def SearchForModulePath(modulePath, folder):
    paths = [
        os.path.join(Config.location, "modules", modulePath), # check Frost root module folder
        os.path.join(Config.location, modulePath), # check Frost root folder
        os.path.join(folder, "modules", modulePath), # check project module folder
        os.path.join(folder, modulePath), # check relative to project folder
    ]
    for path in paths:
        normalized = os.path.normpath(path).replace("\\", "/")
        if os.path.exists(normalized):
            return normalized
    print(f"Failed to find module {modulePath}")
    return None

class Project:
	def __init__(self):
		self.projectName: str = ""
		self.folder: str = ""
		self.script = None
		self.built = False

		self.group: str = ""
		self.externalSources: list[str] = []
		self.includes: list[str] = []
		self.sysIncludes: list[str] = []
		self.externIncludes: list[str] = []
		self.modulePaths: list[str] = []
		self.toolPaths: list[str] = [] # executables of the workspace built with this root; they contribute no includes or links
		self.moduleLinks: list[str] = []
		self.links: list[str] = []
		self.defines: list[str] = []
		self.globalDefines: list[str] = []
		self.vsDefines: list[str] = []
		self.compilerArgs: list[str] = []
		self.linkerArgs: list[str] = []
		self.copies: list[any] = []
		self.selfCopies: list[str] = []
		self.buildType = Config.BuildType.STATIC_LIBRARY
		self.genReflection = True
		self.buildStage = Config.BuildStage.NORMAL
		self.isRootProject = False
		self.isUnitTestProject = False
		self.customCodeGenTool: str = ""

	def Reload(self):
		# load project scripts
		spec=importlib.util.spec_from_file_location(self.projectName, self.GetScriptFile())
		self.script = importlib.util.module_from_spec(spec)
		spec.loader.exec_module(self.script)

		#load project
		if(hasattr(self.script, "GetProject")):
			loadedProj: Project = self.script.GetProject()
			self.projectName = loadedProj.projectName or self.projectName  # the folder's name unless the script names it
			self.group = loadedProj.group
			self.externalSources = loadedProj.externalSources
			self.includes = loadedProj.includes
			self.sysIncludes = loadedProj.sysIncludes
			self.externIncludes = loadedProj.externIncludes
			self.modulePaths = loadedProj.modulePaths
			self.toolPaths = loadedProj.toolPaths
			self.moduleLinks = loadedProj.moduleLinks
			self.links = loadedProj.links
			self.defines = loadedProj.defines
			self.globalDefines = loadedProj.globalDefines
			self.vsDefines = loadedProj.vsDefines
			self.compilerArgs = loadedProj.compilerArgs
			self.linkerArgs = loadedProj.linkerArgs
			self.copies = loadedProj.copies
			self.selfCopies = loadedProj.selfCopies
			self.buildType = loadedProj.buildType
			self.genReflection = loadedProj.genReflection
			self.buildStage = loadedProj.buildStage
			self.isUnitTestProject = loadedProj.isUnitTestProject
			self.isRootProject = loadedProj.isRootProject and not loadedProj.isUnitTestProject # unit test projects can not be set a root project
			self.customCodeGenTool = loadedProj.customCodeGenTool

			if(loadedProj.isRootProject and loadedProj.isUnitTestProject):
				print(f"warning {loadedProj.projectName}: unit test project can not be set as root project. please build and run tests through a valid root project")

		# add defaults
		self.includes.extend([
			self.GetSourceFolder()
		])

		self.defines.extend([
			Config.configuration.upper(),
			"_UNICODE",
			"UNICODE",
			"_CRT_SECURE_NO_WARNINGS",
			f"PROJECT_NAME={self.projectName}",
		])

		Config.globalDefines.extend(self.globalDefines)

		# make includes absolute
		for i, inc in enumerate(self.includes):
			if not os.path.isabs(inc):
				self.includes[i] = os.path.join(self.folder, inc).replace("\\", "/")

		for i, copy in enumerate(self.selfCopies):
			src = self.GetOutput()
			folder = ""
			file = ""
			if(os.path.isfile(copy)):
				folder = os.path.dirname(copy)
				file = os.path.basename(copy)
			elif(os.path.isdir(copy)):
				folder = copy
				file = os.path.basename(src)
			self.selfCopies[i] = os.path.join(folder, file)

		if (self.genReflection == True):
			self.moduleLinks.append("Reflection")

	def SetupProject(self):
		if (hasattr(self.script, "DownloadDependencies")):
			self.script.DownloadDependencies()

	def GetScriptFile(self):
		return os.path.join(self.folder, "Build.py").replace("\\", "/")
	
	def GetSourceFolder(self):
		return os.path.join(self.folder, "src").replace("\\", "/")
	
	def GetModuleFolder(self):
		return os.path.join(self.folder, "modules").replace("\\", "/")
	
	def GetEmbeddedFolder(self):
		return os.path.join(self.folder, "embedded").replace("\\", "/")
	
	def GetGeneratedFolder(self):
		return os.path.join(self.folder, "generated").replace("\\", "/")
	
	def CollectAllModules(self):
		"""
		Collects list of all modules recursively including this one,
		traversing both moduleLinks and resolved modulePaths.
		"""
		modules: list[Project] = []
		modulesQueue: list[str] = [self.projectName]
		i: int = 0
		while i < len(modulesQueue):
			mod = GetProject(modulesQueue[i]) # get module project
			if mod in modules: # continue if module was already added
				i += 1
				continue
			modulesQueue.extend(mod.moduleLinks) # enqueue linked modules
			# Also enqueue resolved modulePaths (sub-projects of this project).
			for mp in mod.modulePaths:
				if mp and os.path.isabs(mp):
					mpProj = GetProject(mp)
					if mpProj is not None:
						modulesQueue.append(mpProj.projectName)
			modules.append(mod)
			i += 1
		return modules

	def CollectCustomCodeGenTools(self) -> "list[Project]":
		"""
		Walk the transitive dependency tree and return all projects that declare
		a custom code generation tool via ``customCodeGenTool``.

		Returns a deduplicated list in dependency-order (root first, then deeper
		dependents).  Only projects whose ``customCodeGenTool`` is non-empty are
		included.
		"""
		tools: "list[Project]" = []
		for mod in self.CollectAllModules():
			if mod.customCodeGenTool and mod not in tools:
				tools.append(mod)
		return tools

	def CollectIncludes(self) -> list[str]:
		modules: list[Project] = self.CollectAllModules()
		includes: list[str] = []
		for mod in modules:
			includes.extend(mod.includes)
			includes.extend(mod.externIncludes)
		return includes
	
	def GetSysIncludes(self) -> list[str]:
		includes = self.sysIncludes
		includes.extend(GetSysIncludes())
		return includes
	
	def CollectSourceFiles(self) -> list[str]:
		sources = CollectFilesFiltered(self.GetSourceFolder(), [".c", ".cpp"])
		sources.extend(ResolveFiles(self.externalSources, self.folder))
		return sources

	def CollectHeaderFiles(self) -> list[str]:
		return CollectFilesFiltered(self.GetSourceFolder(), [".h", ".hpp"])
	
	def CollectAllFiles(self) -> list[str]:
		files: list[str] = CollectFiles(self.GetSourceFolder())
		for inc in self.includes:
			files.extend(CollectFiles(inc))
		return files
	
	def CollectLinks(self) -> list[str]:
		links: list[str] = self.links
		for modName in self.moduleLinks:
			mod = GetProject(modName)
			if mod.buildType == Config.BuildType.STATIC_LIBRARY:
				links.append(mod.GetOutput())
		return links
	
	def GetVSDefines(self) -> list[str]:
		defines = self.defines
		defines.extend(self.vsDefines)
		return defines
	
	def GetBinDir(self) -> str:
		return GetBinDir(self.projectName)
	
	def GetIntDir(self) -> str:
		return GetIntDir(self.projectName)
	
	def GetOutput(self) -> str:
		ext = [".exe", ".lib", ".dll"][self.buildType.value]
		buildName = self.projectName
		if(buildName == ""):
			buildName = os.path.basename(self.folder)
		return self.GetBinDir() + "/" + buildName + ext
	
	def GetLastBuildTime(self) -> float:
		out = self.GetOutput()
		if(not os.path.exists(out)):
			return 0.0
		return os.path.getmtime(out)

projects: dict[str, Project] = {}

def InBuildOrder(projectList: "list[Project]", stage) -> "list[Project]":
	"""The projects of `stage` in `projectList`, each after the projects of that stage it links (moduleLinks), so a
	build of all of them never links a library that is not built yet."""
	ordered: "list[Project]" = []
	visited: set[str] = set()
	def Visit(proj: Project):
		if proj is None or proj.buildStage != stage or proj.projectName in visited:
			return
		visited.add(proj.projectName)
		for name in proj.moduleLinks:
			Visit(FindProject(name))
		ordered.append(proj)
	for proj in projectList:
		Visit(proj)
	return ordered

def FindProject(name: str) -> "Project | None":
	"""The loaded project called `name`, or None."""
	return next((proj for proj in projects.values() if proj.projectName == name), None)

# --------------------- Cycle Detection --------------------- #

def _FindDependencyCycle() -> list[str] | None:
    """Return the cycle path if a cyclic dependency exists among moduleLinks, else None.

    Uses DFS with three colours: WHITE (unvisited), GRAY (on current stack), BLACK (finished).
    A back-edge to a GRAY node means we found a cycle.
    Keys are project names since moduleLinks store short names, not full paths.
    """
    WHITE, GRAY, BLACK = 0, 1, 2
    colour: dict[str, int] = {proj.projectName: WHITE for proj in projects.values()}
    parent: dict[str, str | None] = {name: None for name in colour}

    def dfs(name: str) -> list[str] | None:
        colour[name] = GRAY
        proj = GetProject(name)
        if proj is not None:
            for depName in proj.moduleLinks:
                # Only consider dependencies that are known loaded projects.
                if depName not in colour:
                    continue
                if colour[depName] == GRAY:
                    # Reconstruct the cycle path.
                    cycle = [depName, name]
                    node = name
                    while node != depName:
                        node = parent.get(node)
                        if node is None:
                            break
                        cycle.append(node)
                    cycle.reverse()
                    return cycle
                if colour[depName] == WHITE:
                    parent[depName] = name
                    result = dfs(depName)
                    if result is not None:
                        return result
        colour[name] = BLACK
        return None

    for name in colour:
        if colour[name] == WHITE:
            result = dfs(name)
            if result is not None:
                return result
    return None


def CheckCyclicDependencies() -> bool:
    """Check all loaded projects for cyclic moduleLinks dependencies.

    Returns True if no cycles were found (i.e. the dependency graph is valid).
    Returns False and prints an error if a cycle was detected.
    """
    cycle = _FindDependencyCycle()
    if cycle is None:
        return True

    # Build the list of affected projects' Build.py files for actionable guidance.
    affectedProjects: dict[str, Project] = {}
    for projName in cycle:
        proj = GetProject(projName)
        if proj is not None:
            affectedProjects[projName] = proj

    print("_" * 80)
    print("Error: Cyclic dependency detected among projects!")
    print("_" * 80)
    print("")
    # Format the cycle as a readable chain.
    chain = " -> ".join(cycle)
    print(f"Cycle: {chain}")
    print("")
    print("Affected projects and their Build.py scripts:")
    for projName in cycle:
        proj = affectedProjects.get(projName)
        if proj is not None:
            scriptPath = proj.GetScriptFile()
            links = ", ".join(f'"{dep}"' for dep in proj.moduleLinks if dep in affectedProjects) or "(none in cycle)"
            print(f"  {projName}:")
            print(f"    Script:      {scriptPath}")
            print(f"    Cycle Links: {links}")
    print("")
    print("A project cannot depend on itself through a chain of moduleLinks.")
    print("Check the Build.py scripts listed above and remove the cycle.")
    print("_" * 80)
    return False


# get project using name or path
def GetProject(projName):
	for proj in projects.values():
		if(proj.folder.endswith(projName)):
			return proj

	# fallback: match by project name (handles cases where -p uses a short name)
	baseName = os.path.basename(projName)
	for proj in projects.values():
		if proj.projectName == baseName:
			return proj

	print(f"project {projName} is not defined")
	return None

def ProjectExists(projName):
	for proj in projects.values():
		if(proj.folder.endswith(projName)):
			return True
	return False

def LoadProject(projPath: str):
	# check if path is empty
	if not projPath:
		return

	# check if project already exists by full path
	if ProjectExists(projPath):
		return

	# also check by project name to avoid duplicates from different path resolutions
	projectName = os.path.basename(projPath)
	for proj in projects.values():
		if proj.projectName == projectName:
			return

	print(f"Loading Project: {projPath}")

	p: Project = Project()
	p.projectName = os.path.basename(projPath)
	p.folder = projPath.replace("\\", "/")

	p.Reload()

	# load modules
	for i, modulePath in enumerate(p.modulePaths):
		p.modulePaths[i] = SearchForModulePath(modulePath, p.folder)
		LoadProject(p.modulePaths[i])
	for i, toolPath in enumerate(p.toolPaths):
		p.toolPaths[i] = SearchForModulePath(toolPath, p.folder)
		LoadProject(p.toolPaths[i])

	projects[projPath] = p


def LoadProjects(projectList):
	# load projects
	for proj in projectList:
		LoadProject(proj)