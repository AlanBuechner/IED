"""NuGet packages against a feed in a folder laid out as nuget.org's flat container: install, the ${nuget:Name} variable,
the lock and its offline fast path, `*` and update, conflicting versions, hashes, pruning, a git package's own NuGet
dependency, and manifest errors."""

import json
import os
import stat
import zipfile

from support import read, WorkspaceTest
from test_packages import Remote

from nuke.packages import nuget


def make_package(feed, package_id, version, files):
	"""A .nupkg in `feed` holding `files` ({path: text}) and NuGet's packaging files, listed in the id's index.json."""
	key, ver = package_id.lower(), version.lower()
	folder = os.path.join(feed, key, ver)
	os.makedirs(folder, exist_ok=True)
	with zipfile.ZipFile(os.path.join(folder, f"{key}.{ver}.nupkg"), "w") as archive:
		archive.writestr("_rels/.rels", "<Relationships/>")
		archive.writestr("[Content_Types].xml", "<Types/>")
		archive.writestr(".signature.p7s", "signature")
		archive.writestr(f"{package_id}.nuspec", f"<package><metadata><id>{package_id}</id><version>{version}</version></metadata></package>")
		for path, text in files.items():
			archive.writestr(path, text)
	index = os.path.join(feed, key, "index.json")
	versions = json.loads(read(index))["versions"] if os.path.isfile(index) else []
	with open(index, "w", encoding="utf-8") as f:
		json.dump({"versions": versions + [ver]}, f)


SDK_FILES = {"build/native/bin/x64/Core.dll": "core", "build/native/include/sdk.h": "#define SDK 1\n"}


class NugetBase(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.feed = os.path.join(self.tmp, "feed")
		self.cache = os.path.join(self.tmp, "cache")
		os.environ[nuget.FEED_ENV] = self.feed
		os.environ["NUKE_PACKAGE_CACHE"] = self.cache

	def tearDown(self):
		os.environ.pop(nuget.FEED_ENV, None)
		os.environ.pop("NUKE_PACKAGE_CACHE", None)
		for base, dirs, files in os.walk(self.tmp):
			for name in files:
				os.chmod(os.path.join(base, name), stat.S_IWRITE | stat.S_IREAD)
		super().tearDown()

	def game(self, dependencies, extra=""):
		"""A workspace whose executable Game names `dependencies` ({name: inline table}) and copies Sdk's binaries."""
		self.files({
			"nuke.toml": "[workspace]\nname = \"Ws\"\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			"Game/nuke.toml": "[project]\nkind = \"executable\"\n" + extra + "[dependencies]\n"
				+ "".join(f"{k} = {v}\n" for k, v in dependencies.items()),
			"Game/src/main.cf": "int32 main() { return 0; }\n",
		})

	def lock(self):
		import tomllib
		with open(self.path("packages", "nuke.lock"), "rb") as f:
			return tomllib.load(f)

	def build(self):
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		return out

	def installed(self, *parts):
		return self.path("packages", "nuget", *parts)

	COPY = "[[project.copy]]\nfrom = \"${nuget:Sdk}/build/native/bin/x64\"\nto = \"SDK\"\n"


class NugetTest(NugetBase):
	def test_a_package_is_installed_read_only_and_its_files_reached_through_its_variable(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'}, self.COPY)
		self.build()
		header = self.installed("test.sdk@1.2.3", "build", "native", "include", "sdk.h")
		self.assertEqual(read(header), "#define SDK 1\n")
		self.assertFalse(os.access(header, os.W_OK), "an installed package is read-only")
		for packaging in ("_rels", "[Content_Types].xml", ".signature.p7s"):
			self.assertFalse(os.path.exists(self.installed("test.sdk@1.2.3", packaging)), packaging)
		copied = os.path.join(self.tmp, "ws", "build", "bin", "Development-windows-x64", "Game", "SDK", "Core.dll")
		self.assertEqual(read(copied), "core")
		entry = self.lock()["nuget"][0]
		self.assertEqual((entry["id"], entry["version"]), ("Test.Sdk", "1.2.3"))
		self.assertEqual(len(entry["sha256"]), 64)

	def test_its_include_folder_reaches_the_compiler(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'}, "includes = [\"${nuget:Sdk}/build/native/include\"]\n")
		self.build()
		description = read(os.path.join(self.tmp, "ws", "build", "int", "Development-windows-x64", "Game", "projects.toml"))
		self.assertIn("packages/nuget/test.sdk@1.2.3/build/native/include", description)

	def test_the_lock_needs_no_feed(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'}, self.COPY)
		self.build()
		os.environ[nuget.FEED_ENV] = os.path.join(self.tmp, "no-feed")
		out = self.build()
		self.assertNotIn("downloading", out)

	def test_star_takes_the_newest_stable_version_and_keeps_it_until_update(self):
		for version in ("1.0.0", "1.10.0", "1.9.0", "2.0.0-preview"):
			make_package(self.feed, "Test.Sdk", version, SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk" }'}, self.COPY)
		self.build()
		self.assertEqual(self.lock()["nuget"][0]["version"], "1.10.0")
		make_package(self.feed, "Test.Sdk", "1.11.0", SDK_FILES)
		self.build()
		self.assertEqual(self.lock()["nuget"][0]["version"], "1.10.0", "the lock keeps its version")
		code, out = self.nuke("package", "update", "Test.Sdk")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.lock()["nuget"][0]["version"], "1.11.0")
		self.assertFalse(os.path.exists(self.installed("test.sdk@1.10.0")), "the old version's install is removed")

	def test_two_projects_wanting_two_versions_are_refused(self):
		make_package(self.feed, "Test.Sdk", "1.0.0", SDK_FILES)
		make_package(self.feed, "Test.Sdk", "2.0.0", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.0.0" }', "Tools": '{ source = "Tools" }'})
		self.files({"Tools/nuke.toml": "[project]\nkind = \"module\"\n[dependencies]\nSdk = { nuget = \"Test.Sdk\", version = \"2.0.0\" }\n",
			"Tools/src/tools.ch": ""})
		code, out = self.nuke("build")
		self.assertNotEqual(code, 0)
		self.assertIn("want different versions", out)
		self.assertIn("Game wants 1.0.0", out)
		self.assertIn("Tools wants 2.0.0", out)

	def test_a_version_the_feed_lacks_says_which_it_has(self):
		make_package(self.feed, "Test.Sdk", "1.0.0", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.5.0" }'})
		code, out = self.nuke("build")
		self.assertNotEqual(code, 0)
		self.assertIn("NuGet package Test.Sdk 1.5.0", out)
		self.assertIn("its newest versions there: 1.0.0", out)

	def test_a_damaged_download_fails_the_locked_hash(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'})
		self.build()
		nuget_root = self.installed()
		for base, dirs, files in os.walk(nuget_root):
			for name in files:
				os.chmod(os.path.join(base, name), stat.S_IWRITE | stat.S_IREAD)
		import shutil
		shutil.rmtree(nuget_root)
		with zipfile.ZipFile(os.path.join(self.cache, "nuget", "test.sdk.1.2.3.nupkg"), "w") as archive:
			archive.writestr("build/native/include/sdk.h", "tampered")
		code, out = self.nuke("build")
		self.assertNotEqual(code, 0)
		self.assertIn("not the package that was locked", out)

	def test_a_package_no_project_names_is_removed_and_unlocked(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'})
		self.build()
		self.game({})
		self.build()
		self.assertFalse(os.path.exists(self.installed()))
		self.assertEqual(self.lock().get("nuget", []), [])

	def test_status_lists_the_packages_and_who_names_them(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		self.game({"Sdk": '{ nuget = "Test.Sdk", version = "1.2.3" }'})
		self.build()
		code, out = self.nuke("package", "status", "--json")
		self.assertEqual(code, 0, out)
		item = json.loads(out)["nuget"][0]
		self.assertEqual((item["id"], item["version"], item["folder"]), ("Test.Sdk", "1.2.3", "packages/nuget/test.sdk@1.2.3"))
		self.assertEqual(item["requiredBy"][0]["project"], "Game")

	def test_manifest_errors_say_what_to_write(self):
		for entry, said in (('{ nuget = "Test.Sdk", source = "Tools" }', "takes nuget and version only"),
				('{ nuget = "Test.Sdk", version = "^1.2" }', "is not a NuGet version"),
				('{ nuget = "Not An Id" }', "is not a NuGet package id")):
			self.game({"Sdk": entry})
			code, out = self.nuke("build")
			self.assertNotEqual(code, 0, entry)
			self.assertIn(said, out, entry)


class NugetFromGitPackageTest(NugetBase):
	def test_a_git_package_brings_its_nuget_dependency_and_its_copies(self):
		make_package(self.feed, "Test.Sdk", "1.2.3", SDK_FILES)
		engine = Remote(self.tmp, "engine")
		engine.commit({"Render/nuke.toml": "[package]\nversion = \"0.1.0\"\n[project]\nname = \"Render\"\nkind = \"module\"\n"
			"[dependencies]\nSdk = { nuget = \"Test.Sdk\", version = \"1.2.3\" }\n" + self.COPY,
			"Render/src/Render/render.ch": ""}, tags=("Render/v0.1.0",))
		self.game({"Render": f'{{ source = "{engine.url}/Render", version = "^0.1.0" }}'})
		self.build()
		copied = os.path.join(self.tmp, "ws", "build", "bin", "Development-windows-x64", "Game", "SDK", "Core.dll")
		self.assertEqual(read(copied), "core")
		self.assertEqual(self.lock()["nuget"][0]["version"], "1.2.3")
		self.assertEqual(len(self.lock()["package"]), 1, "the git package stays locked beside it")
