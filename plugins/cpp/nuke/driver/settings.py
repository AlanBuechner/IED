"""What a workspace and a project tell the C++ driver, checked as strictly as Nuke checks its manifests: the
workspace's [languages.cpp] table and a project's [project.driver] table."""

from dataclasses import dataclass

LIBRARY_TYPES = ("static", "dynamic")


@dataclass(frozen=True)
class Settings:
	"""[languages.cpp]"""
	llvm: str = None                 # the LLVM release C++ compiles with (relative to the workspace)
	system_includes: tuple = ()      # system include folders every C++ compile gets
	standard: str = "c++20"          # -std= of .cpp files
	c_standard: str = "c11"          # -std= of .c files


@dataclass(frozen=True)
class Options:
	"""[project.driver]"""
	library: str = "static"          # a kind = "library" project: a static library (.lib) or a DLL
	reflection: bool = False         # run the Reflection library's HeaderTool and CodeGen over the sources
	system_includes: tuple = ()      # this project's own system include folders (-internal-isystem)
	linker_args: tuple = ()          # extra arguments of the link


def _strings(table, key, where):
	value = table.get(key, [])
	if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
		raise ValueError(f"{where}.{key} must be a list of strings")
	return tuple(value)


def _string(table, key, where, default):
	value = table.get(key, default)
	if not isinstance(value, str):
		raise ValueError(f"{where}.{key} must be a string")
	return value


def _no_others(table, known, where):
	unknown = sorted(set(table) - set(known))
	if unknown:
		raise ValueError(f"unknown key {where}.{unknown[0]} (the C++ driver reads {', '.join(sorted(known))})")


def read_settings(table):
	where = "languages.cpp"
	_no_others(table, ("llvm", "system-includes", "standard", "c-standard"), where)
	llvm = table.get("llvm")
	if llvm is not None and not isinstance(llvm, str):
		raise ValueError(f"{where}.llvm must be a folder")
	return Settings(llvm=llvm, system_includes=_strings(table, "system-includes", where),
		standard=_string(table, "standard", where, "c++20"), c_standard=_string(table, "c-standard", where, "c11"))


def read_options(spec):
	"""The [project.driver] table of `spec`, with the rules its kind sets."""
	table, where = spec.driver_options, "project.driver"
	_no_others(table, ("library", "reflection", "system-includes", "linker-args"), where)
	library = _string(table, "library", where, "static")
	if library not in LIBRARY_TYPES:
		raise ValueError(f"{where}.library must be one of {', '.join(LIBRARY_TYPES)} (got '{library}')")
	if "library" in table and spec.kind != "library":
		raise ValueError(f"{where}.library is for kind = \"library\" (this project is a {spec.kind})")
	reflection = table.get("reflection", False)
	if not isinstance(reflection, bool):
		raise ValueError(f"{where}.reflection must be true or false")
	return Options(library=library, reflection=reflection, system_includes=_strings(table, "system-includes", where),
		linker_args=_strings(table, "linker-args", where))
