"""The C++ toolchain a workspace builds with: an LLVM release (clang, lld, llvm-lib; [languages.cpp] llvm, else the
folder above the clang on PATH, else the one setup.bat installs in Nuke's home, the IED checkout), clang's
own headers, and the MSVC runtime and Windows SDK it targets (setup.bat's kit in the checkout, else Visual Studio's)."""

import functools
import os
import shutil
from dataclasses import dataclass

from . import msvc
from .settings import Settings

TARGET = "x86_64-pc-windows-msvc"


@dataclass(frozen=True)
class Toolchain:
	llvm: str              # the LLVM release's folder
	clang: str             # its clang.exe: the -cc1 compiler and the link driver
	system_includes: tuple  # clang's resource headers, the workspace's system-includes, MSVC's and the SDK's
	library_dirs: tuple    # where a library given by name is looked for
	ms_version: str
	settings: Settings
	sysroot: str = None     # the MSVC and SDK kit's folder when the build uses setup.bat's, for clang's driver

	@property
	def target(self):
		return TARGET

	def sysroot_args(self):
		"""clang driver arguments that take the MSVC and SDK headers and libraries from the kit, not a Visual Studio."""
		return ["-Xmicrosoft-windows-sys-root", self.sysroot] if self.sysroot else []


def checkout_llvm(nuke_home):
	"""The LLVM release setup.bat installs in Nuke's home (ctx.nuke_home), the IED checkout (tools/setup/deps.toml)."""
	return os.path.normpath(os.path.join(nuke_home, "vendor", "llvm"))


def checkout_kit(nuke_home):
	"""The MSVC runtime and Windows SDK setup.bat installs in Nuke's home (tools/setup/deps.toml, package msvc)."""
	return os.path.normpath(os.path.join(nuke_home, "vendor", "msvc"))


def llvm_folder(settings, workspace_dir, checkout_llvm):
	"""The LLVM release the workspace names, else the one whose clang is on PATH, else the checkout's (a workspace made
	outside the IED checkout, from a template, names none)."""
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
def _resolve(llvm, workspace_system_includes, settings, kit):
	clang = os.path.join(llvm, "bin", "clang.exe").replace("\\", "/")
	if not os.path.isfile(clang):
		raise ValueError(f"no clang at {clang} (the workspace's [languages.cpp] llvm names {llvm}; setup.bat installs it)")
	sdk = msvc.find(kit)
	includes = clang_resource_includes(llvm) + workspace_system_includes + sdk.include_dirs
	return Toolchain(llvm, clang, includes, sdk.library_dirs + (f"{llvm}/lib",), sdk.ms_version, settings, sdk.sysroot)


def resolve(settings, workspace_dir, expand, nuke_home):
	"""The workspace's toolchain (probed once per LLVM folder: an installed Visual Studio's version takes a cl.exe run)."""
	llvm = llvm_folder(settings, workspace_dir, checkout_llvm(nuke_home))
	system = tuple(_absolute(expand(folder), workspace_dir) for folder in settings.system_includes)
	return _resolve(llvm, system, settings, checkout_kit(nuke_home))


def _absolute(folder, base):
	return os.path.normpath(folder if os.path.isabs(folder) else os.path.join(base, folder)).replace("\\", "/")
