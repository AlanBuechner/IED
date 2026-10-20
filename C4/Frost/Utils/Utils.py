from Utils.Windows import *
import Config

from pathlib import Path
import os

def GetLatestDir(dir):
	dirs = []
	for d in os.listdir(dir):
		if(d[0].isdigit()):
			dirs.append(d)
	return dir + sorted(dirs)[-1]

def GetBinDir(projName, root = None):
	if(root == None):
		root = Config.projectLocation
	if(root == ""): # if there is no project assume we ar building tools
		root = Config.location
	return os.path.join(root, Config.binDir.format(config=Config.configuration, system=Config.system, arc=Config.architecture, projName=projName)).replace("\\", "/")

def GetIntDir(projName, root = None):
	if(root == None):
		root = Config.projectLocation
	if(root == ""): # if there is no project assume we ar building tools
		root = Config.location
	return os.path.join(root, Config.intDir.format(config=Config.configuration, system=Config.system, arc=Config.architecture, projName=projName)).replace("\\", "/")

def GetToolsBinDir(projName, root = None):
	if(root == None):
		root = Config.projectLocation
	return os.path.join(root, Config.toolsBinDir.format(projName=projName)).replace("\\", "/")

def GetToolsIntDir(projName, root = None):
	if(root == None):
		root = Config.projectLocation
	return os.path.join(root, Config.toolsIntDir.format(projName=projName)).replace("\\", "/")

# Directories to exclude from file collection (build artifacts, generated reflection data)
EXCLUDED_DIRS = {"bin-int", "bin", "__pycache__"}

def ResolveFiles(filters, d) -> list[str]:
	files = []
	for filt in filters:
		name, ext = os.path.splitext(os.path.basename(filt))
		folder = os.path.dirname(filt)
		if (folder != ""):
			folder += "/"
		if(not os.path.isabs(folder)):
			folder = os.path.join(d, folder)
		# Normalize path separators and detect recursive patterns
		folder = folder.replace("\\", "/")
		isRecursive = folder.endswith("/**/")
		if isRecursive:
			folder = folder[:-3]  # Remove "**/"

		if(name == "*"):
			if isRecursive:
				# Recursive glob: walk the directory tree
				for (dirpath, dirnames, filenames) in os.walk(folder):
					# Prune excluded directories to prevent descending into generated/build artifacts
					dirnames[:] = [d for d in dirnames if d not in EXCLUDED_DIRS]
					files.extend([dirpath + "/" + f for f in filenames if os.path.splitext(os.path.basename(f))[1] == ext])
			else:
				# Single directory glob
				filenames = os.listdir(folder)
				for f in filenames:
					f = os.path.join(folder, f)
					if(not os.path.isfile(f)):
						continue
					if(os.path.splitext(os.path.basename(f))[1] == ext):
						files.append(f)
		elif(name == "**"):
			# Legacy recursive pattern (name is exactly **)
			for (dirpath, dirnames, filenames) in os.walk(folder):
				# Prune excluded directories to prevent descending into generated/build artifacts
				dirnames[:] = [d for d in dirnames if d not in EXCLUDED_DIRS]
				files.extend([dirpath + "/" + f for f in filenames if os.path.splitext(os.path.basename(f))[1] == ext])
		else:
			files.append(folder+name+ext)

	return files


def GetSysIncludes():
	includes = []
	includes.append(GetLatestDir(f"{Config.compilerLib}/clang/") + "/include")
	includes.append(Config.compilerInclude)
	includes.extend(GetWindowsIncludes())
	return includes


def GetTarget():
	if(Config.architecture == "x64"):
		if(Config.system == "windows"):
			return "x86_64-pc-windows-msvc"


def CollectFiles(folder: str | os.PathLike[str]) -> list[str]:
	root = Path(folder).expanduser().resolve()
	files: list[str] = []

	for dirpath, dirnames, filenames in os.walk(root, topdown=True, onerror=None, followlinks=False):
		# Prune excluded directories in-place to prevent os.walk from descending into them
		dirnames[:] = [d for d in dirnames if d not in EXCLUDED_DIRS]
		for f in filenames:
			files.append(os.path.join(dirpath, f).replace("\\", "/"))

	return files

def CollectFilesFiltered(folder: str | os.PathLike[str], filters: list[str]) -> list[str]:
	files = CollectFiles(folder)
	filteredFiles: list[str] = []
	for file in files:
		for filter in filters:
			if Path(file).suffix == filter:
				filteredFiles.append(file)
				break

	return filteredFiles


def FindLib(fileName: str, folder: str) -> str:
	# check rel path
	filePath = fileName
	if(not os.path.isabs(filePath)):
		filePath = os.path.join(folder, filePath)
	if(os.path.exists(filePath)):
		return filePath
	
	paths = os.environ["PATH"].split(";") # check PATH
	paths.extend(GetWindowsLibs()) # check windows lib folders
	paths.extend([f"{Config.compilerFolder}/lib"]) # check clang
	for path in paths:
		p = os.path.join(path, fileName)
		if(os.path.exists(p)):
			return p
	
	return ""