"""`${name}` expansion for manifest strings. Unknown names are errors, never silently empty."""

import os
import re

from .errors import NukeError

PATTERN = re.compile(r"\$\$\{|\$\{([^}]*)\}")
SPLICE = "${args}"


class Variables:
	"""A read-only view of named values; values may be callables resolved on first use (e.g. the toolchain)."""

	def __init__(self, values, parent=None):
		self.values = values
		self.parent = parent

	def child(self, values):
		return Variables(values, self)

	def get(self, name):
		if name.startswith("env:"):
			key = name[4:]
			if key not in os.environ:
				raise NukeError(f"environment variable '{key}' is not set (used as ${{{name}}})")
			return os.environ[key]
		scope = self
		while scope is not None:
			if name in scope.values:
				value = scope.values[name]
				if callable(value):
					value = value()
					scope.values[name] = value
				return value
			scope = scope.parent
		raise NukeError(f"unknown variable ${{{name}}} (known: {', '.join(sorted(self.names()))})")

	def names(self):
		scope, seen = self, set()
		while scope is not None:
			seen.update(scope.values)
			scope = scope.parent
		return seen


def expand(text, variables):
	def substitute(match):
		if match.group(0) == "$${":
			return "${"
		value = variables.get(match.group(1).strip())
		if isinstance(value, (list, tuple)):
			raise NukeError(f"${{{match.group(1)}}} is a list; use it as a whole argument")
		return str(value)
	return PATTERN.sub(substitute, text)


def expand_argv(argv, variables):
	"""Expand every argument; an argument that is exactly ${args} splices the extra command-line arguments."""
	result = []
	for arg in argv:
		if arg == SPLICE:
			result.extend(variables.get("args"))
		else:
			result.append(expand(arg, variables))
	return result
