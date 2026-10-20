"""Errors Nuke reports to the user. Anything else escaping the CLI is a bug in Nuke."""


class NukeError(Exception):
	"""A problem with the workspace, a manifest or the command line. Exit code 1."""


class StepFailed(Exception):
	"""A compile, step or program exited non-zero; the invocation stops with its exit code."""

	def __init__(self, what, code):
		super().__init__(f"{what} failed (exit code {code})")
		self.what = what
		self.code = code if code else 1
