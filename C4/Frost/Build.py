# main build file

import os
import sys

sys.dont_write_bytecode = True  # never drop __pycache__ into the caller's directory
import shutil
import inspect
import argparse
import time
import subprocess
from contextlib import chdir

# set our working directory to the build tools root directory
rootDir = os.path.dirname(inspect.getfile(lambda: None))
os.chdir(rootDir)
sys.path.append(rootDir)
sys.path.append(os.getcwd())

import Config
import Builder.ProjectBuilder as ProjectBuilder
from Builder.BuildInfo import SaveBuildInfo, SaveChromeTrace, GetProjectInfo
import ProjectManager as ProjectManager
import Utils.Utils as Utils
import Utils.VSGenerator as VSGenerator

# --------------------- Setup --------------------- #
# The LLVM release (clang, lld and the LLVM headers and libraries the C4 compiler links) is installed into
# vendor/Compiler by setup.bat at the root of the repository.
if not os.path.isfile(Config.compiler):
	print(f"error: no LLVM toolchain at {Config.compilerFolder}; run setup.bat at the root of the repository")
	sys.exit(1)

# --------------------- Define args --------------------- #
parser = argparse.ArgumentParser()
parser.add_argument('-gs', action='store_true', help='generate project files') # generate project files
parser.add_argument('-r', action='store_true', help='run the project') # run
parser.add_argument('-b', action='store_true', help='build the project') # build
parser.add_argument('-fb', action='store_true', help='build the full project') # full build
parser.add_argument('-rebuild', action='store_true', help='rebuild the project') # rebuild the project
parser.add_argument('-clean', action='store_true', help='clean the project') # clean build files
parser.add_argument('-p', type=str, help='the project to build') # project
parser.add_argument('-c', type=str, help='the configuration (Release, Debug, Dist)') # configuration
parser.add_argument('-a', type=str, help='the architecture (x64)') # architecture
parser.add_argument('-s', type=str, help='the system (windows)') # system
parser.add_argument('-ci', action='store_true', help='print compile time information per file and folder after build') # compile info
parser.add_argument('-test', action='store_true', help='run unit tests for the specified project') # run tests
parser.add_argument('-benchmark', action='store_true', help='run benchmarks for the specified project') # run benchmarks
parser.add_argument('-testall', action='store_true', help='discover and run all unit test projects') # run all tests
parser.add_argument('--filter', type=str, default=None, help='wildcard filter for tests (e.g. "Lexer.*")')
parser.add_argument('--benchmark-filter', type=str, default=None, help='wildcard filter for benchmarks')
args = parser.parse_args()

# --------------------- Check args --------------------- #
shouldBuild = args.b or args.rebuild or args.fb
shouldRun = args.r
fullBuild = args.fb
clean = args.clean or args.rebuild
shouldTest = args.test
shouldBenchmark = args.benchmark
shouldTestAll = args.testall

# Test flags implicitly trigger a build if no explicit build flag is provided.
if(shouldTest or shouldBenchmark):
	shouldBuild = True
	fullBuild = True
if(shouldTestAll):
	shouldBuild = True
	fullBuild = True

# A project must be specified for any operation that does something.
anyAction = args.gs or shouldBuild or shouldRun or clean or shouldTest or shouldBenchmark or shouldTestAll
if anyAction and args.p is None:
	print("Error: A project path is required. Specify it with -p <path-to-project>.")
	sys.exit(1)

if(fullBuild):
	shouldBuild = True

def checkArg(arg, options, alias = None):
	for option in options:
		if(arg == option):
			return option
	if alias != None:
		if arg in alias.keys():
			return alias[arg]
	return ""

# get the configuration
if(args.c != None):
	c = checkArg(args.c, Config.configurations)
	if(c == ""):
		print(f"configuration {args.c} is not defined")
		sys.exit(1)
	else:
		Config.configuration = c

# get the architecture
if(args.a != None):
	a = checkArg(args.a, Config.architectures, Config.arcAliases)
	if(a == ""):
		print(f"architecture {args.a} is not defined")
		sys.exit(1)
	else:
		Config.architecture = a

# get the system
if(args.s != None):
	s = checkArg(args.s, Config.systems)
	if(s == ""):
		print(f"system {args.s} is not defined")
		sys.exit(1)
	else:
		Config.system = s

# get the project
if(args.p != None):
	Config.projectLocation = args.p.replace("\\", "/") # get project location
	# remove trailing "/"
	if(Config.projectLocation[-1] == "/"):
		Config.projectLocation = Config.projectLocation[:-1]

	# Resolve relative paths: try rootDir first, then rootDir's parent directory.
	# Absolute paths pass through unchanged.
	if not os.path.isabs(Config.projectLocation):
		rootParent = os.path.dirname(rootDir).replace("\\", "/")
		candidate = os.path.join(rootDir, Config.projectLocation).replace("\\", "/")
		if os.path.isdir(candidate):
			Config.projectLocation = candidate
		else:
			candidate = os.path.join(rootParent, Config.projectLocation).replace("\\", "/")
			if os.path.isdir(candidate):
				Config.projectLocation = candidate
			else:
				# Fall back to absolute resolution from cwd (rootDir)
				Config.projectLocation = os.path.abspath(Config.projectLocation).replace("\\", "/")
	else:
		Config.projectLocation = Config.projectLocation.replace("\\", "/")

if(args.ci):
	Config.printCompileInfo = True


# --------------------- Load Project Scripts --------------------- #
projects = [
	Config.core,
	Config.threading,
	Config.profiling,
	Config.codeGen,
	Config.reflection,
	Config.reflectionHeaderTool,
	Config.staticReflection,
	Config.objectDescription,
	Config.changeNotification,
	Config.unitTesting,
		Config.coreTesting,
	Config.reflectionTesting,
	Config.projectLocation,
]
ProjectManager.LoadProjects(projects)

for proj in ProjectManager.projects.values():
	proj.SetupProject()

# --------------------- Check for Cyclic Dependencies --------------------- #
if not ProjectManager.CheckCyclicDependencies():
	sys.exit(1)

# --------------------- Generate Project Files --------------------- #
if(args.gs):
	# Build project info list for the direct VS generator
	projects_info = []
	rootProj = ProjectManager.GetProject(Config.projectLocation)
	for projName, proj in ProjectManager.projects.items():
		is_root = (proj is rootProj)
		if is_root:
			build_tool_path = "Build.bat"
			project_dir_arg = Config.projectLocation
		else:
			build_tool_path = f"{Config.location}/scripts/Build.py"
			project_dir_arg = projName

		# Collect source and header files relative to project folder
		source_files = []
		for src in proj.CollectSourceFiles():
			rel = os.path.relpath(src, proj.folder).replace("\\", "/")
			source_files.append(rel)

		header_files = []
		for hdr in proj.CollectHeaderFiles():
			rel = os.path.relpath(hdr, proj.folder).replace("\\", "/")
			header_files.append(rel)

		# Collect all includes (dependency chain)
		include_paths = proj.CollectIncludes()

		# Collect VS defines (preprocessor definitions for IntelliSense)
		defines = proj.GetVSDefines()

		# Compute vcxproj path relative to solution directory (root project folder)
		vcxproj_abs = os.path.join(proj.folder, f"{proj.projectName}.vcxproj").replace("\\", "/")
		vcxproj_rel = os.path.relpath(vcxproj_abs, rootProj.folder).replace("\\", "/")

		executable_paths = None
		if proj.buildType == Config.BuildType.EXECUTABLE:
			ext = [".exe", ".lib", ".dll"][proj.buildType.value]
			buildName = proj.projectName if proj.projectName else os.path.basename(proj.folder)
			root = Config.projectLocation if Config.projectLocation else Config.location
			executable_paths = {}
			for cfg in Config.configurations:
				binDir = os.path.join(root, Config.binDir.format(config=cfg, system=Config.system, arc=Config.architecture, projName=buildName)).replace("\\", "/")
				executable_paths[cfg] = binDir + "/" + buildName + ext

		projects_info.append({
			"name": proj.projectName,
			"folder": proj.folder,
			"group": proj.group if proj.group else None,
			"vcxproj_rel_path": vcxproj_rel,
			"build_tool_path": build_tool_path,
			"project_dir_arg": project_dir_arg,
			"is_root_project": is_root,
			"defines": defines,
			"include_paths": include_paths,
			"source_files": source_files,
			"header_files": header_files,
			"executable_paths": executable_paths,
		})

	gameName = os.path.basename(Config.projectLocation)
	VSGenerator.GenerateAllVSFiles(
		solution_name=gameName,
		solution_dir=rootProj.folder,
		projects_info=projects_info,
		configurations=Config.configurations,
		architecture=Config.architecture,
	)


# --------------------- Clean Projects --------------------- #
def CleanProject(proj: ProjectManager.Project):
	print(f"Cleaning project {proj.projectName}")
	shutil.rmtree(proj.GetIntDir(), ignore_errors=True)
	shutil.rmtree(proj.GetGeneratedFolder(), ignore_errors=True)

if(clean):
	for proj in ProjectManager.projects.values():
		CleanProject(proj)



# --------------------- Validate Target Project --------------------- #
def ValidateTargetProject(proj: ProjectManager.Project):
	"""Validate that a directly targeted project is a root executable."""
	if proj.buildType != Config.BuildType.EXECUTABLE:
		print(f"Error: '{proj.projectName}' is not an executable project.")
		print("Non-executable projects can only be built as a result of building an executable project that includes it.")
		sys.exit(1)
	if not proj.isRootProject:
		print(f"Error: '{proj.projectName}' is not marked as a root project.")
		print("Only root projects can be directly targeted for build or run. Set 'isRootProject = True' in the project's Build.py script.")
		sys.exit(1)

rootProj = ProjectManager.GetProject(Config.projectLocation)
if rootProj is None:
	print(f"Error: Could not find a project at '{Config.projectLocation}'.")
	print("The specified path must contain a valid Build.py script.")
	sys.exit(1)
ValidateTargetProject(rootProj)

# --------------------- Build Projects --------------------- #
_buildFailed = False

def BuildProject(proj: ProjectManager.Project, buildStage):

	global _buildFailed

	# Fail fast: once any project has failed, stop building everything else so nothing links
	# against a broken or stale output (e.g. an executable linked from a half-built static lib).
	if(_buildFailed):
		return 1

	if(proj.buildStage != buildStage):
		return None

	# A project is only built once per invocation; dependents of a failure are skipped above.
	if (proj.built == True):
		return None
	proj.built = True

	# build projects dependencies if full build is enabled
	if(fullBuild):
		for modName in proj.moduleLinks:
			mod = ProjectManager.GetProject(modName)
			code = BuildProject(mod, buildStage)
			if(code == 1):
				print(f"Build Failed: dependency '{mod.projectName}' failed; skipping dependent projects.")
				return 1

	# build project
	buildMsg = f"=============== Building: {proj.projectName},   {Config.configuration} {Config.architecture} ==============="
	print("_" * 120)
	print(buildMsg)
	startTime = time.time()
	builder = ProjectBuilder.ProjectEnvironment()
	builder.InitFromProject(proj)
	errorCode = builder.Build()
	endTime = time.time()

	# record total build time in build info
	projInfo = GetProjectInfo(proj.projectName)
	if projInfo is not None:
		projInfo.totalBuildTime = endTime - startTime
		projInfo.buildStartTime = startTime
		projInfo.buildEndTime = endTime

	if(errorCode != 0):
		print("Build Failed")
		_buildFailed = True
	print(f"========== finished building {proj.projectName} in {endTime - startTime:.2f}s ==========")
	print("_" * 120)
	print("\n")
	return errorCode if errorCode != 0 else None

if(shouldBuild):
	# During a normal build (without test flags), skip unit test projects.
	# They are only compiled when explicitly requested via -test, -benchmark, or -testall.
	# Each early project builds after the ones it links, so a clean tree builds with -b too.
	buildable = [proj for proj in ProjectManager.projects.values() if not proj.isUnitTestProject]
	for proj in ProjectManager.InBuildOrder(buildable, Config.BuildStage.EARLY):
		BuildProject(proj, Config.BuildStage.EARLY)

	BuildProject(rootProj, Config.BuildStage.NORMAL)

	# The root's tools (toolPaths) build after it, so a tool that links the root's libraries never links a
	# stale one. EARLY-stage tools were already built by the loop above.
	for toolPath in rootProj.toolPaths:
		tool = ProjectManager.GetProject(toolPath)
		if tool is not None:
			BuildProject(tool, tool.buildStage)

	if(_buildFailed):
		print("BUILD FAILED: one or more projects failed to build. No further steps were taken.")
		sys.exit(1)

	# save build info after all projects have been built
	outputDir = rootProj.GetIntDir()

	buildInfoPath = os.path.join(outputDir, "build-info.json")
	SaveBuildInfo(buildInfoPath)
	print(f"Build info saved to {buildInfoPath}")

	# save chrome tracing JSON for visualising build timings in chrome://tracing
	chromeTracePath = os.path.join(outputDir, "build-trace.json")
	SaveChromeTrace(chromeTracePath)
	print(f"Chrome trace saved to {chromeTracePath}")

# --------------------- Run Project --------------------- #
def RunProject(proj):
	output = proj.GetOutput()
	cwd = Config.project
	with chdir(cwd):
		#os.startfile(output)
		#os.execv(output, [output])
		subprocess.run([output], cwd=cwd, text=True)

if(shouldRun):
	RunProject(rootProj)


# --------------------- Test Execution --------------------- #
import Builder.TestRunner as TestRunner

def FindTestProjectsForRoot(rootProj: ProjectManager.Project) -> list[ProjectManager.Project]:
	"""Return all unit test projects known to the project manager.
	Unit test projects are only compiled when explicitly requested via -test, -benchmark, or -testall."""
	test_projects: list[ProjectManager.Project] = []
	for proj in ProjectManager.projects.values():
		if proj.isUnitTestProject:
			test_projects.append(proj)
	test_projects.sort(key=lambda p: p.projectName)
	return test_projects

if(shouldTest or shouldBenchmark):
	test_projects = FindTestProjectsForRoot(rootProj)
	if not test_projects:
		print(f"No test projects found for '{rootProj.projectName}'.")
		sys.exit(0)
	# Build each test project before running it (they may not be link dependencies).
	# Use each project's own buildStage so EARLY-stage test projects aren't skipped.
	for proj in test_projects:
		BuildProject(proj, proj.buildStage)
	if(_buildFailed):
		print("BUILD FAILED: skipping test execution (refusing to run stale test binaries).")
		sys.exit(1)
	# Build extra CLI args to forward to the test executable (--filter, --benchmark-filter).
	extra_args = []
	if args.filter is not None:
		extra_args.append(f"-filter={args.filter}")
	if args.benchmark_filter is not None:
		extra_args.append(f"-benchmark-filter={args.benchmark_filter}")
	aggregate_code = 0
	for proj in test_projects:
		print()
		code = TestRunner.RunTestProject(proj, extra_args)
		if code != 0:
			aggregate_code = code
	sys.exit(aggregate_code)

if(shouldTestAll):
	test_projects = FindTestProjectsForRoot(rootProj)
	if not test_projects:
		print(f"No test projects found for '{rootProj.projectName}'.")
		sys.exit(0)
	# Build each test project before running it (they may not be link dependencies).
	# Use each project's own buildStage so EARLY-stage test projects aren't skipped.
	for proj in test_projects:
		BuildProject(proj, proj.buildStage)
	# Build extra CLI args to forward to the test executable (--filter, --benchmark-filter).
	extra_args = []
	if args.filter is not None:
		extra_args.append(f"-filter={args.filter}")
	if args.benchmark_filter is not None:
		extra_args.append(f"-benchmark-filter={args.benchmark_filter}")
	aggregate_code = 0
	for proj in test_projects:
		print()
		code = TestRunner.RunTestProject(proj, extra_args)
		if code != 0:
			aggregate_code = code
	sys.exit(aggregate_code)


