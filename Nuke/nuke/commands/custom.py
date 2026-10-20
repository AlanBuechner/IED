"""Commands a workspace defines ([commands.<name>] tables and extension scripts)."""

from ..errors import NukeError, StepFailed
from ..steps import StepContext
from . import base


class CustomCommand(base.Command):
	policy = base.CLOSURE

	def __init__(self, spec):
		self.spec = spec
		self.name = spec.name
		self.description = spec.description or f"defined in {spec.origin}"
		self.requires = spec.requires
		self.project_override = spec.projects

	def finish(self, session, targets):
		if self.spec.function is None:
			return 0
		context = StepContext(session, None, session.variables(), targets)
		try:
			result = self.spec.function(context)
		except (NukeError, StepFailed):
			raise
		except Exception as error:
			raise NukeError(f"{self.spec.origin}: command '{self.name}' raised {type(error).__name__}: {error}") from error
		return result if isinstance(result, int) and not isinstance(result, bool) else 0
