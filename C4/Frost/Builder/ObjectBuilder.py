import os
import subprocess
import multiprocessing
import concurrent.futures
import time
from pathlib import Path

from Utils.Windows import *
from Utils.Utils import *
import ProjectManager
import Config

# Exit codes of a tool that crashed rather than reported errors. clang 22.1.0 and HeaderTool (libclang) crash
# intermittently under a parallel build on Windows (a different file each time, never reproducible on a re-run),
# so a crash is retried; an error exit never is.
CRASH_EXIT_CODES = {
	0xC0000005: "access violation",
	0xC00000FD: "stack overflow",
	0xC0000409: "stack buffer overrun",
}
CRASH_RETRIES = 2

def RunRetryingCrashes(args, cwd, log, label, **run_options):
	for attempt in range(CRASH_RETRIES + 1):
		result = subprocess.run(args, cwd=cwd, text=True, **run_options)
		crash = CRASH_EXIT_CODES.get(result.returncode & 0xFFFFFFFF)
		if crash is None or attempt == CRASH_RETRIES:
			return result
		log(f"warning: {os.path.basename(args[0])} crashed ({crash}) on {label}; retrying ({attempt + 1}/{CRASH_RETRIES})")
	return result

def CreateBaseArgs(includes, sysIncludes, defines, useSys = True, sourceFile = None):
	args = []
	# Compile .c files as C to avoid keyword conflicts and implicit cast errors
	if sourceFile is not None and sourceFile.endswith(".c"):
		args.extend(["-std=c11", "-x", "c"])
	else:
		args.extend([f"-std={Config.cppVersion}", "-x", "c++"])
	if(Config.useAsan == True):
		# The prebuilt LLVM libraries are not sanitized: the STL must lay out strings and vectors the way they do.
		args.extend(["-fsanitize=address", "-D_DISABLE_STRING_ANNOTATION", "-D_DISABLE_VECTOR_ANNOTATION"])
	for i in includes:
		args.extend(["-I", i])
	for i in sysIncludes:
		if(useSys):
			args.extend(["-internal-isystem", i])
		else:
			args.extend(["-I", i])
	for d in defines:
		args.append(f"-D{d}")
	return args

def CreateBuildArgs(includes, sysIncludes, defines, compilerArgs, sourceFile = None):
	args = []
	
	args.extend(CreateBaseArgs(includes, sysIncludes, defines, True, sourceFile))

	optimizationLevel = 0
	if(Config.configuration == "Debug"):
		optimizationLevel = 0
	elif(Config.configuration == "Development"):
		optimizationLevel = 3
	elif(Config.configuration == "Dist"):
		optimizationLevel = 3
	args.append(f"-O{optimizationLevel}")
	if(Config.useAsan == True):
		args.append("-fno-sanitize-address-use-odr-indicator")
	args.extend(["-triple", GetTarget()])
	
	args.extend(compilerArgs)
	args.extend([
		#"-mincremental-linker-compatible",
		#"-disable-free",
		#"-clear-ast-before-backend",
		#"-disable-llvm-verifier",
		#"-discard-value-names",
		#"-mrelocation-model", "pic",
		#"-pic-level", "2",
		#"-mframe-pointer=none",
		#"-relaxed-aliasing",
		#"-fmath-errno",
		#"-ffp-contract=on",
		#"-fno-rounding-math",
		#"-complex-range=full",
		#"-mconstructor-aliases",
		#"-funwind-tables=2",
		#"-target-cpu", "x86-64",
		#"-mllvm",
		#"-x86-asm-syntax=intel",
		#"-tune-cpu", "generic",
		#"-D_MT",
		#"--dependent-lib=msvcrt",
		#"--dependent-lib=oldnames",
		#"-stack-protector", "2",
		#"-fdefault-calling-conv=cdecl",
		#"-gno-column-info",
		#"-Wall",
		#"-Wno-error",
		#"-fdeprecated-macro",
		#"-fno-use-cxa-atexit",
		#"-fno-implicit-modules",
		#"-fskip-odr-check-in-gmf",
		#"-fno-caret-diagnostics",
		#"-vectorize-loops",
		#"-vectorize-slp",
		#"-faddrsig",
		#"-ffunction-sections",

		# debug
		"-debug-info-kind=standalone",
		"-gcodeview",

		# limits
		"-ferror-limit", "10",

		# visual studio
		"-fdiagnostics-format", "msvc",
		"-fms-volatile",
		"-fms-extensions",
		"-fms-compatibility",
		f"-fms-compatibility-version={GetFMSVersion()}",

		# exceptions
		"-fcxx-exceptions",
		"-fexceptions",
		"-fexternc-nounwind",

		# suppressions
		"-Wno-unused-comparison",
	])

	return args

class ObjectBuilder:
	def __init__(self):
		self.sourceFile = ""
		self.objFile = ""
		self.treeFile = ""
		self.argsFile = ""
		self.workingDir = ""
		self.intDir = ""
		self.includeDirs = []
		self.sysIncludeDirs = []
		self.defines: list[str] = []
		self.compilerArgs: list[str] = []
		self.reflect = True
		self._needsBuild = False
		self._needsBuildReflection = False
		self._lastBuildTime = 0
		self.buildError = -1
		self._log = ""
		self.compileTime = 0.0
		self.reflectionCollectTime = 0.0
		self.compileStartTime = 0.0
		self.compileEndTime = 0.0
		self.reflectionCollectStartTime = 0.0
		self.reflectionCollectEndTime = 0.0

	def Log(self, text):
		self._log += str(text)

	def LogLine(self, text):
		self._log += str(text) + "\n"

	def EvaluateDependencies(self):
		objExists = os.path.exists(self.objFile)
		treeExists = os.path.exists(self.treeFile)

		if(not objExists or not treeExists):
			self._needsBuild = True
			return

		self._lastBuildTime = os.path.getmtime(self.objFile)
		compilerTime = os.path.getmtime(Config.compiler)

		if(compilerTime > self._lastBuildTime):
			self._needsBuild = True
			return
		
		if(self.reflect):
			headerToolProject = ProjectManager.GetProject(Config.reflectionHeaderTool)
			headerToolTime = os.path.getmtime(headerToolProject.GetOutput())
			self._needsBuildReflection = headerToolTime > self._lastBuildTime

		file = open(self.treeFile)
		s = file.read()
		lines = s.split(".d:")[1].split("\n")
		for line in lines:
			# sanitize path
			if(line.endswith("\\")):
				line = line[0:-1]
			line = line.strip()

			# check the path is not empty
			if(line == ""):
				continue

			# get all the tokens in the line
			tokens = line.split(" ")
			curr = ""
			lastSuccess = False
			for t in tokens:
				lastSuccess = False
				if curr != "":
					curr += " "
				curr += t
				path = curr
				if(not os.path.isabs(path)):
					path = os.path.join(self.workingDir, path)
				if os.path.isfile(path):
					lastTime = os.path.getmtime(path) # get last time file was modified
					curr = "" # reset path
					lastSuccess = True
					# check if this dependency was modified after the last time the obj was built
					if lastTime > self._lastBuildTime:
						self._needsBuild = True
						break
			if lastSuccess == False: # hit the end of the line with unresolved path meaning a file was deleted
				self._needsBuild = True
			if self._needsBuild == True:
				break
		file.close()

	def DiscardOutputs(self):
		# A failed compile may have written its object first (a crash after the write): without it the next build compiles
		# the file again instead of linking what it left (BUG-255).
		for path in (self.objFile, self.treeFile):
			if os.path.exists(path):
				os.remove(path)

	def EvaluateArgs(self, formattedArgs):
		if(not os.path.exists(self.argsFile)):
			self._needsBuild = True
			return
		
		with open(self.argsFile) as f:
			lastArgs = f.read()
			if(lastArgs != formattedArgs):
				self._needsBuild = True
				return

	def Build(self):		
		file = os.path.basename(self.sourceFile)
		args = []

		args.append(Config.compiler)
		args.append("-cc1")
		args.extend(["-emit-obj"])

		args.extend(CreateBuildArgs(self.includeDirs, self.sysIncludeDirs, self.defines, self.compilerArgs, self.sourceFile))

		args.extend([self.sourceFile])
		args.extend(["-o", self.objFile])
		args.extend([
			"-dependency-file", self.treeFile,
			"-MT", self.treeFile,
		])

		formattedArgs = " ".join(args)
		self.EvaluateArgs(formattedArgs)
		self.EvaluateDependencies()

		error = -1
		if(self._needsBuild or self._needsBuildReflection):
			self.LogLine(f"|------------- Building file : {file} -------------|")

		if(self._needsBuild):
			# write args file
			with open(self.argsFile, "w") as f:
				f.write(formattedArgs)

			# run command with timing
			startTime = time.perf_counter()
			self.compileStartTime = time.time()
			result = RunRetryingCrashes(args, self.workingDir, self.LogLine, file, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
			endTime = time.perf_counter()
			self.compileEndTime = time.time()
			self.compileTime = endTime - startTime
			self.Log(result.stdout)
			self.LogLine(f"building completed in {self.compileTime:.4f}s with error code {result.returncode}")
			error = result.returncode

			# A return code of 0 is not proof of success: if the compiler exits cleanly (or is killed)
			# without producing the object file, downstream archives/links would silently reuse stale
			# artifacts. Treat a missing .obj as a hard failure so the build surfaces it.
			if(error == 0 and not os.path.exists(self.objFile)):
				self.LogLine(f"ERROR: compiler reported success but no object file was produced at '{self.objFile}'.")
				error = 1
			if(error != 0):
				self.DiscardOutputs()

		# generate reflection if build was successful
		if(self.reflect and error == 0):
			if(self._needsBuild or self._needsBuildReflection):
				args = []
				headerToolProject = ProjectManager.GetProject(Config.reflectionHeaderTool)
				args.append(headerToolProject.GetOutput())
				intGeneratedFolder = os.path.join(self.intDir, "generated")
				os.makedirs(intGeneratedFolder, exist_ok=True)  # parallel builders create it too
				args.extend(["--generated-folder", intGeneratedFolder])
				args.extend(["--source-folder", os.path.join(self.workingDir, "src")])
				args.append(self.sourceFile)
				args.extend(["--", Config.compiler])
				args.extend(CreateBaseArgs(self.includeDirs, self.sysIncludeDirs, self.defines, False, self.sourceFile))
				args.append(f"-DHEADER_TOOL")
				startTime = time.perf_counter()
				self.reflectionCollectStartTime = time.time()
				result = RunRetryingCrashes(args, self.workingDir, self.LogLine, file, capture_output=True)
				endTime = time.perf_counter()
				self.reflectionCollectEndTime = time.time()
				self.reflectionCollectTime = endTime - startTime
				self.Log(str(result.stdout))
				self.LogLine(f"collecting reflection completed in {self.reflectionCollectTime:.4f}s with error code {result.returncode}")
				# update last modify time on obj
				os.utime(self.objFile)
				# always propagate HeaderTool failure - fail the build if reflection collection fails
				if(result.returncode != 0):
					self.LogLine(f"ERROR: HeaderTool failed with return code {result.returncode}.")
					error = result.returncode
		
		if(self._log != ""):
			print(self._log)
		self.buildError = error
		return error


def BuildObjects(objects, jobs = 0):
	if(jobs == 0):
		jobs = Config.jobs or multiprocessing.cpu_count()
	pool = concurrent.futures.ThreadPoolExecutor(max_workers=jobs)
	futures = []
	for o in objects:
		futures.append(pool.submit(ObjectBuilder.Build, o))
	
	pool.shutdown(wait=True)

	numSuccess = 0
	numFailed = 0
	total = len(objects)
	for f in futures:
		try:
			code = f.result()
		except Exception as e:
			# A worker exception (e.g. an I/O error while launching the compiler) must not crash the
			# build mid-flight and must not be miscounted as up-to-date: it is a failure.
			print(f"ERROR: object build crashed with {type(e).__name__}: {e}")
			numFailed += 1
			continue
		if(code == 0):
			numSuccess+=1
		elif(code != -1): # -1 means "up-to-date, no work done"; anything else is a failure
			numFailed+=1

	print(f"finished building sources : {numSuccess} succeeded : {numFailed} failed : {total - (numSuccess+numFailed)} up-to-date")
	if(numSuccess == 0 and numFailed == 0):
		return -1 # no work done
	return numFailed

# The linker's own record of its arguments, kept beside the objects (Builder/Linker.py).
LINKER_ARGS_NAME = "linker"


def ObjectName(source: str) -> str:
	return os.path.splitext(os.path.basename(source))[0]


def SharedObjectNames(sources: list[str]) -> list[str]:
	"""The object names two or more sources would share (each source's .obj is named after its base name)."""
	seen: dict[str, str] = {}
	clashes: list[str] = []
	for src in sources:
		name = ObjectName(src)
		if name in seen and seen[name] != src:
			clashes.append(f"{seen[name]} and {src} both compile to {name}.obj")
		seen.setdefault(name, src)
	return clashes


def CleanStaleIntermediateFiles(expectedNames: set[str], intDir: str) -> list[str]:
	"""Remove the intermediate files (.obj, .d, .args) of sources that no longer exist: the linker takes every
	.obj in the folder, so an orphan would be linked into the module.

	`expectedNames`: the object name of every source the project still compiles, generated ones included.
	Returns the removed paths."""
	if not os.path.isdir(intDir):
		return []

	removed: list[str] = []
	staleExtensions = {".obj", ".d", ".args"}
	for entry in os.listdir(intDir):
		fullPath = os.path.join(intDir, entry)
		if not os.path.isfile(fullPath):
			continue
		name, ext = os.path.splitext(entry)
		if ext in staleExtensions and name not in expectedNames and name != LINKER_ARGS_NAME:
			os.remove(fullPath)
			removed.append(fullPath)
	return removed


def BuildSources(sources, projDir, intDir, includes, sysIncludes, defines, compilerArgs, reflect):
	buildActions = []

	for file in sources:
		name = ObjectName(file)
		objFile = os.path.join(intDir, name) + ".obj"
		treeFile = os.path.join(intDir, name) + ".d"
		argsFile = os.path.join(intDir, name) + ".args"

		env = ObjectBuilder()
		env.sourceFile = file
		env.objFile = objFile
		env.treeFile = treeFile
		env.argsFile = argsFile
		env.workingDir = projDir
		env.intDir = intDir
		env.includeDirs = includes
		env.sysIncludeDirs = sysIncludes
		env.defines = defines
		env.compilerArgs = compilerArgs
		env.reflect = reflect

		env.defines.extend(Config.globalDefines)

		buildActions.append(env)

	# build objs
	result = BuildObjects(buildActions)

	if(Config.printCompileInfo):
		PrintCompileInfo(buildActions, projDir)

	return (result, buildActions)


def PrintCompileInfo(buildActions: list[ObjectBuilder], projDir: str):
	"""Print compile time information per file and folder."""
	from collections import defaultdict

	# Gather per-folder stats
	folderStats: dict[str, dict] = defaultdict(lambda: {"files": 0, "totalTime": 0.0})

	totalFiles = 0
	totalTime = 0.0

	print()
	print("=" * 100)
	print("Compile Time Information")
	print("=" * 100)
	print(f"{'File':<70} {'Time (s)':>10}")
	print("-" * 100)

	# Sort by source file path for consistent output
	sortedActions = sorted(buildActions, key=lambda a: a.sourceFile)

	for obj in sortedActions:
		relPath = os.path.relpath(obj.sourceFile, projDir).replace("\\", "/")
		compileTime = obj.compileTime
		totalTime += compileTime
		totalFiles += 1

		print(f"{relPath:<70} {compileTime:>10.4f}")

		# Aggregate by folder
		folder = os.path.dirname(relPath)
		if folder == "":
			folder = "(root)"
		folderStats[folder]["files"] += 1
		folderStats[folder]["totalTime"] += compileTime

	print("-" * 100)
	print(f"{'Total':<70} {totalTime:>10.4f}")
	print()

	# Print per-folder summary
	print(f"{'Folder':<65} {'Files':>8} {'Total (s)':>12} {'Avg (s)':>12}")
	print("-" * 100)

	for folder in sorted(folderStats.keys()):
		stats = folderStats[folder]
		avgTime = stats["totalTime"] / stats["files"] if stats["files"] > 0 else 0.0
		print(f"{folder:<65} {stats['files']:>8} {stats['totalTime']:>12.4f} {avgTime:>12.4f}")

	print("-" * 100)
	avgTotal = totalTime / totalFiles if totalFiles > 0 else 0.0
	print(f"{'Overall':<65} {totalFiles:>8} {totalTime:>12.4f} {avgTotal:>12.4f}")
	print("=" * 100)
	print()