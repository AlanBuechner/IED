"""help: the command line, every command (built-in and this workspace's) and per-command usage."""

from ..errors import NukeError
from . import base

OVERVIEW = """usage: nuke [COMMAND...] [CONFIG] [PROJECT...] [options] [-- PROGRAM ARGS]

Commands run left to right; `nuke build run dev` builds then runs the default project in Development.
A word is a command if one has that name, else a configuration (Debug|dev, Development, Dist|release, or the
workspace's own), else a project name. With no command, nuke builds.

options:
  -p, --project NAME   work on NAME (repeatable; same as naming the project as a word)
  -c, --config NAME    configuration (default: the workspace's default-configuration, else Development)
  -a, --arch NAME      architecture (x64)
  -s, --system NAME    system (windows)
  -w, --workspace DIR  use the workspace at DIR instead of searching up from the current folder
  --all                every project of the workspace
  --filter GLOB        test cases to run (nuke test --filter "math.*")
  --json               machine-readable output (list, info)
  -v, --verbose        show the commands nuke runs
  --version            print Nuke's version"""


class Help(base.Command):
	name = "help"
	description = "Show this overview, or one command's usage (nuke help COMMAND)"
	takes_args = True
	needs_workspace = False
	policy = base.NONE

	def __init__(self, command_table):
		self.command_table = command_table

	def main(self, cli, args):
		if args:
			command = self.command_table.get(args[0])
			if command is None:
				raise NukeError(f"no command named '{args[0]}' (nuke help lists them)")
			cli.log.line(f"nuke {command.name}: {command.description}")
			if command.requires:
				cli.log.line(f"runs first: {', '.join(command.requires)}")
			if command.usage:
				cli.log.line(command.usage)
			return 0
		cli.log.line(OVERVIEW)
		cli.log.line("")
		cli.log.line("commands:")
		for command in self.command_table.values():
			cli.log.line(f"  {command.name:<14} {command.description}")
		if cli.workspace is None:
			cli.log.line("\n(not in a workspace: commands defined by a workspace's nuke.toml are not listed)")
		return 0
