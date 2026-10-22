"""ObjectBuilder's incremental check against a stub compiler: an object is up to date only while its inputs are as they
were when its compile began (BUG-346)."""

import os
import shutil
import sys
import tempfile
import unittest

FROST = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, FROST)

import Config  # noqa: E402
from Builder.ObjectBuilder import ObjectBuilder  # noqa: E402

STUB = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stub_clang.py")


class ObjectBuilderTests(unittest.TestCase):
	def setUp(self):
		self.dir = tempfile.mkdtemp(prefix="frost_objects_")
		self.source = os.path.join(self.dir, "unit.cpp")
		with open(self.source, "w") as f:
			f.write("int main() { return 0; }\n")
		self.log = os.path.join(self.dir, "compiles.log")
		compiler = os.path.join(self.dir, "clang.cmd")
		with open(compiler, "w") as f:
			f.write(f'@"{sys.executable}" "{STUB}" %*\n')
		self.savedCompiler = Config.compiler
		Config.compiler = compiler
		os.environ["STUB_CLANG_LOG"] = self.log

	def tearDown(self):
		Config.compiler = self.savedCompiler
		os.environ.pop("STUB_CLANG_LOG", None)
		os.environ.pop("STUB_CLANG_EDIT", None)
		shutil.rmtree(self.dir, ignore_errors=True)

	def build(self):
		builder = ObjectBuilder()
		builder.sourceFile = self.source
		builder.objFile = os.path.join(self.dir, "unit.obj")
		builder.treeFile = os.path.join(self.dir, "unit.d")
		builder.argsFile = os.path.join(self.dir, "unit.args")
		builder.workingDir = self.dir
		builder.intDir = self.dir
		builder.reflect = False
		return builder.Build()

	def compiles(self):
		if not os.path.exists(self.log):
			return 0
		with open(self.log) as f:
			return len(f.readlines())

	def test_up_to_date_until_its_source_changes(self):
		self.assertEqual(self.build(), 0)
		self.assertEqual(self.build(), -1)
		self.assertEqual(self.compiles(), 1)
		later = os.path.getmtime(self.source) + 10
		os.utime(self.source, (later, later))
		self.assertEqual(self.build(), 0)
		self.assertEqual(self.compiles(), 2)

	def test_a_source_edited_while_it_compiled_builds_again(self):
		os.environ["STUB_CLANG_EDIT"] = "1"
		self.assertEqual(self.build(), 0)
		del os.environ["STUB_CLANG_EDIT"]
		self.assertLessEqual(os.path.getmtime(self.source), os.path.getmtime(os.path.join(self.dir, "unit.d")))
		self.assertEqual(self.build(), 0, "the object was compiled from the text before the edit")
		self.assertEqual(self.compiles(), 2)
		self.assertEqual(self.build(), -1)


if __name__ == "__main__":
	unittest.main()
