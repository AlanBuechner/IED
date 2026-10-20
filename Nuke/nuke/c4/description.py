"""The build description C4.exe reads with --projects (C4/docs/language/15-projects.md): the root program and
every C4 module it depends on, each with its sources, include folders, defines and dependencies."""

import os
from dataclasses import dataclass

from .. import fsutil, graph


@dataclass(frozen=True)
class Module:
	name: str
	sources: tuple       # absolute files or folders
	includes: tuple      # absolute folders
	depends: tuple
	defines: dict


def module_for(workspace, project, configuration):
	override = project.config_overrides.get(configuration.name)
	defines = dict(project.defines)
	if override:
		defines.update(override.defines)
	defines[configuration.name.upper()] = "1"
	defines["PROJECT_NAME"] = project.name
	depends = tuple(d.name for d in (workspace.find_project(n) for n in project.depends) if d.is_c4)
	return Module(
		name=project.name,
		sources=tuple(_absolute(project.dir, s) for s in project.sources),
		includes=tuple(_absolute(project.dir, i) for i in project.includes),
		depends=depends,
		defines=defines,
	)


def modules_for(workspace, root, configuration):
	return [module_for(workspace, p, configuration) for p in graph.c4_modules(workspace, root)]


def _absolute(base, path):
	return fsutil.norm(path if os.path.isabs(path) else os.path.join(base, path))


def _quote(text):
	return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _list(items):
	return "[" + ", ".join(_quote(i) for i in items) + "]"


def render(root_name, modules):
	lines = ["# Written by nuke for every build; see C4/docs/language/15-projects.md.", f"root = {_quote(root_name)}", ""]
	for module in modules:
		lines += [
			"[[project]]",
			f"name = {_quote(module.name)}",
			f"sources = {_list(module.sources)}",
			f"includes = {_list(module.includes)}",
			f"depends = {_list(module.depends)}",
			"[project.defines]",
		]
		lines += [f"{name} = {_quote(value)}" for name, value in module.defines.items()]
		lines.append("")
	return "\n".join(lines)


def tracked_files(modules):
	"""Every file a compile of `modules` reads from the workspace (sources and include folders)."""
	files = []
	for module in modules:
		for path in module.sources + module.includes:
			files.extend(fsutil.files_under(path) if os.path.isdir(path) else [path])
	return sorted(set(files))
