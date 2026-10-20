from typing import Iterable
import os
import subprocess
import shutil
import time

from Utils.Utils import *
from Utils.Windows import *
from Builder.Linker import *
from Builder.ObjectBuilder import *
from Builder.BuildInfo import *
import ProjectManager as ProjectManager
import Config



class ProjectEnvironment:
	def __init__(self):
		self.proj: ProjectManager.Project = None
		self.sourceBuildStatus = -1
		self.reflectionBuildStatus = -1
		self.linkStatus = -1
		self._buildActions: list[ObjectBuilder] = []
		self._projectInfo: Optional[ProjectBuildInfo] = None

	def InitFromProject(self, proj: ProjectManager.Project):
		self.proj = proj
		self._projectInfo = CreateProjectInfo(
			proj.projectName, Config.configuration, Config.architecture
		)

	def BuildSources(self):
		sources = self.proj.CollectSourceFiles()
		clashes = SharedObjectNames(sources)
		if clashes:
			for clash in clashes:
				print(f"ERROR: {clash}; rename one of them.")
			return 1
		self.RemoveStaleIntermediates(sources)
		result, buildActions = BuildSources(
			sources,
			self.proj.folder,
			self.proj.GetIntDir(),
			self.proj.CollectIncludes(),
			self.proj.GetSysIncludes(),
			self.proj.defines,
			self.proj.compilerArgs,
			self.proj.genReflection,
		)
		self.sourceBuildStatus = result

		# Record per-file build info for main sources
		for obj in buildActions:
			file_info = FileBuildInfo()
			file_info.sourcePath = os.path.relpath(obj.sourceFile, self.proj.folder).replace("\\", "/")
			file_info.compileNeeded = obj._needsBuild
			file_info.compileTime = obj.compileTime
			file_info.compileStartTime = obj.compileStartTime
			file_info.compileEndTime = obj.compileEndTime
			file_info.reflectionCollectNeeded = obj._needsBuildReflection
			file_info.reflectionCollectTime = obj.reflectionCollectTime
			file_info.reflectionCollectStartTime = obj.reflectionCollectStartTime
			file_info.reflectionCollectEndTime = obj.reflectionCollectEndTime
			file_info.buildSucceeded = not obj._needsBuild or obj.compileTime > 0
			RegisterFile(self.proj.projectName, file_info)

		if(self.sourceBuildStatus > 0 or self.sourceBuildStatus < -1): # failed to build sources (-1 = no work done)
			return 1

		# create and build reflection
		if(self.BuildReflection()):
			return 1

		return 0
	
	def RemoveStaleIntermediates(self, sources: list[str]):
		"""Deletes the objects of sources that were removed or renamed, before anything is compiled or linked.
		The project's generated sources (reflection, custom code generators) keep theirs."""
		intDir = self.proj.GetIntDir()
		generated = CollectFilesFiltered(os.path.join(intDir, "generated"), [".cpp"]) if os.path.isdir(os.path.join(intDir, "generated")) else []
		expected = {ObjectName(src) for src in sources + generated}
		for path in CleanStaleIntermediateFiles(expected, intDir):
			print(f"removed stale intermediate: {os.path.relpath(path, self.proj.folder).replace(os.sep, '/')}")

	def ExecuteCustomCodeGenTools(self, generatedFolder: str, sourceFolder: str) -> int:
		"""Execute custom code generation tools discovered from transitive dependencies.

		Each dependency project that declares a non-empty ``customCodeGenTool`` has its
		tool executed with the standard ``--generated-folder`` and ``--source-folder``
		CLI arguments.  Any ``.cpp`` files newly produced in the generated folder are
		compiled after all tools finish.

		Returns 0 on success, non-zero if any tool failed or compilation of generated
		outputs failed.
		"""
		custom_tools = self.proj.CollectCustomCodeGenTools()
		if not custom_tools:
			return 0

		print(f"|------------- Executing custom code gen tools : {self.proj.projectName} -------------|")

		# --- Phase 1: Execute each discovered tool ---
		for mod in custom_tools:
			toolName = mod.customCodeGenTool
			if not toolName:
				continue

			toolProj = ProjectManager.GetProject(toolName)
			if toolProj is None:
				print(f"WARNING: Custom code gen tool project '{toolName}' declared by "
				      f"'{mod.projectName}' could not be resolved. Skipping.")
				continue

			toolExe = toolProj.GetOutput()
			if not os.path.exists(toolExe):
				print(f"WARNING: Custom code gen tool executable not found at "
				      f"'{toolExe}'. Skipping.")
				continue

			args = [toolExe, "--generated-folder", generatedFolder,
			          "--source-folder", sourceFolder]
			startTime = time.perf_counter()
			result = subprocess.run(args, cwd=self.proj.folder,
			                        capture_output=True, text=True)
			elapsed = time.perf_counter() - startTime

			print(f"| Custom tool '{toolName}' from {mod.projectName} "
			      f"({elapsed:.4f}s) |")
			if result.stdout:
				print(result.stdout)
			if result.stderr:
				print(result.stderr)

			if result.returncode != 0:
				print(f"ERROR: Custom code gen tool '{toolName}' (from "
				      f"'{mod.projectName}') failed with return code "
				      f"{result.returncode}.")
				return 1

		# --- Phase 2: Compile any newly generated .cpp files ---
		cppFiles = CollectFilesFiltered(generatedFolder, [".cpp"])
		# Exclude the standard generated.cpp which was already compiled by CodeGen
		existing_generated = os.path.join(generatedFolder, "generated.cpp")
		custom_cpp_files = [f for f in cppFiles if f != existing_generated]

		if custom_cpp_files:
			print(f"|------------- Compiling {len(custom_cpp_files)} custom generated "
			      f"file(s) : {self.proj.projectName} -------------|")
			refResult, refBuildActions = BuildSources(
				custom_cpp_files,
				self.proj.folder,
				self.proj.GetIntDir(),
				self.proj.CollectIncludes(),
				self.proj.GetSysIncludes(),
				self.proj.defines,
				self.proj.compilerArgs,
				False,
			)
			if refResult != 0:
				print(f"ERROR: Compilation of custom generated files failed "
				      f"for {self.proj.projectName}.")
				return 1

			# Record build info for custom generated files
			for obj in refBuildActions:
				file_info = FileBuildInfo()
				file_info.sourcePath = os.path.relpath(
					obj.sourceFile, self.proj.folder).replace("\\", "/")
				file_info.compileNeeded = obj._needsBuild
				file_info.compileTime = obj.compileTime
				RegisterFile(self.proj.projectName, file_info)

		return 0

	def BuildReflection(self):
		if(not self.proj.genReflection):
			return 0

		codeGenProject = ProjectManager.GetProject(Config.codeGen)

		buildReflection = False
		if(self.sourceBuildStatus != -1):
			buildReflection = True

		if(codeGenProject.GetLastBuildTime() > self.proj.GetLastBuildTime()):
			buildReflection = True

		# Rebuild reflection when the previously generated object is missing or older than the
		# regenerated source. Without this, an interrupted/failed build (e.g. a killed -testall that
		# died mid-compile of generated.obj) leaves a stale object; later builds see no source change
		# and skip reflection entirely, so newly added reflected symbols (like test registrations) are
		# silently absent from the binary even though generated.cpp is fresh.
		intDir = self.proj.GetIntDir()
		genCpp = os.path.join(intDir, "generated", "generated.cpp")
		genObj = os.path.join(intDir, "generated.obj")
		if(os.path.exists(genCpp)):
			if(not os.path.exists(genObj) or os.path.getmtime(genCpp) > os.path.getmtime(genObj)):
				buildReflection = True

		reflectionCompileStartTime = 0.0
		reflectionCompileEndTime = 0.0
		reflectionCompileTime = 0.0

		generatedFolder = ""
		sourceFolder = ""

		if(buildReflection):
			args = []
			args.append(codeGenProject.GetOutput())
			intDir = self.proj.GetIntDir()
			generatedFolder = os.path.join(intDir, "generated")
			sourceFolder = os.path.join(self.proj.folder, "src")
			os.makedirs(generatedFolder, exist_ok=True)
			args.extend(["--generated-folder", generatedFolder])
			args.extend(["--source-folder", sourceFolder])
			startTime = time.perf_counter()
			self._projectInfo.reflectionGenStartTime = time.time()
			result = subprocess.run(args, cwd=self.proj.folder, capture_output=True, text=True)
			endTime = time.perf_counter()
			self._projectInfo.reflectionGenEndTime = time.time()
			self._projectInfo.reflectionGenTime = endTime - startTime
			self._projectInfo.reflectionGenNeeded = True
			print(f"|------------- Generating reflection : {self.proj.projectName} ({self._projectInfo.reflectionGenTime:.4f}s) -------------|")
			print(str(result.stderr))
			print(str(result.stdout))
			if result.returncode != 0:
				print(f"ERROR: CodeGen failed with return code {result.returncode}.")
				return 1
			print(f"|------------- Building reflection : {self.proj.projectName} -------------|")
			refResult, refBuildActions = BuildSources(
				[os.path.join(generatedFolder, "generated.cpp")],
				self.proj.folder,
				self.proj.GetIntDir(),
				self.proj.CollectIncludes(),
				self.proj.GetSysIncludes(),
				self.proj.defines,
				self.proj.compilerArgs,
				False,
			)
			self.reflectionBuildStatus = refResult

			# Record reflection compile info and capture timestamps
			for obj in refBuildActions:
				file_info = FileBuildInfo()
				file_info.sourcePath = os.path.relpath(obj.sourceFile, self.proj.folder).replace("\\", "/")
				file_info.compileNeeded = obj._needsBuild
				file_info.compileTime = obj.compileTime
				RegisterFile(self.proj.projectName, file_info)

				# Track reflection compilation timing on project info
				if obj.compileStartTime > 0:
					reflectionCompileStartTime = obj.compileStartTime
					reflectionCompileEndTime = obj.compileEndTime
					reflectionCompileTime = obj.compileTime

			self._projectInfo.reflectionCompileStartTime = reflectionCompileStartTime
			self._projectInfo.reflectionCompileEndTime = reflectionCompileEndTime
			self._projectInfo.reflectionCompileTime = reflectionCompileTime

			print(f"|------------- Finished generating reflection : {self.proj.projectName} -------------|")
		if(self.reflectionBuildStatus > 0 or self.reflectionBuildStatus < -1): # -1 = up-to-date, no work done
			return 1

		# Execute custom code gen tools from transitive dependencies
		if generatedFolder and sourceFolder:
			customResult = self.ExecuteCustomCodeGenTools(generatedFolder, sourceFolder)
			if customResult != 0:
				return 1

		return 0
			
	def Link(self):
		needsBuild = self.sourceBuildStatus != -1 or self.reflectionBuildStatus != -1
		linkResult, linkTime, linkStartTime, linkEndTime, linkStderr = LinkObjects(
			self.proj.GetIntDir(),
			self.proj.CollectLinks(),
			self.proj.folder,
			self.proj.GetOutput(),
			self.proj.linkerArgs,
			self.proj.buildType,
			needsBuild,
		)
		self.linkStatus = linkResult
		if linkTime > 0:
			self._projectInfo.linkTime = linkTime
			self._projectInfo.linkStartTime = linkStartTime
			self._projectInfo.linkEndTime = linkEndTime
			self._projectInfo.linkNeeded = True
		if(self.linkStatus > 0):
			# A lock on the OUTPUT file (another process has the exe loaded, e.g. the editor's
			# language server) is not a build error: every source compiled and only writing the
			# binary was refused. Mark it non-fatal so a full build / -testall can still run the
			# rest of the graph; the exe simply keeps its previous (still valid) contents until
			# the file is free. A real link failure (undefined symbols, bad lib) stays fatal.
			if self._isLockedOutputError(linkStderr):
				print(f"[link] WARNING: {self.proj.projectName} sources built OK but the output "
					f"'{os.path.basename(self.proj.GetOutput())}' is locked by another process; "
					f"keeping the previous binary and continuing. It will relink on the next build.")
				self.linkStatus = -2
				return 0
			return 1
		return 0

	def _isLockedOutputError(self, stderr):
		if not stderr:
			return False
		s = stderr.lower()
		return ("permission denied" in s or "access is denied" in s or "winerror 5" in s) \
			and ("failed to write output" in s or "unable to remove file" in s or "cannot open" in s)


	def Build(self):

		if(self.proj.buildType == Config.BuildType.UNBUILT_LIBRARY):
			return 0

		intDir = self.proj.GetIntDir()
		if(not os.path.exists(intDir)):
			os.makedirs(intDir)

		binDir = self.proj.GetBinDir()
		if(not os.path.exists(binDir)):
			os.makedirs(binDir)

		# build source files
		if self.BuildSources():
			return 1

		# link
		if self.Link():
			return 1

		# copy files and symbolic links (only when something was actually built)
		if(self.linkStatus != -1 and self.proj.buildType == Config.BuildType.EXECUTABLE):
			binDir = self.proj.GetBinDir()

			# copy asan files (only if source changed or dest missing)
			if(Config.useAsan == True):
				asanSrc = f"{Config.GetClangLibDir()}/clang_rt.asan_dynamic-x86_64.dll"
				asanDest = f"{binDir}/clang_rt.asan_dynamic-x86_64.dll"
				_conditional_copy(asanSrc, asanDest)

			# create symbolic link for assets folder
			assetsLink = f"{binDir}/assets"
			if os.path.lexists(assetsLink) and not os.path.exists(assetsLink):
				os.unlink(assetsLink)  # remove broken symlink
			if(os.path.lexists(assetsLink) == False):
				try:
					os.symlink(f"{self.proj.folder}/assets", assetsLink, target_is_directory=True)
				except OSError:
					# os.symlink needs elevation/developer mode; a junction works without it.
					# cmd reads a forward slash as a switch: mklink needs backslash paths.
					target = f"{self.proj.folder}/assets"
					subprocess.run(["cmd", "/c", "mklink", "/J", assetsLink.replace("/", "\\"), target.replace("/", "\\")], check=False)

		# copy module files (only if updated)
		for mod in self.proj.CollectAllModules():
			CopyFilesToBinDir(mod.copies, binDir)
		
		# also copy root project's own copies (e.g., DLLs for the compiler)
		CopyFilesToBinDir(self.proj.copies, binDir)

		# copy embedded resource folders for executables regardless of link status,
		# so that changes to shaders, fonts, etc. are picked up even when no C++ code changed
		if(self.proj.buildType == Config.BuildType.EXECUTABLE):
			binDir = self.proj.GetBinDir()
			for mod in self.proj.CollectAllModules():
				resourceFolder = mod.GetEmbeddedFolder()
				if(os.path.exists(resourceFolder)):
					_copytree_if_updated(resourceFolder, binDir + "/embedded")

		# run custom build step
		if (hasattr(self.proj.script, "OnAfterBuild")):
			self.proj.script.OnAfterBuild()

		if self.linkStatus != -1: # check if a build was done
			if (hasattr(self.proj.script, "OnProjectBuilt")):
				self.proj.script.OnProjectBuilt()

		return 0
	
def _needs_copy(src: str, dest: str) -> bool:
	"""Return True if *dest* is missing or older than *src*."""
	if not os.path.exists(dest):
		return True
	return os.path.getmtime(src) > os.path.getmtime(dest)


def _conditional_copy(src: str, dest: str) -> None:
	"""Copy *src* to *dest* only if *dest* is missing or older than *src*.

	Explicitly removes the existing destination before copying so that stale
	or read-only files don't block the update.

	On Windows NTFS (~2 s mtime granularity) we avoid the equal-timestamp
	skip by always writing the destination with a timestamp strictly less
	than the source after a successful copy.  This guarantees that a future
	build will correctly detect a newer source and override the embedded file.
	"""
	if _needs_copy(src, dest):
		try:
			if os.path.exists(dest):
				os.remove(dest)
			shutil.copy(src, dest)
		except (PermissionError, OSError) as e:
			# A destination held open by another process (a running tool locking its own DLLs)
			# cannot be replaced. Skip it with a warning instead of aborting the whole build;
			# the next build after that process exits retries the copy.
			print(f"[copy] warning: could not update {dest}: {e}")
			print("[copy]         file is in use by another process; it will be updated on the next build.")
			return
		# Ensure dest mtime is strictly less than src mtime so that the next
		# build correctly sees the source as newer when it has changed again.
		# Subtract 2 s to account for NTFS ~2-second timestamp granularity.
		src_mtime = os.path.getmtime(src)
		os.utime(dest, (src_mtime - 2, src_mtime - 2))


def Copy(src, dest):
	if(os.path.isdir(src)):
		_copytree_if_updated(src, dest)
	else:
		dest_file = dest if os.path.isdir(dest) else os.path.dirname(dest) or "."
		dest_path = os.path.join(dest_file, os.path.basename(src))
		_conditional_copy(src, dest_path)


def _remove_stale(path: str):
	try:
		if os.path.isdir(path) and not os.path.islink(path):
			shutil.rmtree(path)
		else:
			os.remove(path)
	except (PermissionError, OSError) as e:
		print(f"[copy] warning: could not remove {path}: {e}")


def _copytree_if_updated(src: str, dest: str):
	"""Mirror *src* into *dest*: copy files that changed, remove what *src* no longer has."""
	if not os.path.exists(dest):
		os.makedirs(dest, exist_ok=True)
	names = set()
	for entry in os.scandir(src):
		names.add(entry.name)
		src_child = entry.path
		dest_child = os.path.join(dest, entry.name)
		if entry.is_dir():
			_copytree_if_updated(src_child, dest_child)
		else:
			_conditional_copy(src_child, dest_child)
	for entry in os.scandir(dest):
		if entry.name not in names:
			_remove_stale(entry.path)


def CopyFilesToBinDir(files, binDir):
	for copy in files:
		if(isinstance(copy, dict)):
			folder = binDir + "/" + copy["folder"]
			os.makedirs(folder, exist_ok=True)
			CopyFilesToBinDir(copy["files"], folder)
		else:
			folder = binDir + "/" + os.path.basename(copy)
			Copy(copy, folder)