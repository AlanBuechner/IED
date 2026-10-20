"""The command protocol shared by built-in and workspace-defined commands."""

# How a command picks the projects it works on when the command line names none (and what it does with names):
CLOSURE = "closure"        # the named projects (default: the workspace's default project) and their dependencies
SELECTED = "selected"      # only the named projects (default: the default project)
TESTS = "tests"            # the named projects, or every project that has tests
EVERYTHING = "everything"  # the named projects and their dependencies, or every project
NONE = "none"              # no projects: the command works on the workspace (or nothing) only


class Command:
	name = ""
	description = ""
	usage = ""
	requires = ()
	policy = CLOSURE
	takes_args = False         # the rest of the command line belongs to this command (nuke toolchain install ...)
	needs_workspace = True
	project_override = None    # a workspace command may fix its projects (CommandSpec.projects)

	def work(self, session, project):
		"""Built-in per-project work, between the project's pre-<name> and <name> steps."""

	def finish(self, session, targets):
		"""After every project and the workspace-level steps. Returns the command's exit code."""
		return 0

	def main(self, cli, args):
		"""Entry point of a takes_args command; `cli` carries the log, the store and the workspace (or None)."""
		raise NotImplementedError
