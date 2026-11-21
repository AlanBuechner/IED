"""Build-derived language-server options for IED: after every successful compile the include folders, defines,
toolchain and build description the compiler saw are written to .ied/local/lsp-options/<name>.json, so the
editor's diagnostics match a real build (IED docs/03, docs/06). The file is machine-local (absolute paths)."""

import glob
import json
import os

from .. import fsutil
from . import compile as c4compile

OPTIONS_DIR = ".ied/local/lsp-options"


def options_path(workspace, name):
	return fsutil.join(workspace.dir, OPTIONS_DIR, f"{name}.json")


def script_scopes(session, project):
	"""A script host's plugin folders as the language server's script scopes (C4LSP's `scripts`): each folder its
	[project.script-host] globs match, compiled alone against the program's interface (its .exe's .c4i)."""
	host = project.script_host
	if host is None or not project.is_c4:
		return []
	output = session.output(project)
	interface = os.path.splitext(output)[0] + ".c4i"
	scopes = []
	for pattern in host.folders:
		for folder in sorted(glob.glob(os.path.join(project.dir, pattern))):
			if os.path.isdir(folder):
				scopes.append({"folders": [fsutil.norm(folder)], "interface": fsutil.norm(interface), "allow": list(host.allow)})
	return scopes


def options(job, toolchain, compiler_args, scripts=()):
	include_paths = []
	for module in job.modules:
		for folder in module.sources + module.includes:
			if os.path.isdir(folder) and folder not in include_paths:
				include_paths.append(folder)
	root = job.modules[0]
	result = {
		"includePaths": include_paths,
		"defines": dict(root.defines),
		"c4Home": toolchain.dir,
		"toolchain": toolchain.version,
		"projects": job.description_path,
		"target": [arg for arg in compiler_args if arg.startswith("-m")],
		# A script host's build lowers every class's vtables (C4 docs/runtime/04): the server lowers what the build does.
		"scriptHost": "--script-host" in compiler_args,
	}
	if scripts:
		result["scripts"] = list(scripts)
	return result


def prune(workspace):
	"""Removes the options of projects the workspace no longer has - renamed, removed, or moved to a workspace of their
	own: a language server would go on analyzing the files they shared with live projects against a description no build
	writes again. Returns how many it removed."""
	folder = fsutil.join(workspace.dir, OPTIONS_DIR)
	if not os.path.isdir(folder):
		return 0
	names = {p.name.lower() for p in workspace.projects.values()}
	removed = 0
	for entry in sorted(os.listdir(folder)):
		stem, extension = os.path.splitext(entry)
		if extension == ".json" and stem.lower() not in names:
			os.remove(os.path.join(folder, entry))
			removed += 1
	return removed


def write(workspace, name, job, toolchain, compiler_args, scripts=()):
	"""Write (only when changed) and return the options file path."""
	path = options_path(workspace, name)
	fsutil.write_if_changed(path, json.dumps(options(job, toolchain, compiler_args, scripts), indent=2) + "\n")
	return path


def write_program(session, project):
	"""An executable's options, with the description they name: its test harness's, as the language server analyzes every
	program as its -test build sees it (C4 docs/tools/03): each project's tests/ is compiled there, and the
	[dev-dependencies] they include must be the program's too, or each is an error the build never has (C4 BUG-475)."""
	job = c4compile.plan(session, project, test=True)
	fsutil.write_if_changed(job.description_path, job.description_text)
	return write(session.workspace, project.name, job, session.toolchain(), c4compile.compiler_args(project, session.configuration),
		script_scopes(session, project))


def write_unbuilt(session):
	"""The options of every program of the workspace no build has written them for, with the description they name: a
	workspace just made is analyzed as it will compile from the moment it is opened. Returns how many it wrote."""
	written = 0
	for project in session.workspace.projects.values():
		unbuilt = project.package is None and project.is_c4 and project.kind == "executable"
		if not unbuilt or os.path.isfile(options_path(session.workspace, project.name)):
			continue
		write_program(session, project)
		written += 1
	return written
