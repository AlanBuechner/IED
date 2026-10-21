"""Shared test helpers: temporary workspaces, a stub compiler and a captured invocation of the CLI."""

import io
import json
import os
import shutil
import sys
import tempfile
import unittest

NUKE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if NUKE_DIR not in sys.path:
	sys.path.insert(0, NUKE_DIR)

from nuke import cli, toolchains  # noqa: E402
from nuke.log import Log  # noqa: E402

FAKE_COMPILER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fake_c4.py")


def write_files(root, files):
	for relative, text in files.items():
		path = os.path.join(root, *relative.split("/"))
		os.makedirs(os.path.dirname(path), exist_ok=True)
		with open(path, "w", encoding="utf-8", newline="\n") as f:
			f.write(text)


def read(path):
	with open(path, encoding="utf-8") as f:
		return f.read()


def make_install(folder, version="1.0.0", commit="abc123", dirty=False):
	"""A folder that passes as a C4 install (its files are empty; the stub compiler does the work)."""
	write_files(folder, {f: "" for f in ("C4.exe", "Bedrock/bedrock.ch", "C4Runtime/GC/GCApi.c",
		"toolchain/bin/clang.exe", "toolchain/bin/lld-link.exe")})
	write_files(folder, {"toolchain.json": json.dumps({"name": "c4", "version": version, "commit": commit, "dirty": dirty})})
	return folder


class StubStore(toolchains.ToolchainStore):
	"""A real store whose toolchains compile with the stub compiler instead of C4.exe."""

	def resolve(self, spec, base_dir):
		found = super().resolve(spec, base_dir)
		return toolchains.Toolchain(found.version, found.dir, found.metadata, command=(sys.executable, FAKE_COMPILER))


class WorkspaceTest(unittest.TestCase):
	"""Each test gets a temporary folder, a toolchain store holding version 1.0.0, and `nuke(...)`."""

	def setUp(self):
		self.tmp = tempfile.mkdtemp(prefix="nuke-test-")
		self.root = os.path.join(self.tmp, "ws")
		os.makedirs(self.root)
		self.store_root = os.path.join(self.tmp, "store")
		make_install(os.path.join(self.store_root, "c4", "1.0.0"))
		self.store = StubStore(self.store_root)
		self.compiler_log = os.path.join(self.tmp, "compiler.log")
		os.environ["FAKE_C4_LOG"] = self.compiler_log
		os.environ.pop("FAKE_C4_FAIL", None)
		os.environ.pop("FAKE_C4_WITHOUT", None)
		toolchains.listed_options.cache_clear()

	def tearDown(self):
		os.environ.pop("FAKE_C4_LOG", None)
		os.environ.pop("FAKE_C4_FAIL", None)
		os.environ.pop("FAKE_C4_WITHOUT", None)
		shutil.rmtree(self.tmp, ignore_errors=True)

	def files(self, files):
		write_files(self.root, files)

	def nuke(self, *argv, cwd=None):
		out = io.StringIO()
		code = cli.main(list(argv), log=Log(out), store=self.store, cwd=cwd or self.root)
		return code, out.getvalue()

	def compiles(self):
		if not os.path.isfile(self.compiler_log):
			return []
		with open(self.compiler_log, encoding="utf-8") as f:
			return [json.loads(line) for line in f if line.strip()]

	def path(self, *parts):
		return os.path.join(self.root, *parts)
