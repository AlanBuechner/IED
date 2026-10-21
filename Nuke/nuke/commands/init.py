"""init: scaffold a new workspace (outside any workspace) or a new project (inside one), and with --ied the workspace's
IED solution and project files (ied_files.py)."""

import argparse
import os
import shutil

from .. import fsutil, ied_files
from .. import workspace as workspaces
from ..errors import NukeError
from ..manifest import KINDS, MANIFEST_NAME, PROJECT_NAME
from . import base

TEMPLATES = fsutil.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "templates")
NUKE_SKILLS = fsutil.join(os.path.dirname(TEMPLATES), "skills")
USAGE = """nuke init [NAME] [--kind executable|module|custom] [--toolchain VERSION] [--ied]
  outside a workspace: make the current folder a new workspace with one project (NAME defaults to the folder)
  inside a workspace:  create the project NAME in ./NAME (or here with --here)
  --ied:               also write (or refresh) the IED solution <Workspace>.ied.sln and build/ied/*.ied.proj;
                       inside a workspace, `nuke init --ied` alone does only that"""


class Init(base.Command):
	name = "init"
	description = "Create a workspace here, or a project inside the current workspace (nuke init --help)"
	usage = USAGE
	takes_args = True
	needs_workspace = False
	policy = base.NONE

	def main(self, cli, args):
		options = _parse(args)
		if options.help:
			cli.log.line(USAGE)
			return 0
		cwd = cli.cwd
		if cli.workspace is None:
			create_workspace(cli, cwd, options)
			return ied_solution(cli, workspaces.load_workspace(cwd)) if options.ied else 0
		if options.ied and not options.here and not options.name:
			return ied_solution(cli, cli.workspace)
		folder = cwd if options.here else os.path.join(cwd, options.name or "")
		if not options.here and not options.name:
			raise NukeError(f"inside workspace {cli.workspace.name}: name the new project (nuke init NAME)")
		create_project(cli, folder, options)
		return ied_solution(cli, workspaces.load_workspace(cli.workspace.dir)) if options.ied else 0


def _parse(args):
	parser = argparse.ArgumentParser(prog="nuke init", add_help=False)
	parser.add_argument("name", nargs="?")
	parser.add_argument("--kind", default="executable", choices=KINDS)
	parser.add_argument("--toolchain")
	parser.add_argument("--here", action="store_true")
	parser.add_argument("--ied", action="store_true")
	parser.add_argument("-h", "--help", action="store_true")
	try:
		options = parser.parse_args(args)
	except SystemExit:
		raise NukeError(USAGE) from None
	if options.name and not PROJECT_NAME.match(options.name):
		raise NukeError(f"'{options.name}' is not a valid project name (letters, digits, '_', '.', '-')")
	return options


def _template(template, **values):
	with open(os.path.join(TEMPLATES, template), encoding="utf-8") as f:
		return f.read().format(**values)


def _write_new(path, text):
	if os.path.exists(path):
		raise NukeError(f"{path} already exists")
	fsutil.ensure_dir(os.path.dirname(path))
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)


def _sources(folder, name, kind):
	if kind == "executable":
		_write_new(os.path.join(folder, "src", "main.cf"), _template("main.cf", name=name))
	elif kind == "module":
		_write_new(os.path.join(folder, "src", f"{name}.ch"), _template("module.ch", name=name))


def create_workspace(cli, folder, options):
	name = options.name or os.path.basename(os.path.abspath(folder))
	if not PROJECT_NAME.match(name):
		raise NukeError(f"the folder name '{name}' is not a valid project name; pass one: nuke init NAME")
	toolchain = options.toolchain or _default_toolchain(cli)
	_write_new(os.path.join(folder, MANIFEST_NAME), _template("workspace.toml", name=name, kind=options.kind, toolchain=toolchain))
	_sources(folder, name, options.kind)
	_ensure_gitignore(folder)
	skills = _copy_skills(cli, folder, toolchain)
	cli.log.note(f"created workspace {name} ({options.kind}, toolchain {toolchain}){f', {skills} skill(s) in .ied/skills' if skills else ''}")
	cli.log.note("next: nuke build run")
	return 0


def create_project(cli, folder, options):
	name = options.name or os.path.basename(os.path.abspath(folder))
	if cli.workspace.find_project(name):
		raise NukeError(f"workspace {cli.workspace.name} already has a project named '{name}'")
	_write_new(os.path.join(folder, MANIFEST_NAME), _template("project.toml", name=name, kind=options.kind))
	_sources(folder, name, options.kind)
	relative = os.path.relpath(folder, cli.workspace.dir).replace("\\", "/")
	cli.log.note(f"created project {name} in {relative}")
	if cli.workspace.spec.projects is not None:
		cli.log.note(f"add \"{relative}\" to [workspace].projects in {cli.workspace.spec.file}")
	return 0


def ied_solution(cli, workspace):
	"""The workspace's IED solution and project files, its .gitignore and skills as `nuke init` makes them."""
	toolchain = None
	try:
		toolchain = cli.store.resolve(workspace.spec.toolchain, workspace.dir)
	except NukeError as error:
		cli.log.warn(f"no toolchain, so no C4 plugin in the solution: {error}")
	changed = ied_files.generate(workspace, toolchain, cli.log)
	_ensure_gitignore(workspace.dir)
	_copy_skills(cli, workspace.dir, workspace.spec.toolchain)
	path = os.path.relpath(ied_files.solution_path(workspace), cli.cwd).replace("\\", "/")
	cli.log.note(f"IED solution {path}: {len(workspace.projects)} project(s), {changed} file(s) written or removed")
	return 0


def _default_toolchain(cli):
	latest = cli.store.latest()
	if latest:
		return latest.version
	if cli.store.get("dev"):
		return "dev"
	raise NukeError(f"no C4 toolchain is installed in {cli.store.kind_dir()}; pass --toolchain VERSION or install one first")


def _ensure_gitignore(folder):
	path = os.path.join(folder, ".gitignore")
	existing = fsutil.read_text(path) or ""
	wanted = [line for line in _template("gitignore").splitlines() if line and line not in existing.splitlines()]
	if wanted:
		with open(path, "a", encoding="utf-8", newline="\n") as f:
			f.write(("" if not existing or existing.endswith("\n") else "\n") + "\n".join(wanted) + "\n")


def _copy_skills(cli, folder, toolchain_spec):
	"""The toolchain's C4 skills and Nuke's own skill into .ied/skills/ (the workspace's AI skills)."""
	sources = [os.path.join(NUKE_SKILLS, d) for d in sorted(os.listdir(NUKE_SKILLS))] if os.path.isdir(NUKE_SKILLS) else []
	try:
		skills_dir = cli.store.resolve(toolchain_spec, folder).skills_dir()
		if os.path.isdir(skills_dir):
			sources += [os.path.join(skills_dir, d) for d in sorted(os.listdir(skills_dir))]
	except NukeError as error:
		cli.log.warn(f"no language skills copied: {error}")
	copied = 0
	for source in sources:
		dest = os.path.join(folder, ".ied", "skills", os.path.basename(source))
		if os.path.isfile(os.path.join(source, "SKILL.md")) and not os.path.exists(dest):
			shutil.copytree(source, dest)
			copied += 1
	return copied
