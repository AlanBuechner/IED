"""One invocation's state: the workspace, the chosen configuration/arch/system, the toolchain (resolved on first
use), the output layout build/{bin,int}/{config}-{system}-{arch}/{project} and the variables steps see."""

import os
import sys
import time

from . import fsutil, graph, languages, toolchains
from .errors import NukeError
from .variables import Variables, expand


class Session:
	def __init__(self, workspace, configuration, arch, system, log, store, args=(), test_filter=None, jobs=None):
		self.workspace = workspace
		self.configuration = configuration
		self.arch = arch
		self.system = system
		self.log = log
		self.store = store
		self.args = list(args)
		self.test_filter = test_filter
		self.jobs = jobs               # -j N: how many processes a driver may run at once (None: its own choice)
		self.json = False              # list/info print JSON
		self.done = set()              # (command, project name or None) already executed
		self.trace = []                # (name, start, end) for build/int/.../nuke-trace.json
		self.test_results = []         # (project name, exit code, results.json path) per harness run
		self._toolchain = None
		self._language_values = {}     # language -> its driver's variables(ctx)

	# --- toolchain -------------------------------------------------------------------------------

	def toolchain(self):
		if self._toolchain is None:
			self._toolchain = self.store.resolve(self.workspace.spec.toolchain, self.workspace.dir)
			if not self.workspace.spec.toolchain:
				self.log.warn(f"workspace {self.workspace.name} pins no toolchain; using the latest installed ({self._toolchain.version}). "
					f"Pin it with `nuke toolchain use {self._toolchain.version}`")
		return self._toolchain

	def use_toolchain(self, toolchain):
		self._toolchain = toolchain

	# --- layout ----------------------------------------------------------------------------------

	def triple(self):
		return f"{self.configuration.name}-{self.system}-{self.arch}"

	def bin_root(self):
		return fsutil.join(self.workspace.output_root(), "bin", self.triple())

	def int_root(self):
		return fsutil.join(self.workspace.output_root(), "int", self.triple())

	def bin_dir(self, project):
		return fsutil.join(self.bin_root(), project.name)

	def int_dir(self, project):
		return fsutil.join(self.int_root(), project.name)

	# --- language drivers ------------------------------------------------------------------------

	def driver(self, project):
		"""The driver module of a project in another language than C4."""
		return languages.load_driver(self.workspace.language_package(project.language))

	def driver_context(self, project):
		return languages.DriverContext(project, self.configuration, self.arch, self.system, self.bin_dir(project), self.int_dir(project),
			self.log, list(self.args), self.test_filter, settings=self.workspace.spec.language_settings.get(project.language, {}),
			workspace_dir=self.workspace.dir, jobs=self.jobs,
			dependencies=lambda: [self.driver_context(self.workspace.find_project(name)) for name in project.depends],
			expander=lambda text: expand(text, self.variables(project)))

	def builds_before_test(self, project):
		"""A project whose language's package asks for it is built, with what it depends on, before its tests run."""
		return project.is_driven and self.workspace.language_package(project.language).build_before_test

	def output(self, project):
		"""The file `nuke run` starts: a C4 executable's .exe, the program a driven project's driver names in its bin
		folder, or a custom project's `output` (None if it has none)."""
		if project.is_c4:
			return fsutil.join(self.bin_dir(project), f"{project.name}.exe")
		if project.is_driven:
			name = self.driver(project).program(self.driver_context(project))
			return fsutil.join(self.bin_dir(project), name) if name else None
		if not project.output:
			return None
		path = expand(project.output, self._project_variables(project, with_output=False))
		return fsutil.norm(path if os.path.isabs(path) else os.path.join(project.dir, path))

	def test_output(self, project):
		return fsutil.join(self.bin_dir(project), f"{project.name}-tests.exe")

	def c4_modules(self, project):
		return graph.c4_modules(self.workspace, project)

	def has_tests(self, project):
		"""C4 projects: `tests = true`, or "auto" and a tests/ folder with .cf/.ch files under a source folder.
		Any project: steps in the "test" phase."""
		if any(s.phase == "test" for s in project.steps):
			return True
		if project.tests is False:
			return False
		if project.is_driven:
			has_tests = getattr(self.driver(project), "has_tests", None)
			return has_tests(self.driver_context(project)) if has_tests else True
		if not project.is_c4:
			return False
		if project.tests is True:
			return True
		for source in project.sources:
			folder = os.path.join(project.dir, source, "tests")
			if fsutil.files_under(folder, {".cf", ".ch"}):
				return True
		return False

	# --- variables -------------------------------------------------------------------------------

	def variables(self, project=None):
		root = Variables({
			"workspace.dir": self.workspace.dir,
			"workspace.name": self.workspace.name,
			"config": self.configuration.name,
			"arch": self.arch,
			"system": self.system,
			"build.dir": self.workspace.output_root(),
			"bin.root": self.bin_root(),
			"int.root": self.int_root(),
			"python": fsutil.norm(sys.executable),
			"nuke": fsutil.norm(os.path.dirname(os.path.abspath(__file__))),
			"nuke.home": toolchains.nuke_home(),
			"toolchains": self.store.root,
			"args": list(self.args),
			"toolchain.dir": lambda: self.toolchain().dir,
			"toolchain.version": lambda: self.toolchain().version,
			"c4": lambda: self.toolchain().compiler_path(),
		}, fallback=self._language_variable)
		return self._project_variables(project, base=root) if project else root

	def _project_variables(self, project, base=None, with_output=True):
		values = {
			"project.name": project.name,
			"project.dir": project.dir,
			"bin": self.bin_dir(project),
			"int": self.int_dir(project),
		}
		if with_output:
			values["output"] = lambda: self.output(project) or _no_output(project)
		return (base or self.variables()).child(values)

	def _language_variable(self, name):
		"""${<language>.<name>}: a value the language's driver gives (variables(ctx)); KeyError when none does."""
		language, _, key = name.partition(".")
		if not key:
			raise KeyError(name)
		if language not in self._language_values:
			package = self.workspace.language_packages().get(language)
			function = getattr(languages.load_driver(package), "variables", None) if package else None
			context = languages.WorkspaceContext(self.workspace.dir, self.workspace.spec.language_settings.get(language, {}), self.log)
			self._language_values[language] = function(context) if function else {}
		return self._language_values[language][key]

	# --- tracing ---------------------------------------------------------------------------------

	def timed(self, name):
		return _Timer(self, name)


def _no_output(project):
	raise NukeError(f"project '{project.name}' has no output (a custom project names it with `output = ...`)")


class _Timer:
	def __init__(self, session, name):
		self.session = session
		self.name = name

	def __enter__(self):
		self.start = time.time()
		return self

	def __exit__(self, *exc):
		self.session.trace.append((self.name, self.start, time.time()))
		return False
