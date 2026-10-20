import os
import unittest

from support import read, WorkspaceTest, make_install
from nuke import toolchains
from nuke.errors import NukeError


class StoreTests(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.real = toolchains.ToolchainStore(self.store_root)
		make_install(os.path.join(self.store_root, "c4", "1.10.0"), version="1.10.0")
		make_install(os.path.join(self.store_root, "c4", "dev"), version="1.11.0")
		os.makedirs(os.path.join(self.store_root, "c4", "broken"))

	def test_installed_sorts_numerically_and_skips_incomplete(self):
		self.assertEqual([t.version for t in self.real.installed()], ["1.0.0", "1.10.0", "dev"])

	def test_resolve_exact_latest_dev_and_path(self):
		self.assertEqual(self.real.resolve("1.0.0", self.root).version, "1.0.0")
		self.assertEqual(self.real.resolve("latest", self.root).version, "1.10.0")
		self.assertEqual(self.real.resolve(None, self.root).version, "1.10.0")
		self.assertEqual(self.real.resolve("dev", self.root).metadata["version"], "1.11.0")
		vendored = make_install(os.path.join(self.root, "tc"), version="0.9.0")
		self.assertEqual(self.real.resolve("./tc", self.root).dir, vendored.replace("\\", "/"))

	def test_missing_version_lists_installed(self):
		with self.assertRaisesRegex(NukeError, r"toolchain '2.0.0' is not installed.*1.0.0, 1.10.0, dev"):
			self.real.resolve("2.0.0", self.root)

	def test_incomplete_folder_is_rejected(self):
		with self.assertRaisesRegex(NukeError, "not a complete C4 install"):
			toolchains.open_folder(os.path.join(self.store_root, "c4", "broken"))

	def test_install_from_folder_and_by_name(self):
		image = make_install(os.path.join(self.tmp, "image"), version="2.0.0", commit="def456")
		toolchain, copied = self.real.install(image)
		self.assertEqual(toolchain.version, "2.0.0")
		self.assertGreater(copied, 0)
		frozen, _ = self.real.install("dev")
		self.assertEqual(frozen.version, "1.11.0")

	def test_installed_versions_are_immutable(self):
		image = make_install(os.path.join(self.tmp, "image"), version="1.0.0", commit="other")
		with self.assertRaisesRegex(NukeError, "already installed"):
			self.real.install(image)
		self.real.install(image, force=True)
		self.assertEqual(self.real.get("1.0.0").metadata["commit"], "other")

	def test_reinstalling_the_same_build_is_a_no_op(self):
		image = make_install(os.path.join(self.tmp, "image"), version="1.0.0", commit="abc123")
		self.assertEqual(self.real.install(image)[1], 0)

	def test_install_as_and_bad_version(self):
		image = make_install(os.path.join(self.tmp, "image"), version="3.0.0")
		self.assertEqual(self.real.install(image, as_version="3.0.1")[0].version, "3.0.1")
		with self.assertRaisesRegex(NukeError, "not a version number"):
			self.real.install(image, as_version="banana")

	def test_remove(self):
		self.real.remove("1.10.0")
		self.assertIsNone(self.real.get("1.10.0"))
		with self.assertRaises(NukeError):
			self.real.remove("1.10.0")


class SetToolchainTests(unittest.TestCase):
	def test_replaces_the_line_in_the_workspace_table_only(self):
		text = "[workspace]\nname = \"W\"\ntoolchain = \"1.0.0\"\n\n[project]\ntoolchain = \"keep\"\n"
		self.assertEqual(toolchains.set_workspace_toolchain(text, "2.0.0"),
			"[workspace]\nname = \"W\"\ntoolchain = \"2.0.0\"\n\n[project]\ntoolchain = \"keep\"\n")

	def test_inserts_when_missing(self):
		self.assertEqual(toolchains.set_workspace_toolchain("# c\n[workspace]\nname = \"W\"\n", "dev"),
			"# c\n[workspace]\ntoolchain = \"dev\"\nname = \"W\"\n")

	def test_needs_a_workspace_table(self):
		with self.assertRaises(NukeError):
			toolchains.set_workspace_toolchain("[project]\n", "dev")


class ToolchainCommandTests(WorkspaceTest):
	def test_list_which_use(self):
		self.files({"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\n"})
		code, out = self.nuke("toolchain", "list")
		self.assertEqual(code, 0, out)
		self.assertIn("* 1.0.0", out)
		code, out = self.nuke("toolchain", "which")
		self.assertTrue(out.strip().endswith("c4/1.0.0"), out)
		make_install(os.path.join(self.store_root, "c4", "1.1.0"), version="1.1.0")
		self.assertEqual(self.nuke("toolchain", "use", "1.1.0")[0], 0)
		self.assertIn('toolchain = "1.1.0"', read(self.path("nuke.toml")))
		self.assertEqual(self.nuke("toolchain", "use", "9.9.9")[0], 1)

	def test_install_relative_to_the_current_folder(self):
		make_install(os.path.join(self.root, "img"), version="4.0.0")
		code, out = self.nuke("toolchain", "install", "img")
		self.assertEqual(code, 0, out)
		self.assertTrue(os.path.isdir(os.path.join(self.store_root, "c4", "4.0.0")))

	def test_unknown_action(self):
		self.assertEqual(self.nuke("toolchain", "frob")[0], 1)


if __name__ == "__main__":
	unittest.main()
