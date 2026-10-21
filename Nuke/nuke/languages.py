"""Language drivers (IED docs/26 section 6): a project in a language other than C4 names its `language`, and Nuke
builds, tests and runs it through that language's driver - Python in the language's package, an IED plugin folder with
nuke/nuke-language.toml - which runs the language's own tool (cargo, go) and hands back the artifacts Nuke copies into
build/bin. The tool's manifests, lockfiles and caches stay its own. A language's package is the one the workspace pins
in [languages], else one of the workspace's own plugins (.ied/plugins/<id>/), else one of IED's (plugins/ beside Nuke,
or the folders NUKE_LANGUAGE_PACKAGES names).

A driver module defines:
	build(ctx) -> [paths]     build the project; the artifacts to copy into its bin folder
	program(ctx) -> name      the file in the bin folder `nuke run` starts (None: nothing to run)
	test(ctx) -> exit code    run its tests
	clean(ctx)                optional: what Nuke's clean should do beyond removing its own folders
	has_tests(ctx) -> bool    optional (default True)
"""

import importlib.util
import os
import subprocess
import threading
import tomllib
from dataclasses import dataclass, field

from . import fsutil, process
from .errors import NukeError, StepFailed

DESCRIPTION = "nuke-language.toml"
PACKAGES_ENV = "NUKE_LANGUAGE_PACKAGES"
BUILT_IN_PACKAGES = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "plugins")


@dataclass(frozen=True)
class LanguagePackage:
	language: str
	folder: str            # the package: an IED plugin folder
	module: str            # the driver's Python file
	preset: str            # the IED task preset of the language's Nuke projects (`<language>.nuke`)
	pinned: bool = False   # named by the workspace's [languages]


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
	if not isinstance(table, dict) or not isinstance(table.get("id"), str) or not isinstance(table.get("module"), str):
		raise NukeError(f"{description}: needs [language] with an id and the driver's module")
	module = os.path.join(folder, "nuke", table["module"])
	if not os.path.isfile(module):
		raise NukeError(f"{description}: its driver {table['module']} is not there")
	unknown = sorted(set(table) - {"id", "module", "preset"})
	if unknown:
		raise NukeError(f"{description}: unknown key(s) in [language]: {', '.join(unknown)}")
	return LanguagePackage(table["id"], fsutil.norm(folder), fsutil.norm(module), table.get("preset", f"{table['id']}.nuke"), pinned)


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
	"""The driver module of `package`, imported once."""
	if package.module not in _drivers:
		spec = importlib.util.spec_from_file_location(f"nuke_language_{package.language.replace('-', '_')}", package.module)
		module = importlib.util.module_from_spec(spec)
		try:
			spec.loader.exec_module(module)
		except Exception as error:
			raise NukeError(f"{package.module}: the {package.language} driver does not load: {error}") from None
		for name in ("build", "program", "test"):
			if not callable(getattr(module, name, None)):
				raise NukeError(f"{package.module}: the {package.language} driver has no {name}(ctx)")
		_drivers[package.module] = module
	return _drivers[package.module]


@dataclass
class DriverContext:
	"""What a driver's functions get: the project, the configuration, its folders, the command line's extra arguments
	and test filter, and ways to run the language's tool with its output in Nuke's log."""
	project: object
	configuration: object
	arch: str
	system: str
	bin_dir: str
	int_dir: str
	log: object
	args: list = field(default_factory=list)
	test_filter: str = None

	@property
	def root(self):
		return self.project.dir

	@property
	def options(self):
		"""The project's [project.driver] table."""
		return self.project.driver_options

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
	copied = sum(fsutil.copy_if_newer(path, os.path.join(bin_dir, os.path.basename(path))) for path in artifacts if os.path.isfile(path))
	if copied:
		log.note(f"{name}: copied {copied} file(s) into {bin_dir}")
