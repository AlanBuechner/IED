"""[[project.copy]] pruning: what an earlier build copied into a bin folder and this one no longer copies is removed;
nothing Nuke did not copy is."""

import os
import stat
import unittest

import support  # noqa: F401
from support import WorkspaceTest
from nuke import copies


class BuildCopyTests(WorkspaceTest):
	"""Through `nuke build` against the stub compiler: Game copies `data`, Engine and Render copy into one `shaders` folder."""

	def setUp(self):
		super().setUp()
		self.files({
			"nuke.toml": "[workspace]\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Render\"]\n[[project.copy]]\nfrom = \"data\"\nexclude = [\"*.tmp\"]\n"
				"[[project.copy]]\nfrom = \"readme.txt\"\nto = \"docs/readme.txt\"\n",
			"Game/src/main.cf": "int32 main() { return 0; }\n",
			"Game/data/level.txt": "level",
			"Game/data/deep/more.txt": "more",
			"Game/readme.txt": "read me",
			"Engine/nuke.toml": "[project]\nkind = \"module\"\n[[project.copy]]\nfrom = \"shaders\"\nto = \"shaders\"\n",
			"Engine/src/engine.ch": "",
			"Engine/shaders/Engine.hlsl": "engine",
			"Render/nuke.toml": "[project]\nkind = \"module\"\n[[project.copy]]\nfrom = \"shaders\"\nto = \"shaders\"\n",
			"Render/src/render.ch": "",
			"Render/shaders/Render.hlsl": "render",
		})

	def bin(self, *parts):
		return self.path("build", "bin", "Development-windows-x64", "Game", *parts)

	def build(self):
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		return out

	def remove(self, *parts):
		path = self.path(*parts)
		os.chmod(path, stat.S_IREAD | stat.S_IWRITE)
		os.remove(path)

	def test_a_removed_source_file_leaves_the_bin_folder(self):
		self.build()
		self.assertTrue(os.path.isfile(self.bin("shaders", "Engine.hlsl")))
		self.remove("Engine", "shaders", "Engine.hlsl")
		out = self.build()
		self.assertFalse(os.path.exists(self.bin("shaders", "Engine.hlsl")))
		self.assertTrue(os.path.isfile(self.bin("shaders", "Render.hlsl")), "the other module's file stays")
		self.assertIn("removed 1 file", out)

	def test_a_renamed_source_file_leaves_only_the_new_name(self):
		self.build()
		os.rename(self.path("Game", "data", "deep", "more.txt"), self.path("Game", "data", "deep", "other.txt"))
		self.build()
		self.assertEqual(["other.txt"], os.listdir(self.bin("data", "deep")))

	def test_a_folder_left_empty_is_removed_with_its_last_file(self):
		self.build()
		self.remove("Game", "data", "deep", "more.txt")
		self.build()
		self.assertFalse(os.path.exists(self.bin("data", "deep")))
		self.assertTrue(os.path.isfile(self.bin("data", "level.txt")))

	def test_two_modules_copying_into_one_folder_keep_each_others_files(self):
		self.build()
		self.build()
		self.assertEqual(["Engine.hlsl", "Render.hlsl"], sorted(os.listdir(self.bin("shaders"))))
		self.remove("Render", "shaders", "Render.hlsl")
		self.build()
		self.assertEqual(["Engine.hlsl"], os.listdir(self.bin("shaders")))

	def test_a_file_moved_between_modules_stays(self):
		self.build()
		os.replace(self.path("Engine", "shaders", "Engine.hlsl"), self.path("Render", "shaders", "Engine.hlsl"))
		self.build()
		self.assertEqual(["Engine.hlsl", "Render.hlsl"], sorted(os.listdir(self.bin("shaders"))))

	def test_a_file_nuke_did_not_copy_stays(self):
		self.build()
		for relative in ("shaders/dropped.hlsl", "data/screenshot.png", "cache/blob.bin", "data/deep/saved.txt"):
			os.makedirs(os.path.dirname(self.bin(*relative.split("/"))), exist_ok=True)
			with open(self.bin(*relative.split("/")), "w") as f:
				f.write("mine")
		self.remove("Engine", "shaders", "Engine.hlsl")
		self.remove("Game", "data", "deep", "more.txt")
		self.build()
		for relative in ("shaders/dropped.hlsl", "data/screenshot.png", "cache/blob.bin", "data/deep/saved.txt", "Game.exe"):
			self.assertTrue(os.path.isfile(self.bin(*relative.split("/"))), relative)
		self.assertTrue(os.path.isdir(self.bin("data", "deep")), "a folder that still holds something stays")

	def test_stale_files_from_before_any_manifest_stay(self):
		self.build()
		os.remove(self.path("build", "int", "Development-windows-x64", "Game", copies.MANIFEST_NAME))
		self.remove("Engine", "shaders", "Engine.hlsl")
		self.build()
		self.assertTrue(os.path.isfile(self.bin("shaders", "Engine.hlsl")), "not told from a file the user dropped: kept")
		self.remove("Render", "shaders", "Render.hlsl")
		self.build()
		self.assertFalse(os.path.exists(self.bin("shaders", "Render.hlsl")), "the manifest the build wrote is used from then on")

	def test_a_vanished_source_folder_removes_its_files(self):
		self.build()
		for relative in ("Game/data/level.txt", "Game/data/deep/more.txt"):
			self.remove(*relative.split("/"))
		os.rmdir(self.path("Game", "data", "deep"))
		os.rmdir(self.path("Game", "data"))
		code, out = self.nuke("build")
		self.assertEqual(0, code, out)
		self.assertIn("does not exist", out)
		self.assertFalse(os.path.exists(self.bin("data")))
		self.assertTrue(os.path.isfile(self.bin("shaders", "Render.hlsl")))

	def test_a_single_file_copy_is_pruned_when_its_source_goes(self):
		self.build()
		self.assertTrue(os.path.isfile(self.bin("docs", "readme.txt")))
		self.remove("Game", "readme.txt")
		self.build()
		self.assertFalse(os.path.exists(self.bin("docs")))

	def test_a_file_that_becomes_excluded_is_removed(self):
		self.files({"Game/data/scratch.tmp": "tmp"})
		self.build()
		self.assertFalse(os.path.exists(self.bin("data", "scratch.tmp")))
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Render\"]\n[[project.copy]]\nfrom = \"data\"\n"})
		self.build()
		self.assertTrue(os.path.isfile(self.bin("data", "scratch.tmp")))
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Render\"]\n[[project.copy]]\nfrom = \"data\"\nexclude = [\"*.tmp\"]\n"})
		self.build()
		self.assertFalse(os.path.exists(self.bin("data", "scratch.tmp")))
		self.assertFalse(os.path.exists(self.bin("docs", "readme.txt")), "a copy entry that went removes what it copied")

	def test_a_read_only_copy_is_removed(self):
		self.files({"Engine/shaders/Locked.hlsl": "locked"})
		os.chmod(self.path("Engine", "shaders", "Locked.hlsl"), stat.S_IREAD)
		self.build()
		self.remove("Engine", "shaders", "Locked.hlsl")
		self.build()
		self.assertFalse(os.path.exists(self.bin("shaders", "Locked.hlsl")))

	def test_the_program_is_not_a_copy(self):
		self.files({"Game/nuke.toml": "[project]\ndepends = [\"Engine\", \"Render\"]\n[[project.copy]]\nfrom = \"data\"\n"})
		self.build()
		self.build()
		self.assertTrue(os.path.isfile(self.bin("Game.exe")))


class ManifestTests(unittest.TestCase):
	def test_a_copy_outside_the_bin_folder_is_not_recorded(self):
		self.assertEqual(["a/b.txt"], copies.relative_files("/bin/G", ["/bin/G/a/b.txt", "/bin/Other/x.txt", "/bin/G/a/b.txt"]))

	def test_an_unreadable_manifest_counts_as_none(self):
		import tempfile
		with tempfile.TemporaryDirectory() as folder:
			self.assertIsNone(copies.read_manifest(folder))
			with open(copies.manifest_path(folder), "w") as f:
				f.write("{ not json")
			self.assertIsNone(copies.read_manifest(folder))
			copies.write_manifest(folder, ["b", "a"])
			self.assertEqual(["a", "b"], copies.read_manifest(folder))

	def test_a_manifest_naming_a_path_outside_the_bin_folder_removes_nothing_there(self):
		import tempfile
		with tempfile.TemporaryDirectory() as root:
			bin_dir = os.path.join(root, "bin")
			os.makedirs(bin_dir)
			outside = os.path.join(root, "keep.txt")
			with open(outside, "w") as f:
				f.write("x")
			self.assertEqual(0, copies.prune(bin_dir, ["../keep.txt"], []))
			self.assertTrue(os.path.isfile(outside))


if __name__ == "__main__":
	unittest.main()
