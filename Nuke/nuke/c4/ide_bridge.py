"""Build-derived language-server options for IED: after every successful compile the include folders, defines,
toolchain and build description the compiler saw are written to .ied/local/lsp-options/<name>.json, so the
editor's diagnostics match a real build (IED docs/03, docs/06). The file is machine-local (absolute paths)."""

import json
import os

from .. import fsutil

OPTIONS_DIR = ".ied/local/lsp-options"


def options_path(workspace, name):
	return fsutil.join(workspace.dir, OPTIONS_DIR, f"{name}.json")


def options(job, toolchain, compiler_args):
	include_paths = []
	for module in job.modules:
		for folder in module.sources + module.includes:
			if os.path.isdir(folder) and folder not in include_paths:
				include_paths.append(folder)
	root = job.modules[0]
	return {
		"includePaths": include_paths,
		"defines": dict(root.defines),
		"c4Home": toolchain.dir,
		"toolchain": toolchain.version,
		"projects": job.description_path,
		"target": [arg for arg in compiler_args if arg.startswith("-m")],
	}


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


def write(workspace, name, job, toolchain, compiler_args):
	"""Write (only when changed) and return the options file path."""
	path = options_path(workspace, name)
	fsutil.write_if_changed(path, json.dumps(options(job, toolchain, compiler_args), indent=2) + "\n")
	return path
