"""run: build, then start the program (arguments after `--` are passed to it)."""

import os

from .. import fsutil, process
from ..errors import NukeError, StepFailed
from . import base


class Run(base.Command):
	name = "run"
	description = "Build, then run the project's program; arguments after -- go to the program"
	requires = ("build",)
	policy = base.SELECTED

	def work(self, session, project):
		if project.kind == "module":
			raise NukeError(f"'{project.name}' is a module; run a program that depends on it")
		program = session.output(project)
		if program is None:
			if not any(s.phase == "run" for s in project.steps):
				raise NukeError(f"'{project.name}' has nothing to run: give it `output = ...` or a step with phase = \"run\"")
			return
		if not os.path.isfile(program):
			raise NukeError(f"{program} does not exist (did the build produce it?)")
		cwd = fsutil.join(project.dir, project.working_dir)
		session.log.note(f"running {program} {' '.join(session.args)}".rstrip())
		code = process.run_attached([program] + session.args, cwd=cwd)
		if code:
			raise StepFailed(f"{project.name}", code)
