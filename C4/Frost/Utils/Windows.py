import Config

import subprocess
import re
import os

def GetLatestDir(dir):
	dirs = []
	for d in os.listdir(dir):
		if(d[0].isdigit()):
			dirs.append(d)
	return dir + sorted(dirs)[-1]

def GetVisualStudioDirectory():
	# Auto-detect the newest installed Visual Studio edition that has an MSVC toolchain.
	base = "C:/Program Files/Microsoft Visual Studio/"
	try:
		candidates = []
		for version in os.listdir(base):
			if not version[0].isdigit() and version != "Installer":
				continue
			for edition in os.listdir(os.path.join(base, version)):
				msvcRoot = os.path.join(base, version, edition, "VC/Tools/MSVC/")
				if os.path.isdir(msvcRoot) and any(d[0].isdigit() for d in os.listdir(msvcRoot)):
					candidates.append((version, os.path.join(base, version, edition) + "/"))
		if candidates:
			return sorted(candidates)[-1][1]
	except OSError:
		pass
	return "C:/Program Files/Microsoft Visual Studio/2022/Community/"

def GetMSVCDirectory():
	return GetLatestDir(GetVisualStudioDirectory() + "VC/Tools/MSVC/")

def CashFMSVersion():
	# cl.exe by its full path: a bare name only resolves through the current directory, which
	# NoDefaultCurrentDirectoryInExePath turns off. cl prints its banner (and version) on stderr.
	compiler = GetMSVCDirectory() + "/bin/Hostx64/x64/cl.exe"
	result = subprocess.run([compiler], capture_output=True, text=True)
	version = re.search(r"\d+\.\d+\.\d+", result.stderr)
	if version is None:
		raise RuntimeError(f"could not read the MSVC version from '{compiler}': {result.stderr.strip()}")
	return version.group()
fms_version = CashFMSVersion()

def GetFMSVersion():
	return fms_version

def GetWindowsKit():
	return "C:/Program Files (x86)/Windows Kits"

def GetWindowsKitInclude():
	return GetLatestDir(f"{GetWindowsKit()}/10/Include/")

def GetWindowsKitBin():
	return GetLatestDir(f"{GetWindowsKit()}/10/bin/")

def GetWindowsKitLib():
	return GetLatestDir(f"{GetWindowsKit()}/10/Lib/")

def GetNETFXSDK():
	return GetLatestDir("C:/Program Files (x86)/Windows Kits/NETFXSDK/")

def GetWindowsIncludes():
	vsDir = GetVisualStudioDirectory()
	msvcDir = GetMSVCDirectory()
	windowsKitIncludeBase = GetWindowsKitInclude()
	NETFXSDK = GetNETFXSDK()

	includes = []
	includes.append(f"{msvcDir}/include")
	includes.append(f"{msvcDir}/atlmfc/include")
	includes.append(f"{vsDir}/VC/Auxiliary/VS/include")
	includes.append(f"{vsDir}/VC/Auxiliary/VS/UnitTest/include")
	includes.append(f"{windowsKitIncludeBase}/ucrt")
	includes.append(f"{windowsKitIncludeBase}/um")
	includes.append(f"{windowsKitIncludeBase}/shared")
	includes.append(f"{windowsKitIncludeBase}/winrt")
	includes.append(f"{windowsKitIncludeBase}/cppwinrt")
	includes.append(f"{NETFXSDK}4.8/Include/um")
	return includes


def GetWindowsLibs():
	vsDir = GetVisualStudioDirectory()
	msvcDir = GetMSVCDirectory()
	windowsKitLibBase = GetWindowsKitLib()
	NETFXSDK = GetNETFXSDK()

	# TODO add more
	includes = []
	includes.append(f"{windowsKitLibBase}/um/x64")
	return includes