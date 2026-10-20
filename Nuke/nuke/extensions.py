"""Python extension scripts. A script listed in a manifest's `scripts` defines `register(nuke)`; the object it
receives adds commands and steps. Nothing is registered through module-level state."""

import importlib.util
import os
import sys

from .errors import NukeError
from .manifest import COMMAND_NAME, CommandSpec, StepSpec


class Registry:
	"""What one extension script registers. `default_project` is the project whose manifest listed the script."""

	def __init__(self, origin, default_project=None):
		self.origin = origin
		self.default_project = default_project
		self.commands = []
		self.steps = []          # (project name or None, StepSpec)

	def command(self, name, function=None, description="", requires=(), projects=None):
		"""Add a command. `function(ctx)` runs after the command's project work; use as a decorator or call."""
		def add(fn):
			if not COMMAND_NAME.match(name):
				raise NukeError(f"{self.origin}: '{name}' is not a valid command name")
			self.commands.append(CommandSpec(name=name, origin=self.origin, description=description or (fn.__doc__ or "").strip(),
				requires=tuple(requires), projects=tuple(projects) if projects is not None else None, function=fn))
			return fn
		return add(function) if function else add

	def step(self, phase, function=None, project=None, name=None, configs=(), inputs=(), outputs=(), description=""):
		"""Add a step to `phase` ("pre-build", "post-build", a command name...). `function(ctx)` returns None or an exit code."""
		def add(fn):
			owner = project if project is not None else self.default_project
			spec = StepSpec(name=name or fn.__name__, phase=phase, origin=self.origin, function=fn,
				configs=tuple(configs), inputs=tuple(inputs), outputs=tuple(outputs), description=description or (fn.__doc__ or "").strip())
			self.steps.append((owner, spec))
			return fn
		return add(function) if function else add


def load_module(path, unique_name):
	if not os.path.isfile(path):
		raise NukeError(f"script not found: {path}")
	spec = importlib.util.spec_from_file_location(unique_name, path)
	module = importlib.util.module_from_spec(spec)
	sys.modules[unique_name] = module
	try:
		spec.loader.exec_module(module)
	except NukeError:
		raise
	except Exception as error:
		raise NukeError(f"{path}: {type(error).__name__}: {error}") from error
	return module


def _module_name(path):
	return "nuke_ext_" + "".join(c if c.isalnum() else "_" for c in os.path.abspath(path))


def load_script(path, default_project=None):
	"""Run one extension script's register() and return what it registered."""
	module = load_module(path, _module_name(path))
	if not hasattr(module, "register"):
		raise NukeError(f"{path}: an extension script must define register(nuke)")
	registry = Registry(path.replace("\\", "/"), default_project)
	try:
		module.register(registry)
	except NukeError:
		raise
	except Exception as error:
		raise NukeError(f"{path}: register() raised {type(error).__name__}: {error}") from error
	return registry


def resolve_function(reference, base_dir):
	"""The callable named by a manifest `python = "file.py:function"` (relative to base_dir) or `"module:function"`."""
	target, sep, function = reference.rpartition(":")
	if not sep or not target or not function:
		raise NukeError(f"python step '{reference}' must look like \"path/to/file.py:function\"")
	if target.endswith(".py"):
		path = target if os.path.isabs(target) else os.path.join(base_dir, target)
		module = load_module(path, _module_name(path))
	else:
		try:
			module = importlib.import_module(target)
		except ImportError as error:
			raise NukeError(f"python step '{reference}': {error}") from error
	if not callable(getattr(module, function, None)):
		raise NukeError(f"python step '{reference}': no function '{function}'")
	return getattr(module, function)
