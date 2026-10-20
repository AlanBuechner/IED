"""toolchain: list, inspect, install and pin versioned C4 toolchains."""

import argparse
import os

from .. import fsutil, toolchains
from ..errors import NukeError
from . import base

USAGE = """nuke toolchain [list]                     installed toolchains (* = this workspace's)
nuke toolchain which [VERSION]            the folder a version (default: this workspace's) resolves to
nuke toolchain install SOURCE [--as V] [--force]
                                          copy a C4 install (a folder, or an installed name like dev) into the
                                          store under its version; installed versions never change without --force
nuke toolchain remove VERSION             delete an installed toolchain
nuke toolchain use VERSION                pin this workspace to VERSION (rewrites [workspace].toolchain)"""


class Toolchain(base.Command):
	name = "toolchain"
	description = "List, install, remove or pin versioned C4 toolchains (nuke toolchain help)"
	usage = USAGE
	takes_args = True
	needs_workspace = False
	policy = base.NONE

	def main(self, cli, args):
		action = args[0] if args else "list"
		rest = args[1:]
		handlers = {"list": self.list, "which": self.which, "install": self.install, "remove": self.remove,
			"use": self.use, "help": self.help}
		if action not in handlers:
			raise NukeError(f"unknown toolchain action '{action}'\n{USAGE}")
		return handlers[action](cli, rest)

	def help(self, cli, rest):
		cli.log.line(USAGE)
		return 0

	def list(self, cli, rest):
		installed = cli.store.installed()
		pinned = cli.workspace.spec.toolchain if cli.workspace else None
		if not installed:
			cli.log.line(f"no toolchains installed in {cli.store.kind_dir()}")
			return 0
		for toolchain in installed:
			mark = "*" if pinned and pinned == toolchain.version else " "
			cli.log.line(f"{mark} {toolchain.version:<12} {toolchain.describe()}")
		cli.log.line(f"store: {cli.store.kind_dir()}")
		return 0

	def which(self, cli, rest):
		spec = rest[0] if rest else (cli.workspace.spec.toolchain if cli.workspace else None)
		base_dir = cli.workspace.dir if cli.workspace else cli.cwd
		cli.log.line(cli.store.resolve(spec, base_dir).dir)
		return 0

	def install(self, cli, rest):
		parser = argparse.ArgumentParser(prog="nuke toolchain install", add_help=False)
		parser.add_argument("source")
		parser.add_argument("--as", dest="version")
		parser.add_argument("--force", action="store_true")
		try:
			options = parser.parse_args(rest)
		except SystemExit:
			raise NukeError(f"usage: nuke toolchain install SOURCE [--as VERSION] [--force]") from None
		source = options.source
		if not os.path.isabs(source) and cli.store.get(source) is None:
			source = os.path.join(cli.cwd, source)
		toolchain, copied = cli.store.install(source, options.version, options.force)
		verb = "installed" if copied else "already installed (same build)"
		cli.log.note(f"{verb}: {toolchain.describe()} -> {toolchain.dir}" + (f" ({copied} file(s) copied)" if copied else ""))
		return 0

	def remove(self, cli, rest):
		if len(rest) != 1:
			raise NukeError("usage: nuke toolchain remove VERSION")
		cli.store.remove(rest[0])
		cli.log.note(f"removed toolchain {rest[0]}")
		return 0

	def use(self, cli, rest):
		if len(rest) != 1:
			raise NukeError("usage: nuke toolchain use VERSION")
		if cli.workspace is None:
			raise NukeError("not in a workspace (no nuke.toml with [workspace] here or above)")
		version = rest[0]
		toolchain = cli.store.resolve(version, cli.workspace.dir)
		path = cli.workspace.spec.file
		text = fsutil.read_text(path)
		fsutil.write_if_changed(path, toolchains.set_workspace_toolchain(text, version))
		cli.log.note(f"workspace {cli.workspace.name} now uses {toolchain.describe()}")
		return 0
