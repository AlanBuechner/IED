"""build, clean and rebuild."""

import os

from .. import copies, fsutil, languages
from ..variables import expand
from ..c4 import compile as c4compile
from ..c4 import ide_bridge, server
from ..errors import NukeError
from . import base


class Build(base.Command):
	name = "build"
	description = "Build the project (default: the workspace's default project) and everything it depends on"
	policy = base.CLOSURE

	def work(self, session, project):
		if project.is_driven:
			session.log.note(f"{project.name}: building ({project.language})")
			context = session.driver_context(project)
			languages.stage(session.driver(project).build(context), context.bin_dir, session.log, project.name)
			return
		if project.kind == "module":
			session.log.detail(f"{project.name}: a module compiles into the programs that depend on it")
			return
		if project.kind != "executable":
			return
		job = c4compile.plan(session, project)
		try:
			c4compile.compile_program(session, job)
		finally:
			# A compile that fails still tells the editor what it compiles: its errors are what the editor should show.
			if session.workspace.spec.ide:
				ide_bridge.write_program(session, project)
		copy_files(session, project)


def copy_files(session, project, test=False):
	"""Every [[project.copy]] of the program and of the modules compiled into it (into its test harness, with `test`), into
	the program's bin folder; what an earlier copy wrote there and this one does not (a source removed, renamed, excluded
	or gone) is removed."""
	destination_root = session.bin_dir(project)
	copied, written = 0, []
	for module in session.c4_modules(project, test):
		values = session.variables(module)
		for copy in module.copies:
			if not copy.applies_to(session.system):
				continue
			source = fsutil.join(module.dir, expand(copy.source, values))
			dest = fsutil.join(destination_root, expand(copy.dest, values))
			if os.path.isdir(source):
				pairs = fsutil.tree_copies(source, dest, copy.exclude)
			elif os.path.isfile(source):
				pairs = [(source, dest)]
			else:
				session.log.warn(f"{module.file}: copy source '{copy.source}' does not exist")
				continue
			copied += sum(fsutil.copy_if_newer(path, target) for path, target in pairs)
			written.extend(target for _, target in pairs)
	removed = copies.record(destination_root, session.int_dir(project), written)
	if copied:
		session.log.note(f"{project.name}: copied {copied} file(s) into {destination_root}")
	if removed:
		session.log.note(f"{project.name}: removed {removed} file(s) from {destination_root} that are no longer copied")


class Clean(base.Command):
	name = "clean"
	description = "Delete the build outputs of the configuration (named projects and their dependencies, default: all)"
	policy = base.EVERYTHING

	def work(self, session, project):
		if project.is_driven and callable(getattr(session.driver(project), "clean", None)):
			session.driver(project).clean(session.driver_context(project))
		if not project.is_driven and project.kind == "executable":
			stop_server(session, project)
		removed = [d for d in (session.bin_dir(project), session.int_dir(project)) if fsutil.remove_tree(d)]
		if removed:
			session.log.note(f"cleaned {project.name} ({session.configuration.name})")


def stop_server(session, project):
	"""Ends the program's C4 build server, which holds its folder and a build's memory until it ends."""
	try:
		job = c4compile.plan(session, project)
	except NukeError:
		return  # no toolchain to compile with: no server of it runs
	if job.compiler and server.stop(job.argv, cwd=job.cwd):
		session.log.note(f"{project.name}: ended its C4 build server")


class Rebuild(base.Command):
	name = "rebuild"
	description = "clean, then build"
	requires = ("clean", "build")
	policy = base.CLOSURE
