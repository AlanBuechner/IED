"""project: add an existing folder to the workspace as a project, take a project out of it, or move one, editing the
manifests in place (tomledit.py); with --ied the IED solution follows. IED's Solution Explorer runs these through the
nuke plugin's project system (IED docs/03 "Managing projects")."""

import argparse
import os
import shutil

from .. import fsutil, languages, tomledit
from .. import workspace as workspaces
from ..errors import NukeError
from ..manifest import KINDS, MANIFEST_NAME, PROJECT_NAME, load
from . import base
from .init import LANGUAGE, ied_solution

USAGE = """nuke project add FOLDER [--name NAME] [--language ID] [--kind KIND] [--ied]
  make an existing folder a project of the workspace: its nuke.toml is written when it has none (the language C4 unless
  --language says; the kind from what is there: a C4 folder with a main.cf is an executable, without one a module, a
  language Nuke builds through a driver an executable, any other a custom project) and the workspace lists it
nuke project remove NAME [--deleting] [--ied]
  take a project out of the workspace, its folder kept (an entry of [workspace].projects goes, or the folder joins
  exclude); refused while another project depends on it; --deleting: the caller deletes the folder next, so nothing
  is excluded
nuke project move PROJECT TO [--ied]
  move a project (by name or folder) to the folder TO and fix the workspace's entries for it; when its folder was
  moved there already (by an IDE whose open files followed), only the entries
  --ied: write the IED solution again afterwards"""


class Project(base.Command):
	name = "project"
	description = "Add a folder as a project, remove a project, or move one (nuke project --help)"
	usage = USAGE
	takes_args = True
	needs_workspace = False
	policy = base.NONE

	def main(self, cli, args):
		if not args or args[0] in ("-h", "--help", "help"):
			cli.log.line(USAGE)
			return 0
		verb, rest = args[0], args[1:]
		actions = {"add": _add, "remove": _remove, "move": _move}
		if verb not in actions:
			raise NukeError(f"nuke project {verb}: not a verb; use add, remove or move (nuke project --help)")
		root = actions[verb](cli, rest)
		if "--ied" in rest:
			ied_solution(cli, workspaces.load_workspace(root))
		return 0


def _parse(prog, args, build):
	parser = argparse.ArgumentParser(prog=f"nuke project {prog}", add_help=False)
	build(parser)
	parser.add_argument("--ied", action="store_true")
	try:
		return parser.parse_args(args)
	except SystemExit:
		raise NukeError(USAGE) from None


def _loaded(cli):
	if cli.workspace is None:
		raise NukeError(f"no workspace in {cli.cwd} or above it (nuke init makes one)")
	return cli.workspace


def _relative(path, root):
	return fsutil.norm(os.path.relpath(path, root))


def _inside(path, root):
	return fsutil.is_within(path, root) and not fsutil.same_path(path, root)


def _root_text(workspace_dir):
	path = os.path.join(workspace_dir, MANIFEST_NAME)
	with open(path, encoding="utf-8") as f:
		return path, f.read()


def _save(path, text):
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)


def _finds(root, entries, folder):
	return any(workspaces.entry_finds(root, entry, folder) for entry in entries)


def _excludes(entry, relative):
	return fsutil.matches_any(relative, [entry])


# ------------------------------------------------------------------------------------------------ #
# add

def _add(cli, args):
	options = _parse("add", args, lambda p: (p.add_argument("folder"), p.add_argument("--name"), p.add_argument("--language", default=""),
		p.add_argument("--kind", default="")))
	workspace = _loaded(cli)
	folder = fsutil.norm(os.path.abspath(os.path.join(cli.cwd, options.folder)))
	if not _inside(folder, workspace.dir):
		raise NukeError(f"{options.folder} is not a folder inside workspace {workspace.name} ({workspace.dir})")
	if not os.path.isdir(folder):
		raise NukeError(f"no folder {folder}")
	known = next((p for p in workspace.projects.values() if fsutil.same_path(p.dir, folder)), None)
	if known is not None:
		raise NukeError(f"{_relative(folder, workspace.dir)} is project {known.name} already")
	name = _write_project(workspace, folder, options)
	relative = _relative(folder, workspace.dir)
	_list(workspace, relative)
	cli.log.note(f"added project {name} in {relative}")
	return workspace.dir


def _write_project(workspace, folder, options):
	"""The project's name: from its nuke.toml when the folder has one, else from the one written now."""
	path = os.path.join(folder, MANIFEST_NAME)
	if os.path.isfile(path):
		manifest = load(path)
		if manifest.workspace is not None:
			raise NukeError(f"{path} is a workspace of its own; a project of {workspace.name} cannot be one")
		if manifest.project is None:
			raise NukeError(f"{path} has no [project] table")
		_unique(workspace, manifest.project.name)
		return manifest.project.name
	name = options.name or os.path.basename(folder)
	if not PROJECT_NAME.match(name):
		raise NukeError(f"'{name}' is not a valid project name (letters, digits, '_', '.', '-'); pass --name")
	_unique(workspace, name)
	language = options.language or "c4"
	if not LANGUAGE.match(language):
		raise NukeError(f"'{language}' is not a language id (lowercase letters, digits, '_', '+', '-')")
	kind = options.kind or _kind_of(workspace, folder, language)
	if kind not in KINDS:
		raise NukeError(f"--kind {kind}: one of {', '.join(KINDS)}")
	lines = ["[project]", f"name = {tomledit.render(name)}", f"kind = {tomledit.render(kind)}"]
	if language != "c4":
		lines.append(f"language = {tomledit.render(language)}")
	if language == "c4" and not os.path.isdir(os.path.join(folder, "src")):
		lines.append(f"sources = {tomledit.render(['.'])}")
	_save(path, "\n".join(lines) + "\n")
	return name


def _unique(workspace, name):
	clash = workspace.find_project(name)
	if clash is not None:
		raise NukeError(f"workspace {workspace.name} has a project named '{clash.name}' already ({clash.file})")


def _kind_of(workspace, folder, language):
	if language == "c4":
		for base, dirs, files in os.walk(folder):
			dirs[:] = [d for d in dirs if d not in workspaces.DISCOVERY_SKIP and not d.startswith(".")]
			if "main.cf" in files:
				return "executable"
		return "module"
	package = languages.find_packages(workspace.dir, workspace.spec.languages).get(language)
	return "executable" if package and package.module else "custom"


def _list(workspace, relative):
	"""Makes the workspace find the folder: an entry in its projects list when it keeps one, and no exclude naming it."""
	path, text = _root_text(workspace.dir)
	if not tomledit.has_table(text, "workspace"):
		text = tomledit.add_table(text, "workspace", {"name": workspace.name, "default-project": workspace.spec.default_project})
	spec = workspace.spec
	exclude = [e for e in spec.exclude if not _excludes(e, relative)]
	if len(exclude) != len(spec.exclude):
		text = tomledit.set_key(text, "workspace", "exclude", exclude) if exclude else tomledit.remove_key(text, "workspace", "exclude")
	folder = os.path.join(workspace.dir, relative)
	if spec.projects is not None and not _finds(workspace.dir, spec.projects, folder):
		text = tomledit.set_key(text, "workspace", "projects", list(spec.projects) + [relative])
	_save(path, text)


# ------------------------------------------------------------------------------------------------ #
# remove

def _remove(cli, args):
	options = _parse("remove", args, lambda p: (p.add_argument("project"), p.add_argument("--deleting", action="store_true")))
	workspace = _loaded(cli)
	project = workspace.project(options.project)
	if fsutil.same_path(project.dir, workspace.dir):
		raise NukeError(f"{project.name} is the workspace's own project (its nuke.toml holds the [workspace]); it cannot be removed")
	if not _inside(project.dir, workspace.dir):
		raise NukeError(f"{project.name} comes with a language package ({project.dir}), not with the workspace")
	dependents = sorted(p.name for p in workspace.projects.values() if any(d.lower() == project.name.lower() for d in p.depends))
	if dependents:
		raise NukeError(f"{', '.join(dependents)} depend{'s' if len(dependents) == 1 else ''} on {project.name}: take it out of their depends first")
	relative = _relative(project.dir, workspace.dir)
	path, text = _root_text(workspace.dir)
	spec = workspace.spec
	listed = spec.projects is not None
	entries = [e for e in (spec.projects or ()) if fsutil.norm(e) != relative]
	if listed and len(entries) != len(spec.projects):
		text = tomledit.set_key(text, "workspace", "projects", entries)
	still_found = not listed or _finds(workspace.dir, entries, project.dir)
	if still_found and not options.deleting:
		text = tomledit.set_key(text, "workspace", "exclude", list(spec.exclude) + [relative])
	if spec.default_project and spec.default_project.lower() == project.name.lower():
		text = tomledit.remove_key(text, "workspace", "default-project")
		cli.log.warn(f"{project.name} was the default project; name another with default-project")
	_save(path, text)
	cli.log.note(f"removed project {project.name} ({relative}{', its folder to be deleted' if options.deleting else ', its folder kept'})")
	return workspace.dir


# ------------------------------------------------------------------------------------------------ #
# move

def _move(cli, args):
	options = _parse("move", args, lambda p: (p.add_argument("project"), p.add_argument("to")))
	root = workspaces.find_root(cli.cwd)
	if root is None:
		raise NukeError(f"no workspace in {cli.cwd} or above it")
	target = fsutil.norm(os.path.abspath(os.path.join(cli.cwd, options.to)))
	if not _inside(target, root):
		raise NukeError(f"{options.to} is not inside the workspace ({root})")
	source = _source(cli, root, options.project, target)
	if fsutil.same_path(source, root):
		raise NukeError("the workspace's own project cannot move: it is the workspace")
	if os.path.isdir(source):
		if os.path.exists(target):
			raise NukeError(f"{target} is there already")
		if fsutil.is_within(target, source):
			raise NukeError("a project cannot move into its own folder")
		fsutil.ensure_dir(os.path.dirname(target))
		shutil.move(source, target)
	elif not os.path.isfile(os.path.join(target, MANIFEST_NAME)):
		raise NukeError(f"no project at {source} to move, and none at {target} already")
	old, new = _relative(source, root), _relative(target, root)
	_rewrite_entries(root, old, new)
	for leaving in _paths_leaving(target):
		cli.log.warn(f"{new}/{MANIFEST_NAME} names {leaving}, outside the project: check it from the new place")
	cli.log.note(f"moved {old} to {new}")
	return root


def _source(cli, root, named, target):
	"""The folder of the project `named`: a project folder, a project's name, or the folder it left for `target`."""
	folder = fsutil.norm(os.path.abspath(os.path.join(cli.cwd, named)))
	if os.path.isfile(os.path.join(folder, MANIFEST_NAME)):
		return folder
	project = cli.workspace.find_project(named) if cli.workspace is not None else None
	if project is not None and not fsutil.same_path(project.dir, target):
		return fsutil.norm(project.dir)
	if os.path.isfile(os.path.join(target, MANIFEST_NAME)):
		return folder
	raise NukeError(f"no project named or at '{named}' in the workspace")


def _moved(entry, old, new):
	"""The entry for the folder `old` (or one under it) now at `new`; unchanged when it names neither."""
	plain = fsutil.norm(entry)
	if plain == old:
		return new
	if plain.startswith(old + "/"):
		return new + plain[len(old):]
	return entry


def _rewrite_entries(root, old, new):
	path, text = _root_text(root)
	spec = load(path).workspace
	if spec is None:
		return
	if spec.projects is not None:
		entries = [_moved(e, old, new) for e in spec.projects]
		if not _finds(root, entries, os.path.join(root, new)):
			entries.append(new)
		if entries != list(spec.projects):
			text = tomledit.set_key(text, "workspace", "projects", entries)
	exclude = [_moved(e, old, new) for e in spec.exclude]
	if exclude != list(spec.exclude):
		text = tomledit.set_key(text, "workspace", "exclude", exclude)
	_save(path, text)


def _paths_leaving(folder):
	"""The strings of the project's nuke.toml that climb out of its folder ("../third_party/include")."""
	path = os.path.join(folder, MANIFEST_NAME)
	if not os.path.isfile(path):
		return []
	with open(path, encoding="utf-8") as f:
		text = f.read()
	found = []
	for quote in ('"', "'"):
		for piece in text.split(quote)[1::2]:
			if piece.replace("\\", "/").startswith("../") and piece not in found:
				found.append(piece)
	return found
