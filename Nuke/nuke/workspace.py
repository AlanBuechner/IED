"""The loaded workspace: its manifest, its projects, configurations, custom commands and steps."""

import difflib
import glob
import os
from dataclasses import replace

from . import configs, extensions, fsutil
from .errors import NukeError
from .manifest import MANIFEST_NAME, load

# Never searched for projects: build output, vendored or third-party code, and hidden folders.
DISCOVERY_SKIP = {"build", "bin", "bin-int", "vendor", "third_party", "toolchains", "node_modules", "__pycache__"}


class Workspace:
	def __init__(self, spec, projects, commands, steps):
		self.spec = spec
		self.dir = spec.dir
		self.name = spec.name
		self.projects = projects          # name -> ProjectSpec, in load order
		self.commands = commands          # name -> CommandSpec (custom commands only)
		self.steps = steps                # workspace-level StepSpecs
		self.configurations = configs.merge(spec.configurations)
		default = spec.default_configuration or configs.DEFAULT_CONFIGURATION
		self.default_configuration = configs.require(default, self.configurations)

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


def load_workspace(root):
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
	commands = {c.name: c for c in spec.commands}
	steps = list(spec.steps) + [s for c in spec.commands for s in c.steps]
	_apply_extensions(spec, projects, commands, steps)
	return Workspace(spec, projects, commands, steps)


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


def _project_folders(spec):
	if spec.projects is None:
		return discover(spec.dir, spec.exclude)
	folders = []
	for entry in spec.projects:
		matches = _match_entry(spec.dir, entry)
		if not matches:
			raise NukeError(f"{spec.file}: workspace.projects entry '{entry}' matches no folder with a {MANIFEST_NAME}")
		folders.extend(m for m in matches if not any(fsutil.same_path(m, f) for f in folders))
	return folders


def _match_entry(root, entry):
	pattern = os.path.join(root, entry)
	candidates = glob.glob(pattern) if glob.has_magic(entry) else [pattern]
	return sorted(fsutil.norm(c) for c in candidates if os.path.isfile(os.path.join(c, MANIFEST_NAME)))


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
