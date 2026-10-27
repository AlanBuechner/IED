"""The command table: built-in commands plus the workspace's own, by name."""

from ..errors import NukeError
from .build import Build, Clean, Rebuild
from .custom import CustomCommand
from .help import Help
from .init import Init
from .inspect import Info, List
from .package import Package
from .project import Project
from .run import Run
from .test import Test
from .toolchain import Toolchain


def command_table(workspace=None):
	table = {}
	for command in (Build(), Run(), Clean(), Rebuild(), List(), Init(), Project(), Package(), Toolchain()):
		table[command.name] = command
	table["test"] = Test(table)
	table["info"] = Info(table)
	table["help"] = Help(table)
	for spec in (workspace.commands.values() if workspace else ()):
		if spec.name in table:
			raise NukeError(f"{spec.origin}: '{spec.name}' is a built-in command; attach steps to its phases "
				f"(pre-{spec.name}, {spec.name}, post-{spec.name}) instead of redefining it")
		table[spec.name] = CustomCommand(spec)
	return table
