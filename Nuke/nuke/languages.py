"""Language drivers (IED docs/26 section 6): a project in a language other than C4 names its `language`, and Nuke
builds, tests and runs it through that language's driver - Python in the language's package, an IED plugin folder with
nuke/nuke-language.toml - which runs the language's own tool (cargo, go), or is the language's build itself (C++), and
hands back the artifacts Nuke copies into build/bin. The tool's manifests, lockfiles and caches stay its own. A
language's package is the one the workspace pins in [languages], else one of the workspace's own plugins
(.ied/plugins/<id>/), else one of IED's (plugins/ beside Nuke, or the folders NUKE_LANGUAGE_PACKAGES names).

A package may also bring library projects (`libraries` in its description: folders with a nuke.toml [project]); a
workspace that depends on one by name gets it as one of its projects, built into the workspace's own build/ folder. A
package may have no driver (no `module`): it then names its projects' preset and brings libraries only - C4's
(plugins/c4/nuke), since Nuke compiles C4 itself (IED docs/23 N-23).

A driver module (or package: a module named __init__.py loads with its folder as a Python package) defines:
	build(ctx) -> [paths]     build the project; the artifacts to copy into its bin folder
	program(ctx) -> name      the file in the bin folder `nuke run` starts (None: nothing to run)
	test(ctx) -> exit code    run its tests (after Nuke built the project, with `build-before-test = true`)
	clean(ctx)                optional: what Nuke's clean should do beyond removing its own folders
	has_tests(ctx) -> bool    optional (default True)
	implicit_depends(spec)    optional: projects a project needs besides its `depends` (names), added to them
	variables(ctx) -> dict    optional: values manifests read as ${<language>.<name>} (ctx: a WorkspaceContext)
	editor(folder, contexts)  optional: while the workspace develops a package source, {file name: text, or None to
	                          remove it} in `folder` (build/develop/) for the language server in IED, from the contexts of
	                          all the workspace's projects in the language (develop_files.py)

A driver reports a project it cannot build with ctx.problem(message) (or, without a context, a ValueError), and a
failed compile with ctx.fail(what, code).
"""

import glob
import importlib.util
import os
import subprocess
import sys
import threading
import tomllib
from dataclasses import dataclass, field

from . import fsutil, process
from .errors import NukeError, StepFailed

DESCRIPTION = "nuke-language.toml"
PACKAGES_ENV = "NUKE_LANGUAGE_PACKAGES"
BUILT_IN_PACKAGES = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "plugins")
DESCRIPTION_KEYS = {"id", "module", "preset", "libraries", "build-before-test"}


@dataclass(frozen=True)
class LanguagePackage:
	language: str
	folder: str            # the package: an IED plugin folder
	module: str            # the driver's Python file; None for a package without one
	preset: str            # the IED task preset of the language's Nuke projects (`<language>.nuke`)
	pinned: bool = False   # named by the workspace's [languages]
	libraries: tuple = ()  # globs, relative to the package, of the library projects it brings
	build_before_test: bool = False   # `nuke test` builds a project (and what it depends on) before test(ctx)

	def library_folders(self):
		"""The folders of the package's library projects (each holding a nuke.toml)."""
		found = []
		for pattern in self.libraries:
			for folder in sorted(glob.glob(os.path.join(self.folder, pattern))):
				if os.path.isfile(os.path.join(folder, "nuke.toml")) and not any(fsutil.same_path(folder, f) for f in found):
					found.append(fsutil.norm(folder))
		return found


def read_package(folder, pinned=False):
	"""The package in `folder`, or None when it has no nuke/nuke-language.toml."""
	description = os.path.join(folder, "nuke", DESCRIPTION)
	if not os.path.isfile(description):
		return None
	try:
		with open(description, "rb") as f:
			data = tomllib.load(f)
	except (OSError, tomllib.TOMLDecodeError) as error:
		raise NukeError(f"{description}: {error}") from None
	table = data.get("language")
	if not isinstance(table, dict) or not isinstance(table.get("id"), str) or not isinstance(table.get("module", ""), str):
		raise NukeError(f"{description}: needs [language] with an id (and the driver's module, when it has one)")
	module = os.path.join(folder, "nuke", table["module"]) if "module" in table else None
	if module and not os.path.isfile(module):
		raise NukeError(f"{description}: its driver {table['module']} is not there")
	unknown = sorted(set(table) - DESCRIPTION_KEYS)
	if unknown:
		raise NukeError(f"{description}: unknown key(s) in [language]: {', '.join(unknown)}")
	libraries = table.get("libraries", [])
	if not isinstance(libraries, list) or not all(isinstance(entry, str) for entry in libraries):
		raise NukeError(f"{description}: [language].libraries must be a list of folders or globs, relative to the package")
	before_test = table.get("build-before-test", False)
	if not isinstance(before_test, bool):
		raise NukeError(f"{description}: [language].build-before-test must be true or false")
	return LanguagePackage(table["id"], fsutil.norm(folder), fsutil.norm(module) if module else None, table.get("preset", f"{table['id']}.nuke"),
		pinned, tuple(libraries), before_test)


def packages_in(parent):
	"""The packages among the folders of `parent`."""
	if not os.path.isdir(parent):
		return []
	found = []
	for name in sorted(os.listdir(parent)):
		package = read_package(os.path.join(parent, name))
		if package:
			found.append(package)
	return found


def search_folders(workspace_dir):
	"""Where packages are looked for after the workspace's pins: its own plugins, then IED's."""
	extra = [p for p in os.environ.get(PACKAGES_ENV, "").split(os.pathsep) if p]
	return [os.path.join(workspace_dir, ".ied", "plugins")] + extra + [BUILT_IN_PACKAGES]


def find_packages(workspace_dir, pins):
	"""language -> package: the workspace's pins first (`pins`: language -> folder, relative to the workspace), then
	the first package of each language in the search folders."""
	packages = {}
	for language, folder in pins.items():
		path = folder if os.path.isabs(folder) else os.path.join(workspace_dir, folder)
		package = read_package(path, pinned=True)
		if package is None:
			raise NukeError(f"[languages] {language} = \"{folder}\": no nuke/{DESCRIPTION} there")
		if package.language != language:
			raise NukeError(f"[languages] {language} = \"{folder}\": that package is for {package.language}")
		packages[language] = package
	for parent in search_folders(workspace_dir):
		for package in packages_in(parent):
			packages.setdefault(package.language, package)
	return packages


_drivers = {}


def load_driver(package):
	"""The driver module of `package`, imported once; a module named __init__.py is imported as a package, so the
	driver can be split into modules of its own."""
	if package.module is None:
		raise NukeError(f"the {package.language} package ({package.folder}) has no Nuke driver: its nuke/{DESCRIPTION} names no module")
	if package.module not in _drivers:
		name = f"nuke_language_{package.language.replace('-', '_')}"
		folders = [os.path.dirname(package.module)] if os.path.basename(package.module) == "__init__.py" else None
		spec = importlib.util.spec_from_file_location(name, package.module, submodule_search_locations=folders)
		module = importlib.util.module_from_spec(spec)
		try:
			if folders:
				sys.modules[name] = module
			spec.loader.exec_module(module)
		except NukeError:
			raise
		except Exception as error:
			raise NukeError(f"{package.module}: the {package.language} driver does not load: {error}") from None
		for function in ("build", "program", "test"):
			if not callable(getattr(module, function, None)):
				raise NukeError(f"{package.module}: the {package.language} driver has no {function}(ctx)")
		_drivers[package.module] = module
	return _drivers[package.module]


@dataclass
class WorkspaceContext:
	"""What a driver's variables(ctx) gets: the workspace's folder and its [languages.<id>] settings."""
	workspace_dir: str
	settings: dict
	log: object = None


@dataclass
class DriverContext:
	"""What a driver's functions get: the project, the configuration, its folders, the command line's extra arguments
	and test filter, the workspace's settings for the language, the contexts of what the project depends on, and ways
	to run the language's tool with its output in Nuke's log."""
	project: object
	configuration: object
	arch: str
	system: str
	bin_dir: str
	int_dir: str
	log: object
	args: list = field(default_factory=list)
	test_filter: str = None
	settings: dict = field(default_factory=dict)   # the workspace's [languages.<id>] table
	workspace_dir: str = ""
	jobs: int = None                               # `nuke -j N`: how many tool processes may run at once (None: the driver's choice)
	dependencies: object = None                    # () -> [DriverContext] of the projects in `depends`
	expander: object = None                        # (text) -> text with the project's ${variables}
	checkouts: tuple = ()                          # packages.checkouts.Checkout of each source the workspace develops
	develop_dir: str = ""                          # build/develop/<project>: where develop_file() keeps its files

	@property
	def root(self):
		return self.project.dir

	@property
	def options(self):
		"""The project's [project.driver] table."""
		return self.project.driver_options

	@property
	def depends(self):
		"""The contexts of the projects this one depends on, in `depends` order."""
		return self.dependencies() if self.dependencies else []

	def expand(self, text):
		"""`text` with the project's ${variables} (the steps' ones, the language packages' included)."""
		return self.expander(text) if self.expander else text

	def develop_file(self, name, text):
		"""Keeps `text` as the file `name` in the project's develop folder, for what points the language's tool at the
		develop checkouts (written only when it changed), and returns its path; None removes the file and returns None."""
		path = os.path.normpath(os.path.join(self.develop_dir, name))
		if text is None:
			if os.path.isfile(path):
				os.remove(path)
			return None
		fsutil.write_if_changed(path, text)
		return path

	def problem(self, message):
		"""Stops Nuke with `message` as a problem of the workspace or a manifest (exit code 1)."""
		raise NukeError(message)

	def fail(self, what, code=1):
		"""Stops Nuke because `what` (a compile, a link) failed, with its exit code."""
		raise StepFailed(what, code)

	def tool(self, name):
		"""The command of the language's tool: NUKE_TOOL_<NAME> when set (tests, a tool off PATH), else its name."""
		return os.environ.get(f"NUKE_TOOL_{name.upper()}", name)

	def run(self, argv, cwd=None, env=None, check=True):
		"""Runs `argv` with its output streamed to the log; its exit code, or StepFailed when `check` and not 0."""
		code = process.run(list(argv), self.log, cwd=cwd or self.root, env=env)
		if check and code != 0:
			raise StepFailed(f"{self.project.name}: {os.path.basename(argv[0])}", code)
		return code

	def capture(self, argv, cwd=None, env=None, check=True):
		"""Runs `argv` with its stderr streamed to the log and its stdout kept: (exit code, stdout)."""
		code, out = run_capturing(list(argv), self.log, cwd=cwd or self.root, env=env)
		if check and code != 0:
			raise StepFailed(f"{self.project.name}: {os.path.basename(argv[0])}", code)
		return code, out


def run_capturing(argv, log, cwd=None, env=None):
	"""Runs `argv`: stderr streamed to `log` line by line, stdout returned whole."""
	log.detail(f"run: {subprocess.list2cmdline(argv)}" + (f"  (in {cwd})" if cwd else ""))
	merged = dict(os.environ, **env) if env else None
	try:
		proc = subprocess.Popen(argv, cwd=cwd, env=merged, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
			text=True, encoding="utf-8", errors="replace", bufsize=1)
	except FileNotFoundError as error:
		log.line(f"nuke: cannot start {argv[0]}: {error.strerror}")
		return 127, ""
	with proc:
		streamed = threading.Thread(target=lambda: [log.write(text) for text in proc.stderr], daemon=True)
		streamed.start()
		out = proc.stdout.read()
		streamed.join()
	return proc.returncode, out


def stage(artifacts, bin_dir, log, name):
	"""Copies each artifact into `bin_dir` when it is newer than the copy there."""
	copied = sum(fsutil.copy_if_newer(path, os.path.join(bin_dir, os.path.basename(path))) for path in artifacts
		if os.path.isfile(path) and not fsutil.same_path(os.path.dirname(path), bin_dir))
	if copied:
		log.note(f"{name}: copied {copied} file(s) into {bin_dir}")
