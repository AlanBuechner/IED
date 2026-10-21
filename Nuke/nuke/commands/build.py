"""build, clean and rebuild."""

import os

from .. import fsutil, languages
from ..c4 import compile as c4compile
from ..c4 import ide_bridge
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
		c4compile.compile_program(session, job)
		copy_files(session, project)
		if session.workspace.spec.ide:
			ide_bridge.write(session.workspace, project.name, job, session.toolchain(), c4compile.compiler_args(project, session.configuration))


def copy_files(session, project):
	"""Every [[project.copy]] of the program and of the modules compiled into it, into the program's bin folder."""
	destination_root = session.bin_dir(project)
	copied = 0
	for module in session.c4_modules(project):
		for copy in module.copies:
			source = fsutil.join(module.dir, copy.source)
			dest = fsutil.join(destination_root, copy.dest)
			if os.path.isdir(source):
				copied += fsutil.copy_tree_if_newer(source, dest)
			elif os.path.isfile(source):
				copied += fsutil.copy_if_newer(source, dest)
			else:
				session.log.warn(f"{module.file}: copy source '{copy.source}' does not exist")
	if copied:
		session.log.note(f"{project.name}: copied {copied} file(s) into {destination_root}")


class Clean(base.Command):
	name = "clean"
	description = "Delete the build outputs of the configuration (named projects and their dependencies, default: all)"
	policy = base.EVERYTHING

	def work(self, session, project):
		if project.is_driven and callable(getattr(session.driver(project), "clean", None)):
			session.driver(project).clean(session.driver_context(project))
		removed = [d for d in (session.bin_dir(project), session.int_dir(project)) if fsutil.remove_tree(d)]
		if removed:
			session.log.note(f"cleaned {project.name} ({session.configuration.name})")


class Rebuild(base.Command):
	name = "rebuild"
	description = "clean, then build"
	requires = ("clean", "build")
	policy = base.CLOSURE
