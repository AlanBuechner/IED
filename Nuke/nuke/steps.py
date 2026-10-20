"""Running manifest and extension steps. A step with `outputs` is skipped while its outputs are newer than its
inputs and its expanded command is unchanged (recorded in a stamp under the int folder)."""

import hashlib
import json
import os

from . import extensions, fsutil, process
from .errors import NukeError, StepFailed
from .variables import expand, expand_argv


class StepContext:
	"""What a Python step or command handler receives."""

	def __init__(self, session, project, variables, targets=()):
		self.session = session
		self.workspace = session.workspace
		self.project = project
		self.config = session.configuration.name
		self.configuration = session.configuration
		self.arch = session.arch
		self.system = session.system
		self.args = list(session.args)
		self.log = session.log
		self.variables = variables
		self.targets = list(targets)

	def expand(self, text):
		return expand(text, self.variables)

	def run(self, argv, cwd=None, env=None, check=True):
		"""Run a program with streamed output. Raises StepFailed on a non-zero exit when `check`."""
		code = process.run(list(argv), self.log, cwd=cwd or (self.project.dir if self.project else self.workspace.dir), env=env)
		if check and code != 0:
			raise StepFailed(f"{argv[0]}", code)
		return code

	def toolchain(self):
		return self.session.toolchain()

	def bin_dir(self, project=None):
		return self.session.bin_dir(project or self.project)

	def int_dir(self, project=None):
		return self.session.int_dir(project or self.project)


def run_steps(session, steps, phase, project=None):
	for step in steps:
		if step.phase == phase and step.applies_to(session.configuration.name):
			run_step(session, step, project)


def run_step(session, step, project=None):
	variables = session.variables(project)
	base = project.dir if project else session.workspace.dir
	label = f"{project.name + ': ' if project else ''}{step.phase} step '{step.name}'"
	cwd = _path(expand(step.cwd, variables), base) if step.cwd else base
	env = {k: expand(v, variables) for k, v in step.env.items()}
	inputs = [expand(i, variables) for i in step.inputs]
	outputs = [expand(o, variables) for o in step.outputs]
	command = _command(step, variables, cwd)
	stamp = _stamp_path(session, project, step)
	signature = _signature(command, cwd, env, inputs, outputs)
	if outputs and _up_to_date(stamp, signature, inputs, outputs, base):
		session.log.note(f"{label}: up to date")
		return
	session.log.note(f"{label}{' - ' + step.description if step.description else ''}")
	with session.timed(label):
		code = _execute(session, step, project, variables, command, cwd, env)
	if code:
		raise StepFailed(label, code)
	if outputs:
		fsutil.write_if_changed(stamp, signature)


def _command(step, variables, cwd):
	if step.run is None:
		return None
	if isinstance(step.run, str):
		return expand(step.run, variables)
	return program_relative_to(expand_argv(step.run, variables), cwd)


def program_relative_to(argv, cwd):
	"""Windows resolves a relative program path against Nuke's own folder, not the step's; resolve it here."""
	program = argv[0] if argv else ""
	if program and not os.path.isabs(program) and ("/" in program or "\\" in program):
		return [fsutil.join(cwd, program)] + argv[1:]
	return argv


def _execute(session, step, project, variables, command, cwd, env):
	if command is not None:
		return process.run(command, session.log, cwd=cwd, env=env, shell=isinstance(command, str))
	function = step.function or extensions.resolve_function(step.python, os.path.dirname(step.origin))
	context = StepContext(session, project, variables)
	try:
		result = _in_folder(cwd, function, context)
	except (NukeError, StepFailed):
		raise
	except Exception as error:
		raise NukeError(f"{step.origin}: step '{step.name}' raised {type(error).__name__}: {error}") from error
	return result if isinstance(result, int) and not isinstance(result, bool) else 0


def _in_folder(folder, function, context):
	previous = os.getcwd()
	os.chdir(folder)
	try:
		return function(context)
	finally:
		os.chdir(previous)


def _path(path, base):
	return fsutil.norm(path if os.path.isabs(path) else os.path.join(base, path))


def _stamp_path(session, project, step):
	owner = session.int_dir(project) if project else fsutil.join(session.int_root(), "_workspace")
	key = hashlib.sha1(f"{step.origin}|{step.phase}|{step.name}".encode()).hexdigest()[:12]
	return fsutil.join(owner, "steps", f"{step.phase}-{key}.stamp")


def _signature(command, cwd, env, inputs, outputs):
	return json.dumps({"command": command, "cwd": cwd, "env": env, "inputs": inputs, "outputs": outputs}, sort_keys=True)


def _up_to_date(stamp, signature, inputs, outputs, base):
	if fsutil.read_text(stamp) != signature:
		return False
	produced = fsutil.expand_globs(outputs, base)
	if not produced or not all(os.path.exists(p) for p in produced):
		return False
	consumed = fsutil.expand_globs(inputs, base)
	if any(not os.path.exists(p) for p in consumed):
		return False
	newest_input = max((fsutil.mtime(p) for p in consumed), default=0.0)
	return newest_input <= min(fsutil.mtime(p) for p in produced)
