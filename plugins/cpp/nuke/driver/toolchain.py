"""The C++ toolchain a workspace builds with: an LLVM release (clang, lld, llvm-lib; [languages.cpp] llvm, else the
folder above the clang on PATH, else the one setup.bat installs in Nuke's home, the IED checkout), clang's
own headers, and the MSVC runtime and Windows SDK it targets (setup.bat's kit in the checkout, else Visual Studio's).

For Linux (`--system linux`, C4 D-86) Nuke runs on Linux (tools/linux/builder): the LLVM is the one whose clang the
workspace's llvm folder holds for Linux, else $LLVM_HOME's, else the clang on PATH's, and clang's driver finds the
system's headers and libraries itself."""

import functools
import os
import shutil
import sys
from dataclasses import dataclass

from . import msvc
from .settings import Settings

TARGET = "x86_64-pc-windows-msvc"
LINUX_TARGET = "x86_64-unknown-linux-gnu"


@dataclass(frozen=True)
class Toolchain:
	llvm: str              # the LLVM release's folder
	clang: str             # its clang.exe: the -cc1 compiler and the link driver
	system_includes: tuple  # clang's resource headers, the workspace's system-includes, MSVC's and the SDK's
	library_dirs: tuple    # where a library given by name is looked for
	ms_version: str
	settings: Settings
	sysroot: str = None     # the MSVC and SDK kit's folder when the build uses setup.bat's, for clang's driver
	linux: bool = False     # building for Linux, on Linux

	@property
	def target(self):
		return LINUX_TARGET if self.linux else TARGET

	@property
	def archiver(self):
		"""llvm-ar, which makes a Linux static library."""
		return f"{self.llvm}/bin/llvm-ar"

	def sysroot_args(self):
		"""clang driver arguments that take the MSVC and SDK headers and libraries from the kit, not a Visual Studio."""
		return ["-Xmicrosoft-windows-sys-root", self.sysroot] if self.sysroot else []


def checkout_llvm(nuke_home):
	"""The LLVM release setup.bat installs in Nuke's home (ctx.nuke_home), the IED checkout (tools/setup/deps.toml)."""
	return os.path.normpath(os.path.join(nuke_home, "vendor", "llvm"))


def checkout_kit(nuke_home):
	"""The MSVC runtime and Windows SDK setup.bat installs in Nuke's home (tools/setup/deps.toml, package msvc)."""
	return os.path.normpath(os.path.join(nuke_home, "vendor", "msvc"))


def clang_name():
	"""clang's file name where Nuke runs."""
	return "clang.exe" if sys.platform == "win32" else "clang"


def llvm_folder(settings, workspace_dir, checkout_llvm):
	"""The LLVM release the workspace names, else the one whose clang is on PATH, else the checkout's (a workspace made
	outside the IED checkout, from a template, names none). On Linux the workspace's folder counts only when it holds a
	Linux clang (the checkout's vendor/llvm is Windows'), and $LLVM_HOME comes before PATH."""
	if sys.platform != "win32":
		if settings.llvm:
			folder = settings.llvm if os.path.isabs(settings.llvm) else os.path.join(workspace_dir, settings.llvm)
			if os.path.isfile(os.path.join(folder, "bin", "clang")):
				return os.path.normpath(folder)
		if os.environ.get("LLVM_HOME") and os.path.isfile(os.path.join(os.environ["LLVM_HOME"], "bin", "clang")):
			return os.path.normpath(os.environ["LLVM_HOME"])
		clang = shutil.which("clang")
		if clang is not None:
			return os.path.dirname(os.path.dirname(os.path.realpath(clang)))
		raise ValueError("no LLVM for C++ on Linux: set LLVM_HOME or put clang on PATH (tools/linux/builder has both)")
	if settings.llvm:
		folder = settings.llvm if os.path.isabs(settings.llvm) else os.path.join(workspace_dir, settings.llvm)
		return os.path.normpath(folder).replace("\\", "/")
	clang = shutil.which("clang")
	if clang is not None:
		return os.path.dirname(os.path.dirname(os.path.abspath(clang))).replace("\\", "/")
	if os.path.isfile(os.path.join(checkout_llvm, "bin", "clang.exe")):
		return checkout_llvm.replace("\\", "/")
	raise ValueError("no LLVM for C++: name its folder in the workspace's [languages.cpp] llvm = \"...\", put clang on PATH, or run setup.bat in the IED checkout")


def clang_resource_includes(llvm):
	"""clang's own headers (lib/clang/<version>/include) of the LLVM release."""
	newest = msvc.newest_version_folder(os.path.join(llvm, "lib", "clang"))
	return (f"{newest}/include",) if newest else ()


@functools.lru_cache(maxsize=None)
def _resolve_linux(llvm, workspace_system_includes, settings):
	clang = os.path.join(llvm, "bin", "clang")
	if not os.path.isfile(clang):
		raise ValueError(f"no clang at {clang}")
	# A library given by name is looked for in the LLVM release, then where Debian keeps the system's.
	libraries = (f"{llvm}/lib", "/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu")
	return Toolchain(llvm, clang, workspace_system_includes, libraries, "", settings, None, linux=True)


@functools.lru_cache(maxsize=None)
def _resolve(llvm, workspace_system_includes, settings, kit):
	clang = os.path.join(llvm, "bin", "clang.exe").replace("\\", "/")
	if not os.path.isfile(clang):
		raise ValueError(f"no clang at {clang} (the workspace's [languages.cpp] llvm names {llvm}; setup.bat installs it)")
	sdk = msvc.find(kit)
	includes = clang_resource_includes(llvm) + workspace_system_includes + sdk.include_dirs
	return Toolchain(llvm, clang, includes, sdk.library_dirs + (f"{llvm}/lib",), sdk.ms_version, settings, sdk.sysroot)


def resolve(settings, workspace_dir, expand, nuke_home, system_name="windows"):
	"""The workspace's toolchain for `system_name` (probed once per LLVM folder: an installed Visual Studio's version takes
	a cl.exe run). Linux is built on Linux."""
	if system_name == "linux" and sys.platform == "win32":
		raise ValueError("C++ for Linux builds on Linux: run Nuke in tools/linux/builder's container (tools/linux/build.py)")
	llvm = llvm_folder(settings, workspace_dir, checkout_llvm(nuke_home))
	system = tuple(_absolute(expand(folder), workspace_dir) for folder in settings.system_includes)
	if system_name == "linux":
		return _resolve_linux(llvm, system, settings)
	return _resolve(llvm, system, settings, checkout_kit(nuke_home))


def _absolute(folder, base):
	return os.path.normpath(folder if os.path.isabs(folder) else os.path.join(base, folder)).replace("\\", "/")
