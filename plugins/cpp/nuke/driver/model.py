"""A Nuke project as the C++ driver builds it (a Target) and the C++ libraries it reaches through `depends`.

Include folders and links travel through static libraries: a project compiles with its own include folders (its source
folders and `includes`) and those of every C++ static library it reaches through static libraries, and an executable or
DLL links its objects with those libraries and their `links`. An executable or a DLL in `depends` only orders the build
(a tool it runs, a program it ships beside it). A static library holds its own objects only."""

import glob
import os
from dataclasses import dataclass

from .settings import read_options

LANGUAGE = "cpp"
SOURCE_EXTENSIONS = (".c", ".cpp")
SKIPPED_FOLDERS = {"bin", "bin-int", "__pycache__"}
OUTPUT_EXTENSIONS = {"executable": ".exe", "static": ".lib", "dynamic": ".dll"}


@dataclass(frozen=True)
class Target:
	name: str
	dir: str
	kind: str              # "executable", "static" (a .lib) or "dynamic" (a DLL)
	bin_dir: str
	int_dir: str
	sources: tuple         # the .c and .cpp files it compiles
	includes: tuple        # the include folders it gives itself and its dependents
	system_includes: tuple
	defines: tuple         # NAME or NAME=VALUE
	compiler_args: tuple
	links: tuple           # library files: by path, globbed, or found by name
	linker_args: tuple
	reflection: bool
	copies: tuple          # (file or folder, where in the bin folder)
	tests: bool

	@property
	def output(self):
		return f"{self.bin_dir}/{self.name}{OUTPUT_EXTENSIONS[self.kind]}"

	@property
	def is_library(self):
		return self.kind in ("static", "dynamic")


def norm(path):
	return os.path.normpath(path).replace("\\", "/")


def absolute(path, base):
	return norm(path if os.path.isabs(path) else os.path.join(base, path))


def is_static_library(ctx):
	"""A C++ static library: what includes and links travel through."""
	return ctx.project.language == LANGUAGE and ctx.project.kind == "library" and read_options(ctx.project).library == "static"


def source_files(entries, folder):
	"""The sources of `entries`: a folder gives every .c and .cpp below it, a glob ("yaml-cpp/src/**/*.cpp") its matches,
	a file itself. A folder that is not there gives nothing (a project without src/)."""
	found = []
	for entry in entries:
		path = absolute(entry, folder)
		if glob.has_magic(entry):
			found += sorted(norm(p) for p in glob.glob(path, recursive=True) if os.path.isfile(p))
		elif os.path.isdir(path):
			found += files_below(path, SOURCE_EXTENSIONS)
		elif os.path.isfile(path):
			found.append(path)
		elif os.path.splitext(path)[1].lower() in SOURCE_EXTENSIONS:
			raise ValueError(f"source {entry} does not exist")
	unique = []
	for path in found:
		if path not in unique:
			unique.append(path)
	return tuple(unique)


def files_below(folder, extensions):
	found = []
	for base, dirs, files in os.walk(folder):
		dirs[:] = sorted(d for d in dirs if d not in SKIPPED_FOLDERS)
		found += [norm(os.path.join(base, name)) for name in sorted(files) if os.path.splitext(name)[1].lower() in extensions]
	return found


def source_folders(entries, folder):
	return tuple(absolute(entry, folder) for entry in entries if not glob.has_magic(entry) and os.path.isdir(absolute(entry, folder)))


def define_flags(defines):
	return tuple(name if value == "" else f"{name}={value}" for name, value in defines.items())


def automatic_defines(name, configuration):
	"""What every C++ compile defines: the configuration's name (DEVELOPMENT), Unicode Windows APIs, no CRT
	deprecation warnings, and the project's name."""
	return (configuration.name.upper(), "_UNICODE", "UNICODE", "_CRT_SECURE_NO_WARNINGS", f"PROJECT_NAME={name}")


def target(ctx):
	"""The Target of the project `ctx` builds."""
	spec, expand = ctx.project, ctx.expand
	options = read_options(spec)
	override = _override(spec, ctx.configuration.name)
	kind = "executable" if spec.kind == "executable" else options.library
	sources = tuple(expand(entry) for entry in spec.sources)
	defines = {**spec.defines, **override.get("defines", {}), **ctx.configuration.defines}
	return Target(
		name=spec.name,
		dir=norm(spec.dir),
		kind=kind,
		bin_dir=norm(ctx.bin_dir),
		int_dir=norm(ctx.int_dir),
		sources=source_files(sources, spec.dir),
		includes=source_folders(sources, spec.dir) + tuple(absolute(expand(i), spec.dir) for i in spec.includes),
		system_includes=tuple(absolute(expand(i), spec.dir) for i in options.system_includes),
		defines=define_flags(defines) + automatic_defines(spec.name, ctx.configuration),
		compiler_args=tuple(expand(a) for a in spec.compiler_args + tuple(override.get("compiler_args", ()))),
		links=tuple(expand(link) for link in spec.links + tuple(override.get("links", ()))),
		linker_args=tuple(expand(a) for a in options.linker_args),
		reflection=options.reflection,
		copies=tuple((absolute(expand(copy.source), spec.dir), copy.dest) for copy in spec.copies),
		tests=spec.tests is True,
	)


def _override(spec, config_name):
	"""The project's [project.config.<Name>] additions for this configuration (case-insensitive name)."""
	for name, override in spec.config_overrides.items():
		if name.lower() == config_name.lower():
			return {"defines": override.defines, "compiler_args": override.compiler_args, "links": override.links}
	return {}


def libraries(ctx):
	"""The DriverContexts of the C++ static libraries `ctx` reaches through static libraries, depth first, each once."""
	found, seen = [], set()

	def visit(context):
		for dependency in context.depends:
			if not is_static_library(dependency) or dependency.project.name in seen:
				continue
			seen.add(dependency.project.name)
			found.append(dependency)
			visit(dependency)
	visit(ctx)
	return found


def dependency(ctx, name):
	"""The context of the project called `name` that `ctx` depends on directly, or None."""
	return next((d for d in ctx.depends if d.project.name.lower() == name.lower()), None)


def excluded_links(entries):
	"""The file names "!NAME" entries leave out (lower case)."""
	return {entry[1:].lower() for entry in entries if entry.startswith("!")}


def resolve_links(entries, folder, library_dirs):
	"""Library files for `entries`: a path (relative to `folder`), a glob of paths, or a name looked up in the project's
	folder, `library_dirs` and PATH; "!NAME" leaves out every file of that name the others matched."""
	excluded = excluded_links(entries)
	files = []
	for entry in entries:
		if entry.startswith("!"):
			continue
		if glob.has_magic(entry):
			matches = sorted(norm(p) for p in glob.glob(absolute(entry, folder)) if os.path.isfile(p))
			if not matches:
				raise ValueError(f"link {entry} matches no file")
			files += matches
		else:
			files.append(find_library(entry, folder, library_dirs))
	result = []
	for path in files:
		if os.path.basename(path).lower() not in excluded and path not in result:
			result.append(path)
	return tuple(result)


def find_library(entry, folder, library_dirs):
	candidate = absolute(entry, folder)
	if os.path.isfile(candidate):
		return candidate
	if os.path.isabs(entry) or "/" in entry or "\\" in entry:
		raise ValueError(f"link {entry} does not exist")
	for directory in list(library_dirs) + [p for p in os.environ.get("PATH", "").split(os.pathsep) if p]:
		path = os.path.join(directory, entry)
		if os.path.isfile(path):
			return norm(path)
	raise ValueError(f"link {entry} is not in the project's folder, the Windows SDK, MSVC, the LLVM release or PATH")
