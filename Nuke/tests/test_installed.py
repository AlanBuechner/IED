"""The Nuke an IED install carries (installed.py, IED docs/21): its home is the checkout it was installed from, and a
command line given in a checkout runs that checkout's own Nuke."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from support import NUKE_DIR, make_install, write_files
from nuke import cli, installed

# A checkout's Nuke that says what it was given.
ECHO_NUKE = """import json, os, sys
with open(os.environ["NUKE_TEST_ECHO"], "w", encoding="utf-8") as f:
	json.dump(sys.argv[1:], f)
sys.exit(3)
"""


def norm(path):
	return path.replace("\\", "/")


class InstalledTests(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.mkdtemp(prefix="nuke-installed-")
		self.checkout = os.path.join(self.tmp, "IED")
		self.install = os.path.join(self.tmp, "Programs", "IED")
		self.outside = os.path.join(self.tmp, "Game")
		write_files(self.checkout, {"Nuke/nuke/__main__.py": ECHO_NUKE, "C4/nuke.toml": "[project]\nname = \"C4\"\n"})
		write_files(self.install, {"Nuke/nuke/__main__.py": "", "install.json": json.dumps({"source": norm(self.checkout)})})
		os.makedirs(self.outside)
		self.echo = os.path.join(self.tmp, "echo.json")

	def tearDown(self):
		shutil.rmtree(self.tmp, ignore_errors=True)

	def echoed(self):
		with open(self.echo, encoding="utf-8") as f:
			return json.load(f)

	def test_an_install_s_home_is_its_checkout(self):
		self.assertEqual(installed.home_of(self.install), norm(self.checkout))
		self.assertEqual(installed.home_of(self.checkout), norm(self.checkout), "a checkout is its own home")

	def test_an_install_whose_checkout_is_gone_is_its_own_home(self):
		shutil.rmtree(os.path.join(self.checkout, "Nuke"))
		self.assertEqual(installed.home_of(self.install), norm(self.install))
		write_files(self.install, {"install.json": "not json"})
		self.assertEqual(installed.home_of(self.install), norm(self.install), "a record that does not read")

	def test_a_command_line_in_a_checkout_goes_to_its_nuke(self):
		inside = os.path.join(self.checkout, "C4")
		self.assertEqual(cli.carried_checkout(["build"], inside, self.install), norm(self.checkout))
		self.assertEqual(cli.carried_checkout(["-w", inside, "build"], self.outside, self.install), norm(self.checkout), "-w names the folder")
		self.assertIsNone(cli.carried_checkout(["build"], self.outside, self.install), "outside a checkout: the installed Nuke")
		self.assertIsNone(cli.carried_checkout(["build"], self.install, self.install), "in the install itself")
		self.assertIsNone(cli.carried_checkout(["build"], inside, self.checkout), "a checkout's Nuke runs every command line itself")

	def test_the_checkout_s_nuke_gets_the_same_words_and_gives_its_exit_code(self):
		os.environ["NUKE_TEST_ECHO"] = self.echo
		try:
			code = installed.run_nuke(self.checkout, ["build", "Development", "--", "a b"])
		finally:
			del os.environ["NUKE_TEST_ECHO"]
		self.assertEqual(code, 3)
		self.assertEqual(self.echoed(), ["build", "Development", "--", "a b"])

	def test_the_installed_entry_hands_over_in_a_checkout_and_keeps_the_checkout_s_store_elsewhere(self):
		shutil.rmtree(os.path.join(self.install, "Nuke"))
		shutil.copytree(os.path.join(NUKE_DIR, "nuke"), os.path.join(self.install, "Nuke", "nuke"), ignore=shutil.ignore_patterns("__pycache__"))
		make_install(os.path.join(self.checkout, "toolchains", "c4", "7.0.0"), version="7.0.0")
		env = {k: v for k, v in os.environ.items() if k != "NUKE_TOOLCHAINS"}
		env["NUKE_TEST_ECHO"] = self.echo
		nuke = [sys.executable, os.path.join(self.install, "Nuke", "nuke")]
		handed = subprocess.run(nuke + ["list", "--json"], cwd=os.path.join(self.checkout, "C4"), env=env, capture_output=True, text=True, timeout=120)
		self.assertEqual(handed.returncode, 3, handed.stderr)
		self.assertEqual(self.echoed(), ["list", "--json"])
		listed = subprocess.run(nuke + ["toolchain", "list"], cwd=self.outside, env=env, capture_output=True, text=True, timeout=120)
		self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
		self.assertIn("7.0.0", listed.stdout, "the store of the checkout the install names")


if __name__ == "__main__":
	unittest.main()
