"""The loaded workspace: its manifest, its projects, configurations, custom commands and steps."""

import difflib
import glob
import os
import sys
from dataclasses import replace

from . import configs, extensions, fsutil, languages
from . import packages as packages_module
from .errors import NukeError
from .log import Log
from .manifest import MANIFEST_NAME, load
from .packages import checkouts as develop_checkouts

# Never searched for projects: build output, vendored or third-party code, and hidden folders.
DISCOVERY_SKIP = {"build", "bin", "bin-int", "vendor", "third_party", "toolchains", "node_modules", "__pycache__"}


class Workspace:
	def __init__(self, spec, projects, commands, steps, packages=None):
		self.spec = spec
		self.dir = spec.dir
		self.name = spec.name
		self.projects = projects          # name -> ProjectSpec, in load order
		self.commands = commands          # name -> CommandSpec (custom commands only)
		self.steps = steps                # workspace-level StepSpecs
		self.configurations = configs.merge(spec.configurations)
		default = spec.default_configuration or configs.DEFAULT_CONFIGURATION
		self.default_configuration = configs.require(default, self.configurations)
		self._packages = packages         # language -> LanguagePackage, found on first use
		self._checkouts = None            # the sources it develops, read on first use

	def project(self, name):
		for project in self.projects.values():
			if project.name.lower() == name.lower():
				return project
		hint = difflib.get_close_matches(name, list(self.projects), n=1)
		suggestion = f" (did you mean '{hint[0]}'?)" if hint else ""
		raise NukeError(f"no project named '{name}' in workspace {self.name}{suggestion}")

	def find_project(self, name):
		return next((p for p in self.projects.values() if p.name.lower() == name.lower()), None)

	def default_project(self):
		if self.spec.default_project:
			return self.project(self.spec.default_project)
		executables = [p for p in self.projects.values() if p.kind == "executable"]
		if len(executables) == 1:
			return executables[0]
		if len(self.projects) == 1:
			return next(iter(self.projects.values()))
		return None

	def output_root(self):
		return fsutil.join(self.dir, "build")

	def develop_root(self):
		"""build/develop/: a folder per project for what its driver writes to point the language's tool at the develop
		checkouts (a go.work, Cargo's [patch]); `nuke package lock` removes it, the next build writes what is still needed."""
		return fsutil.join(self.output_root(), "develop")

	def checkouts(self):
		"""The sources the workspace develops (packages/checkouts.py)."""
		if self._checkouts is None:
			self._checkouts = develop_checkouts.of_workspace(self.spec)
		return self._checkouts

	def language_packages(self):
		"""language -> the package its projects build with (languages.py)."""
		if self._packages is None:
			self._packages = languages.find_packages(self.dir, self.spec.languages)
		return self._packages

	def language_package(self, language):
		package = self.language_packages().get(language)
		if package is None:
			raise NukeError(f"no Nuke driver for {language}: pin its package in the workspace's [languages] "
				f"({language} = \"<folder with nuke/{languages.DESCRIPTION}>\") or add its IED plugin to .ied/plugins/")
		return package


# ------------------------------------------------------------------------------------------------ #

def find_root(start):
	"""The nearest folder at or above `start` whose nuke.toml has a [workspace] table, else the nearest
	folder with a project-only nuke.toml (an implicit one-project workspace), else None."""
	folder = os.path.abspath(start)
	project_only = None
	while True:
		path = os.path.join(folder, MANIFEST_NAME)
		if os.path.isfile(path):
			manifest = load(path)
			if manifest.workspace is not None:
				return folder
			if project_only is None:
				project_only = folder
		parent = os.path.dirname(folder)
		if parent == folder:
			return project_only
		folder = parent


def load_workspace(root, package_options=None):
	"""The workspace at `root`, with the projects of the packages it uses (installed and locked first: packages/__init__.py;
	`package_options` carries the log and what to update)."""
	root_manifest = load(os.path.join(root, MANIFEST_NAME))
	spec = root_manifest.workspace or _implicit_workspace(root_manifest)
	manifests = [root_manifest] if root_manifest.project else []
	for folder in _project_folders(spec):
		if fsutil.same_path(folder, root):
			continue
		manifest = load(os.path.join(folder, MANIFEST_NAME))
		if manifest.workspace is not None:
			raise NukeError(f"{manifest.path}: a project of workspace {spec.name} cannot declare its own [workspace]")
		if manifest.project is None:
			raise NukeError(f"{manifest.path}: listed as a project of {spec.name} but has no [project] table")
		manifests.append(manifest)
	projects = _index_projects([m.project for m in manifests])
	if packages_module.active(spec):
		options = package_options or packages_module.Options(Log(messages=sys.stderr))
		for project in packages_module.sync(spec, projects, options):
			projects[project.name] = project
	packages = _complete_projects(spec, projects)
	commands = {c.name: c for c in spec.commands}
	steps = list(spec.steps) + [s for c in spec.commands for s in c.steps]
	_apply_extensions(spec, projects, commands, steps)
	return Workspace(spec, projects, commands, steps, packages)


def _implicit_workspace(manifest):
	from .manifest import WorkspaceSpec
	project = manifest.project
	return WorkspaceSpec(name=project.name, dir=project.dir, file=manifest.path, default_project=project.name)


def _index_projects(specs):
	projects = {}
	for spec in specs:
		clash = next((p for p in projects.values() if p.name.lower() == spec.name.lower()), None)
		if clash is not None:
			raise NukeError(f"two projects are named '{spec.name}': {clash.file} and {spec.file}")
		projects[spec.name] = spec
	return projects


def _complete_projects(spec, projects):
	"""Gives each project in another language the dependencies its driver adds (implicit_depends) and adds the
	language packages' library projects the workspace depends on, until nothing more is needed. Returns the language
	packages when they had to be found (None otherwise)."""
	if not any(p.is_driven for p in projects.values()) and not _unresolved(projects):
		return None
	packages = languages.find_packages(spec.dir, spec.languages)
	libraries = None
	pending = list(projects)
	while pending:
		project = _with_implicit_depends(projects[pending.pop(0)], packages)
		projects[project.name] = project
		for name in project.depends:
			if _find(projects, name) is not None:
				continue
			if libraries is None:
				libraries = _package_libraries(packages)
			library = libraries.get(name.lower())
			if library is not None:
				projects[library.name] = library
				pending.append(library.name)
	return packages


def _find(projects, name):
	return next((p for p in projects.values() if p.name.lower() == name.lower()), None)


def _unresolved(projects):
	return any(_find(projects, name) is None for p in projects.values() for name in p.depends)


def _with_implicit_depends(project, packages):
	package = packages.get(project.language) if project.is_driven else None
	implicit = getattr(languages.load_driver(package), "implicit_depends", None) if package else None
	if implicit is None:
		return project
	known = {name.lower() for name in project.depends} | {project.name.lower()}
	try:
		wanted = implicit(project)
	except ValueError as error:
		raise NukeError(f"{project.file}: {error}") from None
	extra = tuple(name for name in wanted if name.lower() not in known)
	return replace(project, depends=project.depends + extra) if extra else project


def _package_libraries(packages):
	"""name (lower case) -> ProjectSpec of every library project the packages bring; the first package wins a name."""
	libraries = {}
	for package in packages.values():
		for folder in package.library_folders():
			manifest = load(os.path.join(folder, MANIFEST_NAME))
			if manifest.workspace is not None or manifest.project is None:
				raise NukeError(f"{manifest.path}: a library of the {package.language} package must be a [project] and nothing else")
			libraries.setdefault(manifest.project.name.lower(), manifest.project)
	return libraries


def _project_folders(spec):
	"""The workspace's own project folders: never anything in its packages folder, whose projects come with packages."""
	installed = os.path.join(spec.dir, spec.packages_dir)
	if spec.projects is None:
		return discover(spec.dir, tuple(spec.exclude) + (spec.packages_dir.strip("/\\").replace("\\", "/"),))
	folders = []
	for entry in spec.projects:
		matches = _match_entry(spec.dir, entry)
		if not matches:
			raise NukeError(f"{spec.file}: workspace.projects entry '{entry}' matches no folder with a {MANIFEST_NAME}")
		if glob.has_magic(entry):
			matches = [m for m in matches if not fsutil.matches_any(os.path.relpath(m, spec.dir), spec.exclude)
				and not fsutil.is_within(m, installed)]
		folders.extend(m for m in matches if not any(fsutil.same_path(m, f) for f in folders))
	return folders


def _match_entry(root, entry):
	pattern = os.path.join(root, entry)
	candidates = glob.glob(pattern) if glob.has_magic(entry) else [pattern]
	return sorted(fsutil.norm(c) for c in candidates if os.path.isfile(os.path.join(c, MANIFEST_NAME)))


def entry_finds(root, entry, folder):
	"""Whether the projects entry `entry` (a folder or a glob) finds the project folder `folder`."""
	return any(fsutil.same_path(match, folder) for match in _match_entry(root, entry))


def discover(root, exclude=()):
	"""Every folder below `root` with a nuke.toml declaring a [project]; nested workspaces are not entered."""
	found = []
	for base, dirs, files in os.walk(root):
		relative = os.path.relpath(base, root).replace("\\", "/")
		prefix = "" if relative == "." else relative + "/"
		dirs[:] = sorted(d for d in dirs if d not in DISCOVERY_SKIP and not d.startswith(".")
			and not fsutil.matches_any(prefix + d, exclude))
		if fsutil.same_path(base, root) or MANIFEST_NAME not in files:
			continue
		manifest = load(os.path.join(base, MANIFEST_NAME))
		if manifest.workspace is not None:
			dirs[:] = []
			continue
		if manifest.project is not None:
			found.append(fsutil.norm(base))
	return found


def _apply_extensions(spec, projects, commands, steps):
	sources = [(os.path.join(spec.dir, s), None) for s in spec.scripts]
	for project in projects.values():
		sources.extend((os.path.join(project.dir, s), project.name) for s in project.scripts)
	for path, owner in sources:
		registry = extensions.load_script(path, owner)
		for command in registry.commands:
			if command.name in commands:
				raise NukeError(f"command '{command.name}' is defined twice: {commands[command.name].origin} and {command.origin}")
			commands[command.name] = command
		for project_name, step in registry.steps:
			if project_name is None:
				steps.append(step)
				continue
			target = next((p for p in projects.values() if p.name.lower() == project_name.lower()), None)
			if target is None:
				raise NukeError(f"{step.origin}: step '{step.name}' names unknown project '{project_name}'")
			projects[target.name] = replace(target, steps=target.steps + (step,))
