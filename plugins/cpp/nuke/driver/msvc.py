"""The Microsoft side of a Windows C++ build: the MSVC runtime's headers and libraries (and the version clang's
-fms-compatibility-version must name) and the Windows SDK's - from the kit setup.bat installs in the IED checkout
(vendor/msvc: Visual Studio's layout without Visual Studio, tools/setup/msvc_kit.py) when there is one, which makes
every machine build alike, else from the newest Visual Studio with C++ tools and the installed Windows SDK."""

import os
import re
import subprocess
from dataclasses import dataclass

VISUAL_STUDIO_ROOTS = ("C:/Program Files/Microsoft Visual Studio", "C:/Program Files (x86)/Microsoft Visual Studio")
WINDOWS_KITS = "C:/Program Files (x86)/Windows Kits"
SDK_INCLUDE_PARTS = ("ucrt", "um", "shared", "winrt", "cppwinrt")


@dataclass(frozen=True)
class WindowsSdk:
	include_dirs: tuple    # system include folders: MSVC's, then the Windows SDK's
	library_dirs: tuple    # where library names (kernel32.lib) are looked for
	ms_version: str        # cl.exe's version, for -fms-compatibility-version
	sysroot: str = None    # the kit's folder, for clang's -Xmicrosoft-windows-sys-root; None for an installed Visual Studio


def version_key(name):
	return tuple(int(part) if part.isdigit() else 0 for part in re.split(r"[.\-]", name))


def newest_version_folder(folder):
	"""The sub-folder of `folder` with the highest version number for a name ("10.0.22621.0"), or None."""
	try:
		names = [name for name in os.listdir(folder) if name[:1].isdigit() and os.path.isdir(os.path.join(folder, name))]
	except OSError:
		return None
	return os.path.join(folder, max(names, key=version_key)).replace("\\", "/") if names else None


def visual_studio():
	"""The newest Visual Studio install that has MSVC tools: (its folder, its newest MSVC folder), or (None, None)."""
	found = []
	for root in VISUAL_STUDIO_ROOTS:
		try:
			versions = [v for v in os.listdir(root) if v[:1].isdigit()]
		except OSError:
			continue
		for version in versions:
			try:
				editions = os.listdir(os.path.join(root, version))
			except OSError:
				continue
			for edition in editions:
				install = os.path.join(root, version, edition).replace("\\", "/")
				msvc = newest_version_folder(os.path.join(install, "VC", "Tools", "MSVC"))
				if msvc:
					found.append((version_key(version), version_key(os.path.basename(msvc)), install, msvc))
	if not found:
		return None, None
	_, _, install, msvc = max(found)
	return install, msvc


def compiler_version(msvc):
	"""cl.exe's version (it prints its banner, with the version, on stderr when run without arguments)."""
	cl = os.path.join(msvc, "bin", "Hostx64", "x64", "cl.exe")
	try:
		result = subprocess.run([cl], capture_output=True, text=True, timeout=60)
	except OSError as error:
		raise ValueError(f"cannot run {cl} to read the MSVC version: {error}") from None
	version = re.search(r"\d+\.\d+\.\d+", result.stderr)
	if version is None:
		raise ValueError(f"could not read the MSVC version from {cl}: {result.stderr.strip()}")
	return version.group()


def toolset_compiler_version(msvc):
	"""The cl.exe version of an MSVC toolset folder, from its name: toolset 14.51.36231 is cl 19.51.36231."""
	parts = os.path.basename(msvc).split(".")
	return ".".join(["19"] + parts[1:3])


def existing(folders):
	return tuple(folder for folder in folders if os.path.isdir(folder))


def sdk_of(install, msvc, kits, ms_version, sysroot=None):
	kit_include = newest_version_folder(f"{kits}/10/Include")
	kit_lib = newest_version_folder(f"{kits}/10/Lib")
	if kit_include is None or kit_lib is None:
		raise ValueError(f"no Windows 10/11 SDK under {kits}/10: run setup.bat in the IED checkout (it installs one in vendor/msvc)")
	includes = [f"{msvc}/include", f"{msvc}/atlmfc/include", f"{install}/VC/Auxiliary/VS/include",
		f"{install}/VC/Auxiliary/VS/UnitTest/include"]
	includes += [f"{kit_include}/{part}" for part in SDK_INCLUDE_PARTS]
	libraries = [f"{kit_lib}/um/x64", f"{kit_lib}/ucrt/x64", f"{msvc}/lib/x64"]
	return WindowsSdk(existing(includes), existing(libraries), ms_version, sysroot)


def find(kit):
	"""The kit at `kit` (vendor/msvc of the IED checkout) when it is there, else the Visual Studio and Windows SDK this
	machine has; a ValueError saying what to do when there are none."""
	kit = kit.replace("\\", "/")
	msvc = newest_version_folder(f"{kit}/VC/Tools/MSVC")
	if msvc is not None:
		return sdk_of(kit, msvc, f"{kit}/Windows Kits", toolset_compiler_version(msvc), kit)
	install, msvc = visual_studio()
	if msvc is None:
		raise ValueError(f"no MSVC runtime and Windows SDK: run setup.bat in the IED checkout (it installs them in {kit})")
	return sdk_of(install, msvc, f"{WINDOWS_KITS}", compiler_version(msvc))
