"""What IED's language servers read while the workspace develops a package source (IED docs/31 section 7): each
language driver's editor(folder, contexts) files in build/develop/, made from all the workspace's projects of its
language - for Go a go.work with the clones' modules they require and gopls's options naming it, for Rust Cargo's
[patch] and rust-analyzer's options naming it. The language plugins' server profiles read those options
(initializationOptionsFile), and IED starts a server again when its file changes. Written on every command, so a
`nuke package develop` reaches the editor at once; `nuke package lock` removes build/develop/."""

import os

from . import fsutil, languages
from . import packages
from .errors import NukeError, StepFailed


def write(workspace, log):
	"""The editor files of every language whose driver has some, for the sources the workspace develops."""
	if not packages.active(workspace.spec) or not workspace.checkouts():
		return
	folder = workspace.develop_root()
	for language, projects in _driven(workspace).items():
		package = workspace.language_packages().get(language)
		editor = getattr(languages.load_driver(package), "editor", None) if package and package.module else None
		if editor is None:
			continue
		contexts = [_context(workspace, project, folder, log) for project in projects]
		try:
			files = editor(folder, contexts)
		except (NukeError, StepFailed, OSError, ValueError) as error:
			log.warn(f"the {language} language server's develop files are not written: {error}")
			continue
		for name, text in files.items():
			_keep(fsutil.join(folder, name), text)


def _driven(workspace):
	"""language -> the workspace's projects in it, in load order."""
	found = {}
	for project in workspace.projects.values():
		if project.is_driven:
			found.setdefault(project.language, []).append(project)
	return found


def _context(workspace, project, folder, log):
	return languages.DriverContext(project, None, "x64", "windows", "", "", log,
		settings=workspace.spec.language_settings.get(project.language, {}), workspace_dir=workspace.dir,
		checkouts=workspace.checkouts(), develop_dir=fsutil.join(folder, project.name))


def _keep(path, text):
	if text is not None:
		fsutil.write_if_changed(path, text)
	elif os.path.isfile(path):
		os.remove(path)
