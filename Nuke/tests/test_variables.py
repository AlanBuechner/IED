import os
import unittest

import support  # noqa: F401
from nuke.errors import NukeError
from nuke.variables import Variables, expand, expand_argv


class VariableTests(unittest.TestCase):
	def setUp(self):
		self.vars = Variables({"config": "Dist", "args": ["-x", "y"]}).child({"project.name": "Game"})

	def test_expands_names_from_every_scope(self):
		self.assertEqual(expand("${project.name}-${config}", self.vars), "Game-Dist")

	def test_unknown_name_is_an_error_listing_known_ones(self):
		with self.assertRaisesRegex(NukeError, r"unknown variable \$\{nope\}.*config"):
			expand("${nope}", self.vars)

	def test_escape(self):
		self.assertEqual(expand("$${config} ${config}", self.vars), "${config} Dist")

	def test_environment(self):
		os.environ["NUKE_TEST_VALUE"] = "v"
		try:
			self.assertEqual(expand("${env:NUKE_TEST_VALUE}", self.vars), "v")
		finally:
			del os.environ["NUKE_TEST_VALUE"]
		with self.assertRaisesRegex(NukeError, "not set"):
			expand("${env:NUKE_TEST_MISSING}", self.vars)

	def test_args_splice_only_as_a_whole_argument(self):
		self.assertEqual(expand_argv(["run", "${args}", "${config}"], self.vars), ["run", "-x", "y", "Dist"])
		with self.assertRaisesRegex(NukeError, "is a list"):
			expand("x${args}", self.vars)

	def test_lazy_values_resolve_once(self):
		calls = []
		lazy = Variables({"toolchain.dir": lambda: calls.append(1) or "C:/tc"})
		self.assertEqual(expand("${toolchain.dir}/${toolchain.dir}", lazy), "C:/tc/C:/tc")
		self.assertEqual(len(calls), 1)


if __name__ == "__main__":
	unittest.main()
