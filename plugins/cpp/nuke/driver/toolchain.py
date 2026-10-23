"""The C++ toolchain a workspace builds with: an LLVM release (clang, lld, llvm-lib; [languages.cpp] llvm, else the
folder above the clang on PATH, else the one setup.bat installs in the IED checkout this package is part of), clang's
own headers, and the Visual Studio and Windows SDK it targets."""

import functools
import os
import shutil
from dataclasses import dataclass

from . import msvc
from .settings import Settings

TARGET = "x86_64-pc-windows-msvc"
# plugins/cpp/nuke/driver -> the checkout's vendor/llvm (tools/setup/deps.toml)
CHECKOUT_LLVM = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "vendor", "llvm"))


@dataclass(frozen=True)
class Toolchain:
	llvm: str              # the LLVM release's folder
	clang: str             # its clang.exe: the -cc1 compiler and the link driver
	system_includes: tuple  # clang's resource headers, the workspace's system-includes, MSVC's and the SDK's
	library_dirs: tuple    # where a library given by name is looked for
	ms_version: str
	settings: Settings

	@property
	def target(self):
		return TARGET


def llvm_folder(settings, workspace_dir, checkout_llvm=CHECKOUT_LLVM):
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
def _resolve(llvm, workspace_system_includes, settings):
	clang = os.path.join(llvm, "bin", "clang.exe").replace("\\", "/")
	if not os.path.isfile(clang):
		raise ValueError(f"no clang at {clang} (the workspace's [languages.cpp] llvm names {llvm}; setup.bat installs it)")
	sdk = msvc.find()
	includes = clang_resource_includes(llvm) + workspace_system_includes + sdk.include_dirs
	return Toolchain(llvm, clang, includes, sdk.library_dirs + (f"{llvm}/lib",), sdk.ms_version, settings)


def resolve(settings, workspace_dir, expand):
	"""The workspace's toolchain (probed once per LLVM folder: the MSVC version takes a cl.exe run)."""
	llvm = llvm_folder(settings, workspace_dir)
	system = tuple(_absolute(expand(folder), workspace_dir) for folder in settings.system_includes)
	return _resolve(llvm, system, settings)


def _absolute(folder, base):
	return os.path.normpath(folder if os.path.isabs(folder) else os.path.join(base, folder)).replace("\\", "/")
