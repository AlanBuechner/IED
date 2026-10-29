"""nuke.toml parsing. Manifests are declarative and strictly validated: unknown keys and wrong types are errors
that name the file and key, so a typo never silently changes a build."""

import difflib
import os
import re
import tomllib
from dataclasses import dataclass, field

from .errors import NukeError

MANIFEST_NAME = "nuke.toml"
KINDS = ("executable", "module", "library", "custom")   # library: a non-C4 language's (a C4 one is a module)
SUBSYSTEMS = ("console", "windows")                     # a C4 executable's (C4.exe --subsystem)
PROJECT_NAME = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
COMMAND_NAME = re.compile(r"^[a-z][a-z0-9-]*$")
VERSION = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$")
NUGET_ID = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*$")
NUGET_VERSION = re.compile(r"^\d+(\.\d+){0,3}(-[0-9A-Za-z.-]+)?$")   # NuGet's: up to four numbers, a prerelease label
PACKAGE_PATH = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.-]*(/[A-Za-z0-9_][A-Za-z0-9_.-]*)*$")


@dataclass(frozen=True)
class StepSpec:
	name: str
	phase: str
	origin: str
	run: object = None              # list[str] (argv) or str (shell command line)
	python: str = None              # "path/to/file.py:function" or "module:function"
	function: object = None         # a callable registered by an extension script
	cwd: str = None
	env: dict = field(default_factory=dict)
	inputs: tuple = ()
	outputs: tuple = ()
	configs: tuple = ()             # empty: every configuration
	description: str = ""

	def applies_to(self, config_name):
		return not self.configs or any(c.lower() == config_name.lower() for c in self.configs)


@dataclass(frozen=True)
class CopySpec:
	source: str
	dest: str
	exclude: tuple = ()             # of a folder: paths in it or names, globs allowed, left out (fsutil.excluded)


@dataclass(frozen=True)
class ConfigOverride:
	defines: dict = field(default_factory=dict)
	compiler_args: tuple = ()
	links: tuple = ()


@dataclass(frozen=True)
class ProjectSpec:
	name: str
	dir: str
	file: str
	kind: str
	description: str = ""
	group: str = ""
	depends: tuple = ()
	sources: tuple = ("src",)
	includes: tuple = ()
	defines: dict = field(default_factory=dict)
	links: tuple = ()
	compiler_args: tuple = ()
	subsystem: str = "console"      # a C4 executable's: "console", or "windows" for a windowed program (no console)
	tests: object = "auto"          # "auto", True or False
	output: str = None              # custom kind: the file `nuke run` starts
	working_dir: str = "."
	copies: tuple = ()
	steps: tuple = ()
	config_overrides: dict = field(default_factory=dict)
	scripts: tuple = ()
	language: str = ""              # the project's language: "c4" by default for executable and module; another one
	                                # builds through its driver (languages.py), a custom project's is for IDEs only
	driver_options: dict = field(default_factory=dict)   # [project.driver]: what the language's driver reads
	package: object = None          # packages.Origin of a project a package brings (None: the workspace's own)
	dependencies: tuple = ()        # [dependencies]: the packages it uses (PackageRequirements); their projects join
	                                # its depends

	@property
	def is_c4(self):
		return self.kind in ("executable", "module") and self.language == "c4"

	@property
	def is_driven(self):
		"""Built, tested and run by its language's driver: an executable or library in a language other than C4."""
		return self.kind in ("executable", "library") and self.language != "c4"


@dataclass(frozen=True)
class CommandSpec:
	name: str
	origin: str
	description: str = ""
	requires: tuple = ()
	projects: tuple = None          # None: the invocation's selection
	steps: tuple = ()
	function: object = None         # an extension's command handler: function(ctx)


@dataclass(frozen=True)
class WorkspaceSpec:
	name: str
	dir: str
	file: str
	default_project: str = None
	toolchain: str = None
	projects: tuple = None          # None: discover every nuke.toml with a [project] table
	exclude: tuple = ()
	default_configuration: str = None
	configurations: dict = field(default_factory=dict)
	scripts: tuple = ()
	steps: tuple = ()
	commands: tuple = ()
	ide: bool = True
	languages: dict = field(default_factory=dict)   # [languages]: language -> its package's folder (languages.py)
	language_settings: dict = field(default_factory=dict)   # [languages.<id>] tables: language -> what its driver reads
	ied_plugins: tuple = ()         # folders or globs of the IED plugins it develops: projects of IED's solution (ied_files.py)
	packages_dir: str = "packages"  # where Nuke installs packages, relative to the workspace


@dataclass(frozen=True)
class PackageRequirement:
	"""A [dependencies] entry. Its source as written is a package in a git repository - "<repository URL>/<the package's
	folder there>": `url` and `path` - wanted at a version, a branch or a commit (none: any version), or a folder of the
	project's own package (`local`); or a NuGet package (`nuget`: its id) at an exact version or `*` (packages/nuget.py)."""
	name: str                       # its key in the manifest
	origin: str                     # the manifest that names it
	source: str                     # as written
	url: str = None                 # the repository of a package from git; None for a local dependency
	path: str = ""                  # the package's folder in its repository ("" for the repository as a whole)
	local: str = None               # a local dependency's folder, as written
	version: str = None
	branch: str = None
	rev: str = None
	writable: bool = False          # the workspace develops its repository: a git clone, not read-only installs
	nuget: str = None               # a NuGet package's id (Microsoft.Direct3D.D3D12); its version is `version`

	def describe(self):
		if self.local is not None:
			return "its own package"
		if self.branch:
			return f"branch {self.branch}"
		if self.rev:
			return f"commit {self.rev[:12]}"
		return self.version or "*"


@dataclass(frozen=True)
class PackageSpec:
	"""A [package] table: the folder is a package of its repository (Nuke/README.md section 12)."""
	dir: str
	file: str
	description: str = ""
	dependencies: tuple = ()        # [dependencies] (and [package.dependencies]): the packages it uses
	version: str = None             # its version, which `nuke package release` writes and tags


@dataclass(frozen=True)
class ManifestFile:
	path: str
	workspace: WorkspaceSpec = None
	project: ProjectSpec = None
	package: PackageSpec = None


# ------------------------------------------------------------------------------------------------ #

class Table:
	"""Typed, checked access to one TOML table; every key read is recorded so leftovers can be reported."""

	def __init__(self, data, where, file):
		if not isinstance(data, dict):
			raise NukeError(f"{file}: {where} must be a table")
		self.data = data
		self.where = where
		self.file = file
		self.read = set()

	def _fail(self, key, expected):
		raise NukeError(f"{self.file}: {self.where}.{key} must be {expected}")

	def _get(self, key, default):
		self.read.add(key)
		return self.data.get(key, default)

	def string(self, key, default=None):
		value = self._get(key, default)
		if value is not None and not isinstance(value, str):
			self._fail(key, "a string")
		return value

	def boolean(self, key, default):
		value = self._get(key, default)
		if not isinstance(value, bool):
			self._fail(key, "true or false")
		return value

	def integer(self, key, default):
		value = self._get(key, default)
		if not isinstance(value, int) or isinstance(value, bool):
			self._fail(key, "an integer")
		return value

	def strings(self, key, default=()):
		value = self._get(key, None)
		if value is None:
			return tuple(default) if default is not None else None
		if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
			self._fail(key, "a list of strings")
		return tuple(value)

	def string_map(self, key):
		value = self._get(key, {})
		if not isinstance(value, dict):
			self._fail(key, "a table of strings")
		result = {}
		for name, item in value.items():
			if isinstance(item, bool):
				item = "1" if item else "0"
			elif isinstance(item, (int, float)):
				item = str(item)
			if not isinstance(item, str):
				self._fail(f"{key}.{name}", "a string, number or boolean")
			result[name] = item
		return result

	def table(self, key):
		value = self._get(key, None)
		return None if value is None else Table(value, f"{self.where}.{key}", self.file)

	def tables(self, key):
		value = self._get(key, [])
		if not isinstance(value, list):
			self._fail(key, "an array of tables")
		return [Table(item, f"{self.where}.{key}[{i}]", self.file) for i, item in enumerate(value)]

	def subtables(self, key):
		value = self._get(key, {})
		if not isinstance(value, dict):
			self._fail(key, "a table of tables")
		return [(name, Table(item, f"{self.where}.{key}.{name}", self.file)) for name, item in value.items()]

	def done(self):
		unknown = [k for k in self.data if k not in self.read]
		if unknown:
			key = unknown[0]
			hint = difflib.get_close_matches(key, sorted(self.read), n=1)
			suggestion = f" (did you mean '{hint[0]}'?)" if hint else ""
			raise NukeError(f"{self.file}: unknown key {self.where}.{key}{suggestion}")


# ------------------------------------------------------------------------------------------------ #

def load(path):
	try:
		with open(path, "rb") as f:
			data = tomllib.load(f)
	except tomllib.TOMLDecodeError as error:
		raise NukeError(f"{path}: {error}") from None
	except OSError as error:
		raise NukeError(f"{path}: {error.strerror}") from None
	return parse(data, path)


def parse(data, path):
	path = os.path.abspath(path).replace("\\", "/")
	folder = os.path.dirname(path)
	root = Table(data, "", path)
	root.where = "(root)"
	workspace_table = root.table("workspace")
	project_table = root.table("project")
	configurations = root.subtables("configurations")
	commands = root.subtables("commands")
	languages = _languages(root.table("languages"))
	package_table = root.table("package")
	dependencies_table = root.table("dependencies")
	for gone in ("sources", "packages"):
		if root.table(gone) is not None:
			raise NukeError(f"{path}: [{gone}] is gone: each project lists the packages it uses in its own [dependencies], "
				f"as Name = {{ source = \"https://github.com/owner/repo.git/Folder/Of/Package\", version = \"^1.0\" }}")
	root.done()
	if workspace_table is None and project_table is None and package_table is None:
		raise NukeError(f"{path}: needs a [workspace], [project] or [package] table")
	if workspace_table is None and (configurations or commands or any(languages)):
		raise NukeError(f"{path}: [configurations], [commands] and [languages] belong to the workspace file")
	if dependencies_table is not None and project_table is None and package_table is None:
		raise NukeError(f"{path}: [dependencies] belong to a project or a package (a [project] or [package] table)")
	dependencies = _dependencies(dependencies_table, path) if dependencies_table is not None else ()
	workspace = _workspace(workspace_table, configurations, commands, languages, folder, path) if workspace_table else None
	project = _project(project_table, folder, path, dependencies) if project_table else None
	package = _package(package_table, folder, path, dependencies) if package_table else None
	return ManifestFile(path, workspace, project, package)


def _dependencies(t, path):
	"""[dependencies]: name -> { source = "<repository URL>/<package folder>", version | branch | rev, writable } for a
	package from git, { source = "<folder from the workspace's root>" } for a project of the same package, or
	{ nuget = "<package id>", version = "<exact version>" } for a NuGet package."""
	found = []
	for key, value in t.data.items():
		t.read.add(key)
		where = f"dependencies.{key}"
		if not isinstance(value, dict):
			raise NukeError(f"{path}: {where} must be a table: {{ source = \"https://host/owner/repo.git/Folder\", version = \"^1.0\" }}, "
				f"{{ source = \"Folder\" }} (from the workspace's root) for a project of the same package, or "
				f"{{ nuget = \"Package.Id\", version = \"1.2.3\" }} for a NuGet package")
		table = Table(value, where, path)
		if "nuget" in value:
			found.append(_nuget_dependency(key, table, path, where))
			continue
		source = table.string("source")
		if not source:
			raise NukeError(f"{path}: {where} needs its source: the package's repository URL and folder, or a local folder "
				f"(or nuget = \"Package.Id\" for a NuGet package)")
		version, branch, rev = table.string("version"), table.string("branch"), table.string("rev")
		writable = table.boolean("writable", False)
		table.done()
		url, folder = split_source(source)
		if url is None:
			folder = folder.strip("/").removeprefix("./")
			parts = folder.split("/")
			if not folder or ".." in parts or re.match(r"^[A-Za-z]:", folder) or source.startswith("/"):
				raise NukeError(f"{path}: {where}: '{source}' - a local dependency is a folder of the workspace written from its "
					f"root, as {{ source = \"Game\" }} or {{ source = \"Engine/Core\" }}: no ../ and no absolute path")
			if version or branch or rev or writable:
				raise NukeError(f"{path}: {where}: '{source}' is a local folder - a project of the same package, built with "
					f"it - so it takes no version, branch, rev or writable")
			found.append(PackageRequirement(key, path, source, local=folder))
			continue
		if sum(x is not None for x in (version, branch, rev)) > 1:
			raise NukeError(f"{path}: {where} takes one of version, branch and rev")
		if folder and not PACKAGE_PATH.match(folder):
			raise NukeError(f"{path}: {where}: '{folder}' is not a folder of the repository (like Renderer/RHI)")
		if writable and rev:
			raise NukeError(f"{path}: {where}: a writable package is a clone on a branch, so it takes version or branch, not rev")
		found.append(PackageRequirement(key, path, source, url=url, path=folder,
			version=version if (version or branch or rev) else "*", branch=branch, rev=rev, writable=writable))
	return tuple(found)


def _nuget_dependency(key, table, path, where):
	"""{ nuget = "<package id>", version = "<exact version>" | "*" }: NuGet versions carry no compatibility promise (four
	numbers, prereleases), so one is written exactly; `*` takes the feed's newest stable one, which the lock then keeps."""
	package_id = table.string("nuget")
	version = table.string("version", "*")
	extra = sorted(k for k in table.data if k not in ("nuget", "version"))
	if extra:
		raise NukeError(f"{path}: {where}: a NuGet package takes nuget and version only (not {', '.join(extra)})")
	table.done()
	if not package_id or not NUGET_ID.match(package_id):
		raise NukeError(f"{path}: {where}: '{package_id or ''}' is not a NuGet package id (like Microsoft.Direct3D.D3D12)")
	if version != "*" and not NUGET_VERSION.match(version):
		raise NukeError(f"{path}: {where}: '{version}' is not a NuGet version: write the exact one (1.620.1, 1.9.2609.5) or \"*\" "
			f"for the newest stable one")
	return PackageRequirement(key, path, f"nuget:{package_id}", version=version, nuget=package_id)


REMOTE = re.compile(r"^([a-z][a-z0-9+.-]*://[^/]+/[^/]+/[^/]+|[\w.-]+@[\w.-]+:[^/]+/[^/]+)(/.*)?$", re.IGNORECASE)


def split_source(source):
	"""(repository URL, the package's folder there) of a source in a git repository - "<URL>.git/<folder>", or a URL of
	a host's owner/repository followed by the folder - else (None, the local folder it is)."""
	text = source.strip().replace("\\", "/")
	marker = text.find(".git/")
	if marker >= 0:
		return text[:marker + 4], text[marker + 5:].strip("/")
	if text.endswith(".git"):
		return text, ""
	match = REMOTE.match(text)
	if match:
		return match.group(1), (match.group(2) or "").strip("/")
	return None, text


def _package(t, folder, path, dependencies=()):
	t.where = "package"
	if t.table("dependencies") is not None:
		raise NukeError(f"{path}: [package.dependencies] is now [dependencies], each entry with its repository's URL: "
			f"Name = {{ git = \"<url>\", path = \"<its folder there>\", version = \"^1.0\" }}")
	version = t.string("version")
	if version is not None and not VERSION.match(version):
		raise NukeError(f"{path}: package.version must be a version like 0.2.0 (got '{version}')")
	spec = PackageSpec(
		dir=folder,
		file=path,
		description=t.string("description", ""),
		dependencies=tuple(dependencies),
		version=version,
	)
	t.done()
	return spec


def is_url(text):
	"""Whether `text` names a git repository (rather than a repository's name, or a local folder)."""
	return bool(text) and split_source(text)[0] is not None


def _workspace(t, configurations, commands, languages, folder, path):
	t.where = "workspace"
	spec = WorkspaceSpec(
		name=t.string("name") or os.path.basename(folder),
		dir=folder,
		file=path,
		default_project=t.string("default-project"),
		toolchain=t.string("toolchain"),
		projects=t.strings("projects", default=None),
		exclude=t.strings("exclude"),
		default_configuration=t.string("default-configuration"),
		configurations=dict(_configuration(name, c) for name, c in configurations),
		scripts=t.strings("scripts"),
		steps=tuple(_step(s, path) for s in t.tables("steps")),
		commands=tuple(_command(name, c, path) for name, c in commands),
		ide=t.boolean("ide", True),
		languages=languages[0],
		language_settings=languages[1],
		ied_plugins=t.strings("ied-plugins"),
		packages_dir=t.string("packages-dir", "packages"),
	)
	t.done()
	parts = spec.packages_dir.replace("\\", "/").split("/")
	if os.path.isabs(spec.packages_dir) or ".." in parts or not spec.packages_dir.strip():
		raise NukeError(f"{path}: workspace.packages-dir must be a folder inside the workspace")
	return spec


def _languages(t):
	"""[languages]: each language is its package's folder (`rust = "tools/rust"`) or a table of what its driver reads,
	with that folder as `package` when it pins one (`[languages.cpp] llvm = "vendor/llvm"`). (pins, settings)."""
	pins, settings = {}, {}
	if t is None:
		return pins, settings
	for language, value in t.data.items():
		t.read.add(language)
		if isinstance(value, str):
			pins[language] = value
		elif isinstance(value, dict):
			table = dict(value)
			package = table.pop("package", None)
			if package is not None and not isinstance(package, str):
				raise NukeError(f"{t.file}: languages.{language}.package must be a string (the package's folder)")
			if package is not None:
				pins[language] = package
			settings[language] = table
		else:
			raise NukeError(f"{t.file}: languages.{language} must be its package's folder or a table of its driver's settings")
	return pins, settings


def _configuration(name, t):
	if not PROJECT_NAME.match(name):
		raise NukeError(f"{t.file}: '{name}' is not a valid configuration name")
	fields = {}
	if "optimize" in t.data:
		fields["optimize"] = t.integer("optimize", 3)
		if fields["optimize"] not in (0, 1, 2, 3):
			raise NukeError(f"{t.file}: {t.where}.optimize must be 0, 1, 2 or 3")
	if "debug-info" in t.data:
		fields["debug-info"] = t.boolean("debug-info", True)
	if "incremental" in t.data:
		fields["incremental"] = t.boolean("incremental", False)
	if "aliases" in t.data:
		fields["aliases"] = t.strings("aliases")
	if "defines" in t.data:
		fields["defines"] = t.string_map("defines")
	t.done()
	return name, fields


def _command(name, t, path):
	if not COMMAND_NAME.match(name):
		raise NukeError(f"{path}: '{name}' is not a valid command name (lowercase letters, digits and '-')")
	steps = []
	for s in t.tables("steps"):
		if "phase" in s.data:
			raise NukeError(f"{path}: {s.where}: a command's steps take the command's phase; remove 'phase'")
		s.data = dict(s.data, phase=name)
		steps.append(_step(s, path))
	spec = CommandSpec(
		name=name,
		origin=path,
		description=t.string("description", ""),
		requires=t.strings("requires"),
		projects=t.strings("projects", default=None),
		steps=tuple(steps),
	)
	t.done()
	return spec


def _project(t, folder, path, dependencies=()):
	t.where = "project"
	name = t.string("name") or os.path.basename(folder)
	if not PROJECT_NAME.match(name):
		raise NukeError(f"{path}: '{name}' is not a valid project name (letters, digits, '_', '.', '-')")
	kind = t.string("kind", "executable")
	if kind not in KINDS:
		raise NukeError(f"{path}: project.kind must be one of {', '.join(KINDS)} (got '{kind}')")
	tests = t._get("tests", "auto")
	if tests not in ("auto", True, False):
		raise NukeError(f"{path}: project.tests must be \"auto\", true or false")
	driver = t._get("driver", {})
	if not isinstance(driver, dict):
		raise NukeError(f"{path}: project.driver must be a table (what the language's driver reads)")
	spec = ProjectSpec(
		name=name,
		dir=folder,
		file=path,
		kind=kind,
		description=t.string("description", ""),
		language=t.string("language", "c4" if kind in ("executable", "module") else ""),
		group=t.string("group", ""),
		depends=t.strings("depends"),
		sources=t.strings("sources", ("src",)),
		includes=t.strings("includes"),
		defines=t.string_map("defines"),
		links=t.strings("links"),
		compiler_args=t.strings("compiler-args"),
		subsystem=t.string("subsystem", "console"),
		tests=tests,
		output=t.string("output"),
		working_dir=t.string("working-dir", "."),
		copies=tuple(_copy(c) for c in t.tables("copy")),
		steps=tuple(_step(s, path) for s in t.tables("steps")),
		config_overrides=dict(_override(name, o) for name, o in t.subtables("config")),
		scripts=t.strings("scripts"),
		driver_options=driver,
		dependencies=tuple(dependencies),
	)
	t.done()
	if kind != "custom" and spec.output:
		raise NukeError(f"{path}: project.output is only for kind = \"custom\" (C4 projects build {name}.exe)")
	if spec.subsystem not in SUBSYSTEMS:
		raise NukeError(f"{path}: project.subsystem must be one of {', '.join(SUBSYSTEMS)} (got '{spec.subsystem}')")
	if spec.subsystem != "console" and not (kind == "executable" and spec.language == "c4"):
		raise NukeError(f"{path}: project.subsystem is for C4 executables (a {kind} in {spec.language or 'no language'} links no program)")
	if kind == "module" and spec.language != "c4":
		raise NukeError(f"{path}: a module is C4 source; a library in {spec.language} is kind = \"library\"")
	if kind == "library" and spec.language in ("", "c4"):
		raise NukeError(f"{path}: kind = \"library\" is for another language (language = \"rust\", ...); a C4 library is a module")
	return spec


def _copy(t):
	source = t.string("from")
	dest = t.string("to")
	exclude = t.strings("exclude")
	t.done()
	if not source:
		raise NukeError(f"{t.file}: {t.where} needs 'from' (a file or folder, relative to the project)")
	return CopySpec(source, dest if dest is not None else os.path.basename(source.rstrip("/\\")), tuple(exclude))


def _override(name, t):
	spec = ConfigOverride(
		defines=t.string_map("defines"),
		compiler_args=t.strings("compiler-args"),
		links=t.strings("links"),
	)
	t.done()
	return name, spec


def _step(t, path):
	run = t._get("run", None)
	if run is not None and not (isinstance(run, str) or (isinstance(run, list) and all(isinstance(a, str) for a in run))):
		raise NukeError(f"{path}: {t.where}.run must be a list of arguments or a command-line string")
	python = t.string("python")
	if (run is None) == (python is None):
		raise NukeError(f"{path}: {t.where} needs exactly one of 'run' or 'python'")
	phase = t.string("phase")
	if not phase:
		raise NukeError(f"{path}: {t.where} needs a 'phase' (e.g. \"pre-build\", \"post-build\", or a command name)")
	spec = StepSpec(
		name=t.string("name") or _default_step_name(run, python),
		phase=phase,
		origin=path,
		run=tuple(run) if isinstance(run, list) else run,
		python=python,
		cwd=t.string("cwd"),
		env=t.string_map("env"),
		inputs=t.strings("inputs"),
		outputs=t.strings("outputs"),
		configs=t.strings("configs"),
		description=t.string("description", ""),
	)
	t.done()
	return spec


def _default_step_name(run, python):
	if python:
		return python.rpartition(":")[2]
	words = run.split() if isinstance(run, str) else list(run)
	for word in words:
		name = os.path.basename(word.replace("\\", "/"))
		if name and "${" not in name and not name.startswith("-"):
			return name
	return "run"
