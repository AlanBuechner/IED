"""Packages against real git repositories made in a temporary folder: versions from tags, sub-packages, dependencies
inside a repository and across them, the lock and its offline fast path, updates, developing a source in place,
releasing, and what IED is told."""

import json
import os
import shutil
import stat
import subprocess
import threading
import unittest
import zipfile

from support import read, write_files, WorkspaceTest

from nuke import manifest
from nuke.commands import package as package_command
from nuke.errors import NukeError
from nuke.packages import checkouts, semver, store

GIT_IDENTITY = {"GIT_AUTHOR_NAME": "Test", "GIT_AUTHOR_EMAIL": "test@example.com", "GIT_COMMITTER_NAME": "Test",
	"GIT_COMMITTER_EMAIL": "test@example.com"}


def git(*args, cwd=None):
	env = dict(os.environ, **GIT_IDENTITY)
	result = subprocess.run(["git", *args], cwd=cwd, env=env, capture_output=True, text=True)
	if result.returncode != 0:
		raise AssertionError(f"git {' '.join(args)}: {result.stderr}")
	return result.stdout.strip()


class Remote:
	"""A bare repository standing for a hosted one, and a clone of it the test commits through."""

	def __init__(self, folder, name):
		self.url = os.path.join(folder, f"{name}.git").replace("\\", "/")
		self.work = os.path.join(folder, f"{name}-work")
		git("init", "--quiet", "--bare", "-b", "main", self.url)
		git("clone", "--quiet", self.url, self.work)
		git("checkout", "--quiet", "-b", "main", cwd=self.work)

	def commit(self, files, message="change", tags=()):
		write_files(self.work, files)
		git("add", "-A", cwd=self.work)
		git("commit", "--quiet", "-m", message, cwd=self.work)
		for tag in tags:
			git("tag", "-a", tag, "-m", tag, cwd=self.work)
		git("push", "--quiet", "origin", "main", "--tags", cwd=self.work)
		return git("rev-parse", "HEAD", cwd=self.work)


def module(name, depends=(), package=None, dependencies=None):
	"""A module's nuke.toml: a package when `package` (its description) is given; `depends` its own package's projects;
	`dependencies` {name: inline table} the packages it needs."""
	text = ""
	if package is not None:
		text += f"[package]\ndescription = \"{package}\"\n"
	text += f"[project]\nname = \"{name}\"\nkind = \"module\"\n"
	if depends:
		text += "depends = [" + ", ".join(f'"{d}"' for d in depends) + "]\n"
	if dependencies:
		text += "[dependencies]\n" + "".join(f"{k} = {v}\n" for k, v in dependencies.items())
	return text


def needs(url, path, version):
	"""A [dependencies] entry: the package at `path` of the repository at `url`."""
	return f"{{ source = \"{url}/{path}\", version = \"{version}\" }}"


class PackageTest(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.cache = os.path.join(self.tmp, "cache")
		os.environ["NUKE_PACKAGE_CACHE"] = self.cache
		self.engine = Remote(self.tmp, "engine")

	def tearDown(self):
		os.environ.pop("NUKE_PACKAGE_CACHE", None)
		os.environ.pop("NUKE_TOOL_GIT", None)
		for base, dirs, files in os.walk(self.tmp):
			for name in files:
				os.chmod(os.path.join(base, name), stat.S_IWRITE | stat.S_IREAD)
		super().tearDown()

	def workspace(self, packages, depends=("Windowing",), sources=None):
		"""Game: a workspace whose project Game names `packages` in [dependencies] - each {name: "<repository>@<requirement>"
		or a table of source (a repository's name in `sources`), path (else the name), version, branch, rev, writable} - and
		depends on `depends`."""
		sources = sources or {"engine": self.engine.url}
		entries = "".join(f"{name} = {self.entry(name, value, sources)}\n" for name, value in packages.items())
		self.files({
			"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			"Game/nuke.toml": "[project]\ndepends = [" + ", ".join(f'"{d}"' for d in depends) + "]\n"
				+ ("[dependencies]\n" + entries if entries else ""),
			"Game/src/main.cf": "int32 main() { return 0; }\n",
		})

	@staticmethod
	def entry(name, value, sources):
		"""A [dependencies] entry in Nuke's form, { source = "<repository URL>/<folder>", ... }, from a test's shorthand."""
		import tomllib
		parsed = tomllib.loads(f"x = {value}")["x"]
		if isinstance(parsed, str):
			repository, _, requirement = parsed.rpartition("@")
			parsed = {"source": repository, "version": requirement or "*"}
		table = dict(parsed)
		repository, path = table.pop("source"), table.pop("path", name)
		fields = [f'source = "{sources.get(repository, repository)}/{path}"']
		fields += [f"{k} = {str(v).lower() if isinstance(v, bool) else json.dumps(v)}" for k, v in table.items()]
		return "{ " + ", ".join(fields) + " }"

	def projects(self):
		code, out = self.nuke("list", "--json")
		self.assertEqual(code, 0, out)
		return {p["name"]: p for p in json.loads(out)["projects"]}

	def lock(self):
		import tomllib
		with open(self.path("packages", "nuke.lock"), "rb") as f:
			return {(p["name"], p["path"]): p for p in tomllib.load(f)["package"]}

	def windowing(self):
		"""Windowing 0.1.0 and 0.2.0, each with a Win32 sub-package; Renderer 1.0.0 made against Windowing 0.2.0."""
		self.engine.commit({
			"nuke.toml": "[workspace]\nname = \"Engine\"\n",
			"Windowing/nuke.toml": module("Windowing", package="Windows and raw input"),
			"Windowing/src/Windowing/window.ch": "// 0.1\n",
			"Windowing/Win32/nuke.toml": module("Win32", ["Windowing"], package="the Win32 backend"),
			"Windowing/Win32/src/Win32/win32.ch": "",
		}, tags=["Windowing/v0.1.0"])
		self.engine.commit({"Windowing/src/Windowing/window.ch": "// 0.2\n"}, tags=["Windowing/v0.2.0"])
		return self.engine.commit({
			"Renderer/nuke.toml": module("Renderer", package="the renderer", dependencies={"Windowing": needs(self.engine.url, "Windowing", "^0.2")}),
			"Renderer/src/Renderer/renderer.ch": "",
		}, tags=["Renderer/v1.0.0"])


class SemverTests(unittest.TestCase):
	def matches(self, requirement, version):
		return semver.parse_requirement(requirement, "test").matches(semver.parse_version(version))

	def test_requirements(self):
		self.assertTrue(self.matches("^1.2", "1.9.0"))
		self.assertFalse(self.matches("^1.2", "2.0.0"))
		self.assertTrue(self.matches("^0.2", "0.2.7"))
		self.assertFalse(self.matches("^0.2", "0.3.0"))
		self.assertTrue(self.matches("~1.2.3", "1.2.9"))
		self.assertFalse(self.matches("~1.2.3", "1.3.0"))
		self.assertTrue(self.matches("=1.2.3", "1.2.3"))
		self.assertTrue(self.matches(">=1.2, <1.5", "1.4.9"))
		self.assertFalse(self.matches(">=1.2, <1.5", "1.5.0"))
		self.assertTrue(self.matches("*", "7.0.0"))
		self.assertTrue(self.matches("1.2", "1.3.0"))

	def test_prereleases_only_when_named(self):
		self.assertFalse(self.matches("^1.0", "1.1.0-beta.1"))
		self.assertTrue(self.matches("^1.1.0-beta", "1.1.0-beta.2"))

	def test_pseudo_versions_sort_between_releases_and_meet_their_release_requirements(self):
		pseudo = semver.pseudo_version(semver.parse_version("1.2.0"), 3, "1a2b3c4d5e6f")
		self.assertEqual(str(pseudo), "1.2.0+3.1a2b3c4d")
		self.assertLess(semver.parse_version("1.2.0").key(), pseudo.key())
		self.assertLess(pseudo.key(), semver.parse_version("1.2.1").key())
		self.assertTrue(semver.parse_requirement("^1.2", "t").matches(pseudo))

	def test_bump(self):
		self.assertEqual(str(semver.bump(semver.parse_version("1.2.3"), "patch")), "1.2.4")
		self.assertEqual(str(semver.bump(semver.parse_version("1.2.3"), "minor")), "1.3.0")
		self.assertEqual(str(semver.bump(semver.parse_version("1.2.3"), "major")), "2.0.0")

	def test_bad_requirement(self):
		with self.assertRaisesRegex(NukeError, "not a version requirement"):
			semver.parse_requirement("latest", "test")


class ManifestTests(PackageTest):
	def test_a_dependency_is_a_source_and_a_version(self):
		cases = [
			("Windowing = \"engine@^1.0\"", "must be a table"),
			("Windowing = { version = \"^1.0\" }", "needs its source"),
			("Windowing = { source = \"https://x/o/e.git/Windowing\", version = \"^1\", rev = \"1a2b\" }", "one of version, branch and rev"),
			("Windowing = { source = \"https://x/o/e.git/Windowing\", rev = \"1a2b\", writable = true }", "not rev"),
			("Windowing = { source = \"../Windowing\", version = \"^1.0\" }", "a local folder"),
		]
		for entry, message in cases:
			self.files({"nuke.toml": f"[project]\nname = \"Game\"\n[dependencies]\n{entry}\n"})
			code, out = self.nuke("list")
			self.assertEqual(code, 1, entry)
			self.assertIn(message, out, entry)

	def test_the_old_tables_say_what_to_write(self):
		for table in ("[sources]\ne = \"https://x/o/e.git\"", "[packages]\nWindowing = \"e@^1\""):
			self.files({"nuke.toml": f"[workspace]\n{table}\n"})
			code, out = self.nuke("list")
			self.assertEqual(code, 1)
			self.assertIn("is gone: each project lists the packages it uses in its own [dependencies]", out)
		self.files({"nuke.toml": "[workspace]\n", "Lib/nuke.toml": "[package]\n[package.dependencies]\n"
			"X = { source = \"https://x/o/e.git/X\", version = \"^1\" }\n[project]\nname = \"Lib\"\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("is now [dependencies]", out)

	def test_sources_name_a_repository_and_a_folder(self):
		split = manifest.split_source
		self.assertEqual(split("https://github.com/AlanBuechner/Claymore.git/Renderer/RHI/DX12"), ("https://github.com/AlanBuechner/Claymore.git", "Renderer/RHI/DX12"))
		self.assertEqual(split("https://github.com/AlanBuechner/Claymore/Renderer/RHI"), ("https://github.com/AlanBuechner/Claymore", "Renderer/RHI"))
		self.assertEqual(split("git@github.com:me/utils.git/Strings"), ("git@github.com:me/utils.git", "Strings"))
		self.assertEqual(split("https://github.com/me/utils.git"), ("https://github.com/me/utils.git", ""), "a repository that is one package")
		self.assertEqual(split("C:/repos/engine.git/Windowing"), ("C:/repos/engine.git", "Windowing"), "a local repository")
		self.assertEqual(split("../Game"), (None, "../Game"), "a local folder")


class LocalDependencyTests(PackageTest):
	"""A local dependency is a folder of the same package: it links that project, and never reaches outside the package."""

	def test_a_local_dependency_links_a_project_of_the_same_package(self):
		self.files({
			"nuke.toml": "[workspace]\nname = \"Lib\"\n",
			"Lib/nuke.toml": "[package]\nversion = \"1.0.0\"\n[project]\nname = \"Lib\"\nkind = \"module\"\n[dependencies]\n"
				"Core = { source = \"Core\" }\n",
			"Lib/Core/nuke.toml": "[project]\nname = \"LibCore\"\nkind = \"module\"\n",
			"Game/nuke.toml": "[project]\nname = \"Game\"\n[dependencies]\nLib = { source = \"../Lib\" }\n",
		})
		projects = self.projects()
		self.assertIn("LibCore", projects["Lib"]["depends"], "relative to the project")
		self.assertIn("Lib", projects["Game"]["depends"], "a project in no package uses the workspace's")

	def test_a_local_dependency_stays_inside_its_package(self):
		self.files({
			"nuke.toml": "[workspace]\nname = \"Engine\"\n",
			"Windowing/nuke.toml": "[package]\nversion = \"1.0.0\"\n[project]\nname = \"Windowing\"\nkind = \"module\"\n",
			"Input/nuke.toml": "[package]\nversion = \"1.0.0\"\n[project]\nname = \"Input\"\nkind = \"module\"\n[dependencies]\n"
				"Windowing = { source = \"../Windowing\" }\n",
		})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("is not in package Input: a local dependency is a project of the same package", out)
		self.assertIn("named by its repository", out)


class ProjectDependencyTests(PackageTest):
	"""Packages a project names in its own nuke.toml's [dependencies] - with each package's own [dependencies] followed
	from its manifest - pulled by any command and linked to the project with no `depends` written."""

	def engine_with_dependencies(self):
		"""Windowing 0.2.0 and its Win32 backend, which names Windowing in its [dependencies] by the repository's URL."""
		win32 = (f"[package]\nversion = \"0.2.0\"\ndescription = \"the Win32 backend\"\n[project]\nname = \"Win32\"\nkind = \"module\"\n"
			f"[dependencies]\nWindowing = {needs(self.engine.url, 'Windowing', '^0.2')}\n")
		self.engine.commit({
			"nuke.toml": "[workspace]\nname = \"Engine\"\n",
			"Windowing/nuke.toml": "[package]\nversion = \"0.2.0\"\n[project]\nname = \"Windowing\"\nkind = \"module\"\n",
			"Windowing/src/Windowing/window.ch": "",
			"Windowing/Win32/nuke.toml": win32,
			"Windowing/Win32/src/Win32/win32.ch": "",
		}, tags=["Windowing/v0.2.0", "Windowing/Win32/v0.2.0"])

	def test_a_lone_project_s_dependencies_are_pulled_and_linked(self):
		self.engine_with_dependencies()
		self.files({
			"nuke.toml": f"[project]\nname = \"Game\"\nkind = \"executable\"\n[dependencies]\nWin32 = {needs(self.engine.url, 'Windowing/Win32', '^0.2')}\n",
			"src/main.cf": "int32 main() { return 0; }\n",
		})
		projects = self.projects()
		self.assertIn("Win32", projects["Game"]["depends"], "a dependency links its package's project")
		self.assertIn("Windowing", projects["Win32"]["depends"], "and the package's own [dependencies] are followed")
		self.assertEqual(self.lock()[("engine", "Windowing/Win32")]["version"], "0.2.0")
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.2.0")
		self.assertTrue(os.path.isfile(self.path("packages", "nuke.lock")))

	def test_a_repository_builds_its_own_packages_from_its_folders(self):
		self.engine_with_dependencies()
		self.root = self.engine.work                  # the repository's own checkout: its origin is the dependency's URL
		projects = self.projects()
		self.assertEqual(projects["Win32"]["dir"].replace("\\", "/"), self.engine.work.replace("\\", "/") + "/Windowing/Win32")
		self.assertIn("Windowing", projects["Win32"]["depends"])
		self.assertFalse(os.path.exists(self.path("packages")), "nothing fetched: the dependency is this repository's")

	def test_a_version_written_by_hand_is_released_as_it_is(self):
		self.engine_with_dependencies()
		self.root = self.engine.work
		win32 = os.path.join(self.engine.work, "Windowing", "Win32", "nuke.toml")
		write_files(self.engine.work, {"Windowing/Win32/nuke.toml": read(win32).replace('version = "0.2.0"', 'version = "0.2.5"')})
		git("commit", "--quiet", "-am", "Win32 0.2.5", cwd=self.engine.work)
		head = git("rev-parse", "HEAD", cwd=self.engine.work)
		code, out = self.nuke("package", "release", "Windowing/Win32")
		self.assertEqual(code, 0, out)
		self.assertEqual(head, git("rev-list", "-n", "1", "Windowing/Win32/v0.2.5", cwd=self.engine.work), "tagged where it said so: no commit")
		write_files(self.engine.work, {"Windowing/nuke.toml": "[package]\nversion = \"0.3.0\"\n[project]\nname = \"Windowing\"\nkind = \"module\"\n"})
		git("commit", "--quiet", "-am", "Windowing 0.3.0", cwd=self.engine.work)
		code, out = self.nuke("package", "release", "Windowing")
		self.assertEqual(code, 0, out)
		self.assertIn("also releasing Windowing/Win32 0.2.6", out, "Win32 wanted ^0.2")
		self.assertEqual("Release Windowing 0.3.0, Windowing/Win32 0.2.6", git("log", "-1", "--format=%s", "Windowing/v0.3.0", cwd=self.engine.work))
		self.assertIn(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.3.0')}", read(win32))


class WritableTests(PackageTest):
	"""A project's dependency is locked - installed read-only - unless it says writable: then its repository is a clone."""

	def game(self, entry):
		self.files({
			"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n",
			"Game/nuke.toml": "[project]\ndepends = [\"Win32\"]\n" + (f"[dependencies]\n{entry}\n" if entry else ""),
			"Game/src/main.cf": "int32 main() { return 0; }\n",
		})

	def test_a_source_and_a_version_are_all_a_project_needs(self):
		self.windowing()
		self.game(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.1')}")
		code, out = self.nuke("package", "develop", "enGINE", "--at", os.path.join(self.tmp, "nowhere"))
		self.assertEqual(code, 1)
		self.assertNotIn("no repository named", out, "a repository is found by its name")
		projects = self.projects()
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.1.0", "the newest release ^0.1 allows")
		windowing = projects["Windowing"]["dir"]
		self.assertIn("Windowing@0.1.0", windowing.replace("\\", "/"))
		self.assertFalse(os.access(os.path.join(windowing, "nuke.toml"), os.W_OK), "locked: read-only on disk")
		self.assertIn("Win32", projects, "a sub-package the game depends on comes with it")
		code, out = self.nuke("package", "status", "--json")
		self.assertEqual(code, 0, out)
		self.assertEqual([s["name"] for s in json.loads(out[out.index("{"):])["sources"]], ["engine"])

	def test_writable_is_a_clone_and_taking_it_away_locks_again(self):
		self.windowing()
		self.game(f"Windowing = {{ source = \"{self.engine.url}/Windowing\", version = \"^0.2\", writable = true }}")
		projects = self.projects()
		clone = self.path("packages", "engine")
		self.assertTrue(os.path.isdir(os.path.join(clone, ".git")), "the build cloned the repository")
		self.assertTrue(projects["Windowing"]["dir"].replace("\\", "/").endswith("packages/engine/Windowing"))
		self.assertTrue(os.access(os.path.join(clone, "Windowing", "nuke.toml"), os.W_OK), "writable")
		code, out = self.nuke("package", "status")
		self.assertEqual(code, 0, out)
		self.assertIn("engine: developing in packages/engine", out)
		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 1)
		self.assertIn("writable = true", out, "nuke.toml decides, not a command")
		for base, _, files in os.walk(self.path("packages")):
			for name in files:
				os.chmod(os.path.join(base, name), stat.S_IWRITE | stat.S_IREAD)
		shutil.rmtree(self.path("packages"))
		self.projects()
		self.assertTrue(os.path.isdir(os.path.join(clone, ".git")), "another checkout of the workspace clones it again")
		write_files(clone, {"Windowing/src/Windowing/window.ch": "// edited\n"})
		self.game(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.2')}")
		code, out = self.nuke("list", "--json")
		self.assertEqual(code, 1, out)
		self.assertIn("uncommitted change", out, "work no remote has is never thrown away")
		self.assertTrue(os.path.isdir(os.path.join(clone, ".git")), "the clone stays")
		git("checkout", "--", ".", cwd=clone)
		projects = self.projects()
		self.assertFalse(os.path.exists(clone + "/.git"), "back to locked installs")
		self.assertIn("Windowing@0.2.0", projects["Windowing"]["dir"].replace("\\", "/"))
		self.assertFalse(os.path.isdir(self.path("packages", ".engine.develop")), "the clone set aside is gone")

	def test_add_writes_a_source_and_a_version(self):
		self.windowing()
		self.game("")
		code, out = self.nuke("package", "add", f"{self.engine.url}/Windowing")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.2.0')}", read(self.path("Game", "nuke.toml")))
		code, out = self.nuke("package", "add", f"{self.engine.url}/Renderer", "^1.0", "--writable")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Renderer = {{ source = \"{self.engine.url}/Renderer\", version = \"^1.0\", writable = true }}", read(self.path("Game", "nuke.toml")))
		self.assertTrue(os.path.isdir(self.path("packages", "engine", ".git")), "writable: the repository is a clone")


class ResolveTests(PackageTest):
	def test_highest_matching_release_is_installed_read_only_and_locked(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.1\""})
		projects = self.projects()
		self.assertIn("Windowing", projects)
		self.assertNotIn("Win32", projects, "a sub-package nothing depends on stays out")
		entry = self.lock()[("engine", "Windowing")]
		self.assertEqual(entry["version"], "0.1.0")
		self.assertEqual(entry["uses"], ["Windowing"])
		installed = self.path("packages", "engine", "Windowing@0.1.0", "Windowing", "src", "Windowing", "window.ch")
		self.assertEqual(read(installed), "// 0.1\n")
		self.assertFalse(os.access(installed, os.W_OK), "a locked package's files are read-only")

	def test_a_package_names_the_packages_it_needs(self):
		self.engine.commit({
			"nuke.toml": "[workspace]\nname = \"Engine\"\n",
			"Core/nuke.toml": module("History", package="core"),
			"Runtime/nuke.toml": module("Runtime", ["History"], package="runtime"),
		}, tags=["Core/v0.1.0", "Runtime/v0.1.0"])
		self.workspace({"Runtime": "\"engine@^0.1\""}, depends=("Runtime",))
		code, out = self.nuke("list")
		self.assertEqual(code, 1, "a package's projects depend only on its own; another package is a [dependencies] entry")
		self.assertIn("depends on History, which is the package Core - another package: name it in Runtime/nuke.toml [dependencies]", out)

	def test_git_ignores_the_packages_but_search_does_not(self):
		self.windowing()
		git("init", "--quiet", "-b", "main", self.tmp + "/game")
		self.root = os.path.join(self.tmp, "game", "MyGame")       # a workspace in a folder of its repository
		os.makedirs(self.root)
		self.workspace({"Windowing": "\"engine@^0.1\""})
		self.projects()
		self.assertFalse(os.path.isfile(self.path(".gitignore")), "no .gitignore line: IED's search reads those")
		self.assertIn("/MyGame/packages/", read(os.path.join(self.tmp, "game", ".git", "info", "exclude")))
		self.assertEqual("", git("status", "--porcelain", "--untracked-files=all", "--", "MyGame/packages", cwd=self.tmp + "/game"))
		self.assertTrue(os.path.isfile(self.path("packages", "nuke.lock")), "the lock is generated: ignored with the packages")
		self.assertFalse(os.path.exists(self.path("nuke.lock")), "nothing beside nuke.toml")
		os.replace(self.path("packages", "nuke.lock"), self.path("nuke.lock"))     # where Nuke kept it before
		self.projects()
		self.assertTrue(os.path.isfile(self.path("packages", "nuke.lock")) and not os.path.exists(self.path("nuke.lock")), "it moves")
		self.assertEqual(1, read(os.path.join(self.tmp, "game", ".git", "info", "exclude")).count("/MyGame/packages/"), "once")

	def test_a_project_named_by_its_folder_keeps_its_name(self):
		self.engine.commit({"nuke.toml": "[workspace]\n", "Assets/nuke.toml": "[package]\n[project]\nkind = \"module\"\n",
			"Assets/src/Assets/a.ch": ""}, tags=["Assets/v0.1.0"])
		self.workspace({"Assets": "\"engine@^0.1\""}, depends=("Assets",))
		self.assertIn("Assets", self.projects())

	def test_a_workspace_inside_the_repository_is_not_part_of_its_packages(self):
		self.engine.commit({"nuke.toml": "[workspace]\n", "Assets/nuke.toml": module("Assets", package="assets"),
			"Assets/src/Assets/a.ch": "",
			"Assets/Samples/Game/nuke.toml": "[workspace]\n[sources]\ne = \"x\"\n",
			"Assets/Samples/Game/Mod/nuke.toml": module("SampleMod", package="a sample's own"),
		}, tags=["Assets/v0.1.0"])
		self.workspace({"Assets": "\"engine@^0.1\""}, depends=("Assets",))
		projects = self.projects()
		self.assertIn("Assets", projects)
		self.assertNotIn("SampleMod", projects)

	def test_a_sub_package_joins_when_a_project_depends_on_it(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""}, depends=("Win32",))
		projects = self.projects()
		self.assertIn("Win32", projects)
		self.assertIn("Windowing", projects, "Win32 depends on Windowing, released with it")
		self.assertEqual(self.lock()[("engine", "Windowing")]["uses"], ["Windowing", "Windowing/Win32"])

	def test_the_build_compiles_the_package_sources(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		import tomllib
		description = tomllib.loads(read(self.path("build", "int", "Development-windows-x64", "Game", "projects.toml")))
		sources = {p["name"]: p["sources"] for p in description["project"]}
		self.assertTrue(sources["Windowing"][0].endswith("packages/engine/Windowing@0.2.0/Windowing/src"), sources)

	def test_the_lock_is_used_offline_without_git(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		self.projects()
		os.environ["NUKE_TOOL_GIT"] = os.path.join(self.tmp, "no-git.exe")
		self.assertIn("Windowing", self.projects())

	def test_a_newer_release_waits_for_update(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		self.projects()
		self.engine.commit({"Windowing/src/Windowing/window.ch": "// 0.2.1\n"}, tags=["Windowing/v0.2.1"])
		self.projects()
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.2.0")
		code, out = self.nuke("package", "update")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.2.1")
		self.assertFalse(os.path.isdir(self.path("packages", "engine", "Windowing@0.2.0")), "the old install goes")

	def test_a_package_s_dependency_meets_the_workspace_s_requirement(self):
		self.windowing()
		self.workspace({"Renderer": "\"engine@^1.0\"", "Windowing": "\"engine@^0.1\""}, depends=("Renderer",))
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("no version of engine:Windowing", out)
		self.assertIn("project Game wants ^0.1", out)
		self.assertIn("engine:Renderer 1.0.0", out)

	def test_backtracking_finds_an_older_dependent(self):
		self.windowing()
		self.engine.commit({"Renderer/src/Renderer/renderer.ch": "// 1.1\n"}, tags=["Renderer/v1.1.0"])
		# An older Renderer, made on a branch from Windowing 0.1.0: the only one that works with Windowing ^0.1.
		git("checkout", "--quiet", "-b", "old", "Windowing/v0.1.0", cwd=self.engine.work)
		self.engine.commit({"Renderer/nuke.toml": module("Renderer", package="old renderer", dependencies={"Windowing": needs(self.engine.url, "Windowing", "^0.1")}),
			"Renderer/src/Renderer/renderer.ch": "// 0.9\n"}, tags=["Renderer/v0.9.0"])
		git("push", "--quiet", "origin", "old", cwd=self.engine.work)
		self.workspace({"Renderer": "\"engine@>=0.9\"", "Windowing": "\"engine@^0.1\""}, depends=("Renderer",))
		projects = self.projects()
		self.assertIn("Renderer", projects)
		lock = self.lock()
		self.assertEqual(lock[("engine", "Renderer")]["version"], "0.9.0")
		self.assertEqual(lock[("engine", "Windowing")]["version"], "0.1.0")

	def test_dependencies_on_other_sources(self):
		strings = Remote(self.tmp, "strings")
		strings.commit({"nuke.toml": "[workspace]\n", "Strings/nuke.toml": module("Strings", package="text"),
			"Strings/src/Strings/s.ch": ""}, tags=["Strings/v1.0.0", "Strings/v1.1.0"])
		self.engine.commit({
			"nuke.toml": "[workspace]\n",
			"Console/nuke.toml": module("Console", package="console", dependencies={"Strings": needs(strings.url, "Strings", "^1.1")}),
			"Console/src/Console/c.ch": "",
		}, tags=["Console/v0.1.0"])
		self.workspace({"Console": "\"engine@^0.1\""}, depends=("Console",))
		projects = self.projects()
		self.assertEqual(projects["Strings"]["package"]["source"], "strings")
		lock = self.lock()
		self.assertEqual(lock[("strings", "Strings")]["version"], "1.1.0")

	def test_branch_and_no_release(self):
		head = self.engine.commit({"nuke.toml": "[workspace]\n", "Windowing/nuke.toml": module("Windowing", package="w"),
			"Windowing/src/Windowing/w.ch": ""})
		self.workspace({"Windowing": "\"engine@*\""})
		self.projects()
		entry = self.lock()[("engine", "")]
		self.assertEqual(entry["commit"], head)
		self.assertTrue(entry["version"].startswith("0.0.0+"), entry["version"])
		self.workspace({"Windowing": "{ source = \"engine\", version = \"^1.0\" }"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("has no releases", out)

	def test_a_branch_keeps_its_locked_commit_only_while_the_branch_has_it(self):
		self.windowing()
		first = git("rev-list", "-n", "1", "Windowing/v0.1.0", cwd=self.engine.work)
		self.workspace({"Windowing": "{ source = \"engine\", branch = \"main\" }"})
		self.projects()
		main = self.lock()[("engine", "Windowing")]["commit"]
		self.assertEqual(main, git("rev-parse", "main", cwd=self.engine.work))
		self.engine.commit({"Windowing/src/Windowing/window.ch": "// later on main\n"})
		self.projects()
		self.assertEqual(main, self.lock()[("engine", "Windowing")]["commit"], "a newer tip waits for update")
		git("checkout", "--quiet", "-b", "other", first, cwd=self.engine.work)
		other = self.engine.commit({"Windowing/src/Windowing/window.ch": "// other\n"})
		git("push", "--quiet", "origin", "other", cwd=self.engine.work)
		self.workspace({"Windowing": "{ source = \"engine\", branch = \"other\" }"})
		self.projects()
		self.assertEqual(other, self.lock()[("engine", "Windowing")]["commit"], "main's commit is not on other: its tip")

	def test_a_missing_package_names_the_ones_there(self):
		self.windowing()
		self.workspace({"Physics": "\"engine@*\""}, depends=())
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("no Physics/nuke.toml with a [package] table (its packages there: ", out)
		self.assertNotIn("0.0.0+", out, "no made-up version for the repository's tip")

	def test_a_clash_of_project_names_is_an_error(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		self.files({"Windowing/nuke.toml": "[project]\nkind = \"module\"\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("two projects are named 'Windowing'", out)

	def test_ied_is_told_what_is_read_only(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		self.projects()
		readonly = json.loads(read(self.path(".ied", "local", "readonly", "nuke.json")))
		self.assertEqual([p["path"] for p in readonly["paths"]], ["packages/engine/Windowing@0.2.0"])
		self.assertIn("locked package", readonly["paths"][0]["reason"])
		packages = json.loads(read(self.path(".ied", "local", "packages.json")))
		self.assertEqual(packages["packages"][0]["projects"], ["Windowing"])
		self.assertFalse(packages["packages"][0]["writable"])

	def test_the_ied_solution_shows_package_projects_in_a_packages_folder(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""}, depends=("Win32",))
		code, out = self.nuke("init", "--ied")
		self.assertEqual(code, 0, out)
		solution = json.loads(read(self.path("Game.iedsln")))
		self.assertEqual(solution["items"], ["nuke.toml"], "the lock is generated: not an item")
		packages = next(g for g in solution["groups"] if g["name"] == "Packages")
		engine = packages["groups"][0]
		self.assertEqual((engine["name"], engine["projects"]), ("engine", ["Windowing"]), "folders as in the repository")
		windowing = engine["groups"][0]
		self.assertEqual((windowing["name"], windowing["projects"]), ("Windowing", ["Win32"]))
		project = json.loads(read(self.path("build", "ied", "Win32.iedproj")))
		self.assertEqual(project["buildSystem"]["package"]["mode"], "locked")
		self.assertEqual(project["buildSystem"]["package"]["version"], "0.2.0")
		self.assertEqual(project["root"], "../../packages/engine/Windowing@0.2.0/Windowing/Win32")
		self.assertEqual([p["name"] for p in project["packages"]], ["Windowing"], "Win32 uses Windowing")
		game = json.loads(read(self.path("build", "ied", "Game.iedproj")))
		self.assertEqual(game["packages"], [{"name": n, "version": "0.2.0", "source": self.engine.url, "detail": "engine, locked",
			"readOnly": True} for n in ("Windowing", "Windowing/Win32")], "a local repository has no web page to link")

	def test_a_package_manifest_of_no_project_shows_in_its_folder(self):
		self.engine.commit({"nuke.toml": "[workspace]\n", "Core/nuke.toml": "[package]\ndescription = \"the basics\"\n",
			"Core/Base/nuke.toml": module("Base", package="logging"), "Core/Base/src/Base/b.ch": ""}, tags=["Core/v0.1.0"])
		self.workspace({"Core": "\"engine@^0.1\""}, depends=("Base",))
		code, out = self.nuke("init", "--ied")
		self.assertEqual(code, 0, out)
		solution = json.loads(read(self.path("Game.iedsln")))
		core = solution["groups"][0]["groups"][0]["groups"][0]
		self.assertEqual((core["name"], core["projects"], core["items"]), ("Core", ["Base"], ["packages/engine/Core@0.1.0/Core/nuke.toml"]))

	def test_github_links(self):
		from nuke.packages.store import web_links, web_url
		self.assertEqual("https://github.com/me/Engine", web_url("https://github.com/me/Engine.git"))
		self.assertEqual("https://github.com/me/Engine", web_url("git@github.com:me/Engine.git"))
		self.assertEqual("https://github.com/me/Engine", web_url("ssh://git@github.com/me/Engine.git"))
		self.assertIsNone(web_url("C:/repos/engine.git"))
		links = web_links("https://github.com/me/Engine.git", "Windowing/v0.2.0", "Windowing")
		self.assertEqual("https://github.com/me/Engine/tree/Windowing/v0.2.0/Windowing", links["tree"])


class CommandTests(PackageTest):
	def test_add_writes_the_newest_release_and_remove_drops_it(self):
		self.windowing()
		self.workspace({}, depends=())
		code, out = self.nuke("package", "add", f"{self.engine.url}/Windowing")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.2.0')}", read(self.path("Game", "nuke.toml")))
		code, out = self.nuke("package", "add", f"{self.engine.url}/Renderer", "^1.0")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Renderer = {needs(self.engine.url, 'Renderer', '^1.0')}", read(self.path("Game", "nuke.toml")))
		code, out = self.nuke("package", "remove", "Renderer")
		self.assertEqual(code, 0, out)
		self.assertNotIn("Renderer", read(self.path("Game", "nuke.toml")))
		code, out = self.nuke("package", "add", f"{self.engine.url}/Windowing/Win32", "--name", "Backend")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Backend = {needs(self.engine.url, 'Windowing/Win32', '^0.2.0')}", read(self.path("Game", "nuke.toml")))
		code, out = self.nuke("package", "remove", "Windowing/Win32")
		self.assertEqual(code, 0, out)
		self.assertNotIn("Backend", read(self.path("Game", "nuke.toml")), "an entry is removed by its name, source or folder")

	def test_add_a_branch_and_a_nested_package(self):
		self.windowing()
		self.workspace({}, depends=())
		code, out = self.nuke("package", "add", f"{self.engine.url}/Windowing/Win32", "--branch", "main")
		self.assertEqual(code, 0, out)
		self.assertIn(f"Win32 = {{ source = \"{self.engine.url}/Windowing/Win32\", branch = \"main\" }}", read(self.path("Game", "nuke.toml")))
		entry = self.lock()[("engine", "Windowing")]
		self.assertEqual(entry["commit"], git("rev-parse", "main", cwd=self.engine.work), "the branch's tip, locked")
		self.assertEqual(entry["uses"], ["Windowing", "Windowing/Win32"])

	def test_add_a_local_project(self):
		self.workspace({}, depends=())
		self.files({"Lib/nuke.toml": "[project]\nname = \"Lib\"\nkind = \"module\"\n"})
		code, out = self.nuke("package", "add", "../Lib", "--project", "Game")
		self.assertEqual(code, 0, out)
		self.assertIn('Lib = { source = "../Lib" }', read(self.path("Game", "nuke.toml")))
		self.assertIn("Lib", self.projects()["Game"]["depends"])

	def test_status_json_has_the_dependency_graph_and_the_versions(self):
		self.windowing()
		self.workspace({"Renderer": "\"engine@^1.0\""}, depends=("Renderer",))
		code, out = self.nuke("package", "status", "--json")
		self.assertEqual(code, 0, out)
		body = json.loads(out[out.index("{"):])
		self.assertEqual(body["sources"], [{"name": "engine", "url": self.engine.url, "web": None}])
		self.assertEqual(body["requirements"], [{"name": "Renderer", "path": "Renderer", "source": f"{self.engine.url}/Renderer", "url": self.engine.url,
			"requirement": "^1.0", "kind": "version", "by": "project Game", "file": "Game/nuke.toml", "project": "Game"}])
		packages = {p["path"]: p for p in body["packages"]}
		renderer, windowing = packages["Renderer"], packages["Windowing"]
		self.assertEqual([(d["path"], d["version"], d["requirement"]) for d in renderer["dependsOn"]], [("Windowing", "0.2.0", "^0.2")])
		self.assertEqual([(r["requirement"], r["from"] != "") for r in windowing["requiredBy"]], [("^0.2", True)], "Renderer needs it")
		self.assertEqual(renderer["requiredBy"][0]["by"], "project Game")
		self.assertEqual(windowing["versions"], ["0.2.0", "0.1.0"])
		self.assertEqual(renderer["packages"], [{"path": "Renderer", "description": "the renderer", "projects": ["Renderer"],
			"dependencies": [{"name": "Windowing", "source": f"{self.engine.url}/Windowing", "url": self.engine.url, "path": "Windowing", "requirement": "^0.2"}]}])
		state_file = self.path("packages", "nuke-packages.json")
		state = json.loads(read(state_file))
		del state["needs"]
		with open(state_file, "w", encoding="utf-8") as f:
			json.dump(state, f)
		lock_before = read(self.path("packages", "nuke.lock"))
		code, out = self.nuke("package", "status", "--json")
		self.assertEqual(code, 0, out)
		renderer = {p["path"]: p for p in json.loads(out[out.index("{"):])["packages"]}["Renderer"]
		self.assertEqual([d["path"] for d in renderer["dependsOn"]], ["Windowing"], "a state from before the graph gets it")
		self.assertEqual(read(self.path("packages", "nuke.lock")), lock_before, "and the lock stays")

	def test_browse(self):
		self.windowing()
		self.workspace({}, depends=())
		for repository in (self.engine.url, f"{self.engine.url}/Windowing"):
			code, out = self.nuke("package", "browse", repository, "--json")
			self.assertEqual(code, 0, out)
			listed = {p["path"]: p for p in json.loads(out[out.index("{"):])["packages"]}
			self.assertEqual(sorted(listed), ["Renderer", "Windowing", "Windowing/Win32"])
			self.assertEqual((listed["Windowing/Win32"]["line"], listed["Windowing/Win32"]["versions"]), ("Windowing", ["0.2.0", "0.1.0"]))
			self.assertEqual(listed["Renderer"]["description"], "the renderer")
		code, out = self.nuke("package", "browse", os.path.join(self.tmp, "missing.git"))
		self.assertEqual(code, 1)
		self.assertIn("cannot read", out)

	def test_status(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.1\""})
		self.projects()
		code, out = self.nuke("package", "status", "--fetch")
		self.assertEqual(code, 0, out)
		self.assertIn("engine:Windowing", out)
		self.assertIn("0.2.0 available", out)

	def test_status_json_tells_an_update_from_a_breaking_one(self):
		self.windowing()
		self.engine.commit({"Windowing/src/Windowing/fix.ch": ""}, tags=["Windowing/v0.1.1"])
		self.workspace({"Windowing": "\"engine@=0.1.0\""}, depends=())
		code, out = self.nuke("package", "status", "--json", "--fetch")
		self.assertEqual(code, 0, out)
		windowing = {p["path"]: p for p in json.loads(out[out.index("{"):])["packages"]}["Windowing"]
		self.assertEqual((windowing["version"], windowing["newest"], windowing["compatible"]), ("0.1.0", "0.2.0", "0.1.1"))
		self.assertEqual(windowing["versions"], ["0.2.0", "0.1.1", "0.1.0"])


class DevelopTests(PackageTest):
	def test_develop_edit_release_and_lock_again(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		self.projects()
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		checkout = self.path("packages", "engine")
		self.assertTrue(os.path.isdir(os.path.join(checkout, ".git")))
		self.assertFalse(os.path.isdir(self.path("packages", "engine", "Windowing@0.2.0")))
		projects = self.projects()
		self.assertTrue(projects["Windowing"]["dir"].replace("\\", "/").endswith("packages/engine/Windowing"), projects["Windowing"])
		entry = self.lock()[("engine", "Windowing")]
		self.assertEqual((entry["version"], entry["commit"]), ("0.2.0", git("rev-list", "-n", "1", "Windowing/v0.2.0", cwd=checkout)),
			"the tip only changed Renderer: Windowing stays at its release")

		# Edit the package in place, commit: the lock follows without a release.
		write_files(checkout, {"Windowing/src/Windowing/window.ch": "// edited in the game\n"})
		git("commit", "--quiet", "-am", "edit", cwd=checkout)
		self.projects()
		entry = self.lock()[("engine", "Windowing")]
		self.assertEqual(entry["commit"], git("rev-parse", "HEAD", cwd=checkout))
		self.assertTrue(entry["version"].startswith("0.2.0+2."), entry["version"])

		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 1)
		self.assertIn("no remote has", out)

		code, out = self.nuke("package", "release", "Windowing", "--minor")
		self.assertEqual(code, 0, out)
		self.assertIn("Windowing/v0.3.0", git("ls-remote", "--tags", self.engine.url))
		self.assertIn('version = "0.3.0"', read(os.path.join(checkout, "Windowing", "nuke.toml")), "the version is in the manifest")
		self.assertEqual("Release Windowing 0.3.0, Renderer 1.0.1", git("log", "-1", "--format=%s", "Windowing/v0.3.0", cwd=checkout),
			"Renderer wanted Windowing ^0.2: it takes ^0.3.0 and is released again with it")
		self.assertIn(f"Windowing = {needs(self.engine.url, 'Windowing', '^0.3.0')}", read(self.path("Game", "nuke.toml")))
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.3.0")

		write_files(self.root, {"build/develop/Server/go.work": "go 1.21\n"})
		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.isdir(os.path.join(checkout, ".git")))
		self.assertEqual(read(self.path("packages", "engine", "Windowing@0.3.0", "Windowing", "src", "Windowing", "window.ch")), "// edited in the game\n")
		self.assertFalse(os.path.exists(self.path("build", "develop")), "what pointed Go and Rust at the clone goes with it")

	def test_a_checkout_behind_its_remote_is_read_only(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		self.engine.commit({"Windowing/src/Windowing/window.ch": "// someone else\n"})
		code, out = self.nuke("package", "status", "--fetch")
		self.assertEqual(code, 0, out)
		self.assertIn("1 behind", out)
		readonly = json.loads(read(self.path(".ied", "local", "readonly", "nuke.json")))
		self.assertEqual([p["path"] for p in readonly["paths"]], ["packages/engine"])
		self.assertIn("behind", readonly["paths"][0]["reason"])
		code, out = self.nuke("package", "pull", "engine")
		self.assertEqual(code, 0, out)
		readonly = json.loads(read(self.path(".ied", "local", "readonly", "nuke.json")))
		self.assertEqual(readonly["paths"], [])

	def test_a_checkout_that_cannot_be_fetched_does_not_hide_another_behind(self):
		self.windowing()
		utils = Remote(self.tmp, "utils")
		utils.commit({"nuke.toml": "[workspace]\nname = \"Utils\"\n", "Strings/nuke.toml": module("Strings", package="strings"),
			"Strings/src/Strings/strings.ch": ""}, tags=["Strings/v0.1.0"])
		self.workspace({"Windowing": "\"engine@^0.2\"", "Strings": "\"utils@^0.1\""}, depends=("Windowing", "Strings"),
			sources={"engine": self.engine.url, "utils": utils.url})
		for source in ("engine", "utils"):
			code, out = self.nuke("package", "develop", source)
			self.assertEqual(code, 0, out)
		gone = os.path.join(self.tmp, "gone.git").replace("\\", "/")
		git("remote", "set-url", "origin", gone, cwd=self.path("packages", "utils"))
		self.engine.commit({"Windowing/src/Windowing/window.ch": "// someone else\n"})
		code, out = self.nuke("package", "status", "--fetch")
		self.assertEqual(code, 1, out)
		self.assertIn("cannot read " + utils.url, out)
		self.assertIn("1 behind", out, "the clone that was fetched is still told")
		readonly = json.loads(read(self.path(".ied", "local", "readonly", "nuke.json")))
		self.assertEqual([p["path"] for p in readonly["paths"]], ["packages/engine"])
		code, out = self.nuke("package", "status", "--fetch", "--json")
		self.assertEqual(code, 1, out)
		body = json.loads(out)
		self.assertEqual(1, len(body["unfetched"]), body)
		self.assertEqual({"engine": 1, "utils": 0}, {c["source"]: c["behind"] for c in body["checkouts"]})

	def test_a_breaking_release_releases_its_dependents_again(self):
		self.engine.commit({"nuke.toml": "[workspace]\n",
			"Windowing/nuke.toml": module("Windowing", package="windows"), "Windowing/src/Windowing/w.ch": "",
			"Input/nuke.toml": module("Input", package="input", dependencies={"Windowing": needs(self.engine.url, "Windowing", "^0.1")}),
			"Input/src/Input/i.ch": ""},
			tags=["Windowing/v0.1.0", "Input/v0.1.0"])
		self.workspace({"Windowing": "\"engine@^0.1\"", "Input": "\"engine@^0.1\""}, depends=("Input",))
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		checkout = self.path("packages", "engine")
		write_files(checkout, {"Windowing/src/Windowing/w.ch": "// a breaking change\n"})
		git("commit", "--quiet", "-am", "windowing 0.2", cwd=checkout)
		code, out = self.nuke("package", "release", "Windowing", "--minor")
		self.assertEqual(code, 0, out)
		self.assertIn("also releasing Input 0.1.1", out)
		self.assertIn("Input/v0.1.1", git("ls-remote", "--tags", self.engine.url))
		input_manifest = read(os.path.join(checkout, "Input", "nuke.toml"))
		self.assertIn('version = "0.1.1"', input_manifest)
		self.assertIn(f'Windowing = {needs(self.engine.url, "Windowing", "^0.2.0")}', input_manifest, "its requirement moved")
		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 0, out)
		lock = self.lock()
		self.assertEqual((lock[("engine", "Windowing")]["version"], lock[("engine", "Input")]["version"]), ("0.2.0", "0.1.1"))

	@unittest.skipUnless(os.name == "nt", "Windows refuses to rename a folder with a file open in it")
	def test_lock_waits_for_a_program_holding_the_clone(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		held = os.path.join(self.path("packages", "engine"), "Windowing", "nuke.toml")
		write_files(self.root, {"build/develop/gopls.json": "{}\n"})
		original = package_command.HELD_WAIT_SECONDS
		package_command.HELD_WAIT_SECONDS = 0.5
		try:
			with open(held, encoding="utf-8"):
				code, out = self.nuke("package", "lock", "engine")
		finally:
			package_command.HELD_WAIT_SECONDS = original
		self.assertEqual(code, 1, out)
		self.assertIn("is in use by another program", out)
		self.assertTrue(json.loads(read(self.path("packages", "nuke-packages.json")))["develop"], "still developed")
		self.assertTrue(os.path.isdir(self.path("packages", "engine", ".git")))
		self.assertFalse(os.path.exists(self.path("build", "develop")), "what points language servers at the clone went first")

		release = threading.Timer(1.0, lambda: opened.close())
		opened = open(held, encoding="utf-8")
		release.start()
		code, out = self.nuke("package", "lock", "engine")
		release.join()
		self.assertEqual(code, 0, out)
		self.assertTrue(os.path.isdir(self.path("packages", "engine", "Windowing@0.2.0")), "it waited for the file to close")

	def test_drivers_see_the_develop_checkouts(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.2\""})
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		checkout = self.path("packages", "engine")
		write_files(checkout, {"go.mod": "module example.com/engine\n", "Tools/go.mod": "module example.com/engine/tools\n",
			".gitignore": "target/\n", "target/go.mod": "module ignored\n"})
		found = checkouts.of_workspace(manifest.load(self.path("nuke.toml")).workspace)
		self.assertEqual([("engine", self.engine.url)], [(c.source, c.url) for c in found])
		self.assertEqual(["Tools/go.mod", "go.mod"], [os.path.relpath(p, checkout).replace("\\", "/") for p in found[0].files("go.mod")],
			"committed or new, never ignored")
		self.assertTrue(found[0].serves("file:///" + self.engine.url[:-len(".git")]), "Cargo's spelling of the source")

	def test_release_refuses_a_dependency_with_unreleased_changes(self):
		self.windowing()
		self.workspace({"Renderer": "\"engine@^1.0\""}, depends=("Renderer",))
		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		checkout = self.path("packages", "engine")
		write_files(checkout, {"Windowing/src/Windowing/window.ch": "// new api\n"})
		git("commit", "--quiet", "-am", "windowing change", cwd=checkout)
		git("push", "--quiet", "origin", "main", cwd=checkout)
		code, out = self.nuke("package", "release", "Renderer")
		self.assertEqual(code, 1)
		self.assertIn("release it too", out)
		code, out = self.nuke("package", "release", "Renderer", "Windowing")
		self.assertEqual(code, 0, out)
		tags = git("ls-remote", "--tags", self.engine.url)
		self.assertIn("Renderer/v1.0.1", tags)
		self.assertIn("Windowing/v0.2.1", tags)


class RepositorySpellingTests(unittest.TestCase):
	def test_one_repository_spelled_by_other_tools(self):
		for a, b in (("https://github.com/Alan/Claymore.git", "git@github.com:alan/claymore"),
				("ssh://git@host.org:2222/team/engine.git", "https://host.org/team/engine/"),
				("C:/repos/engine.git", "file:///C:/repos/engine"), ("C:/My Repos/engine.git", "file:///C:/My%20Repos/engine.git")):
			self.assertTrue(store.same_repository(a, b), (a, b))
		for a, b in (("https://github.com/a/b", "https://github.com/a/c"), ("https://gitlab.com/a/b", "https://github.com/a/b"),
				("C:/repos/engine", "C:/repos/engine2"), ("https://host.org/Team/x", "https://host.org/team/x")):
			self.assertFalse(store.same_repository(a, b), (a, b))


def real(tool):
	return shutil.which(tool) is not None


def go_version(version):
	return f"package netcode\n\nfunc Version() string {{ return \"{version}\" }}\n"


def rust_version(version):
	return f"pub fn version() -> &'static str {{ \"{version}\" }}\n"


@unittest.skipUnless(real("go") and real("cargo"), "needs go and cargo")
class NativeToolTests(PackageTest):
	"""A Go and a Rust project of the workspace get a module and a crate of a package repository through their own tools -
	a Go module proxy standing for the hosted one, a Cargo git dependency at the package's tag; developing the source
	builds them from the clone without a release, and locking it again goes back to the releases (IED docs/31 section 7).
	The tools' caches are the test's own."""

	TOOL_ENV = ("GOPROXY", "GOSUMDB", "GOMODCACHE", "GOFLAGS", "GOWORK", "CARGO_HOME")

	def setUp(self):
		super().setUp()
		self.saved = {name: os.environ.get(name) for name in self.TOOL_ENV}
		self.proxy = os.path.join(self.tmp, "goproxy")
		os.environ.update({"GOPROXY": "file:///" + self.proxy.replace("\\", "/"), "GOSUMDB": "off", "GOFLAGS": "-modcacherw",
			"GOMODCACHE": os.path.join(self.tmp, "gomodcache"), "CARGO_HOME": os.path.join(self.tmp, "cargo-home")})
		os.environ.pop("GOWORK", None)

	def tearDown(self):
		for name, value in self.saved.items():
			if value is None:
				os.environ.pop(name, None)
			else:
				os.environ[name] = value
		super().tearDown()

	def publish_module(self, module, version, files):
		"""The module version in the file proxy, as a hosted proxy serves it."""
		folder = os.path.join(self.proxy, *module.split("/"), "@v")
		write_files(folder, {"list": f"{version}\n", f"{version}.info": json.dumps({"Version": version, "Time": "2026-10-27T00:00:00Z"}),
			f"{version}.mod": files["go.mod"]})
		with zipfile.ZipFile(os.path.join(folder, f"{version}.zip"), "w") as archive:
			for name, text in files.items():
				archive.writestr(f"{module}@{version}/{name}", text)

	def engine_with_a_module_and_a_crate(self):
		netcode = {"go.mod": "module example.com/engine/netcode\n\ngo 1.21\n", "netcode.go": go_version("0.1.0")}
		self.engine.commit({"nuke.toml": "[workspace]\nname = \"Engine\"\n",
			"Netcode/nuke.toml": "[package]\ndescription = \"networking, a Go module\"\n",
			"Mathx/nuke.toml": "[package]\ndescription = \"maths, a Rust crate\"\n",
			"Mathx/Cargo.toml": "[package]\nname = \"mathx\"\nversion = \"0.1.0\"\nedition = \"2021\"\n",
			"Mathx/src/lib.rs": rust_version("0.1.0"),
			**{f"Netcode/{name}": text for name, text in netcode.items()}}, tags=["Netcode/v0.1.0", "Mathx/v0.1.0"])
		self.publish_module("example.com/engine/netcode", "v0.1.0", netcode)

	def game_with_go_and_rust(self):
		self.files({
			"nuke.toml": "[workspace]\nname = \"Game\"\n",
			"Server/nuke.toml": f"[project]\nlanguage = \"go\"\n[dependencies]\nNetcode = {needs(self.engine.url, 'Netcode', '^0.1')}\n",
			"Server/go.mod": "module example.com/game/server\n\ngo 1.21\n\nrequire example.com/engine/netcode v0.1.0\n",
			"Server/main.go": "package main\n\nimport (\n\t\"fmt\"\n\n\t\"example.com/engine/netcode\"\n)\n\n"
				"func main() { fmt.Println(\"netcode \" + netcode.Version()) }\n",
			"Game/nuke.toml": f"[project]\nlanguage = \"rust\"\n[dependencies]\nMathx = {needs(self.engine.url, 'Mathx', '^0.1')}\n",
			"Game/Cargo.toml": "[package]\nname = \"game\"\nversion = \"0.1.0\"\nedition = \"2021\"\n\n[dependencies]\n"
				f"mathx = {{ git = \"file:///{self.engine.url}\", tag = \"Mathx/v0.1.0\" }}\n",
			"Game/src/main.rs": "fn main() { println!(\"mathx {}\", mathx::version()); }\n",
		})
		subprocess.run(["go", "mod", "tidy"], cwd=self.path("Server"), check=True, capture_output=True)   # go.sum, as its author has it

	def built(self):
		"""What the Go and the Rust program print after `nuke build`."""
		code, out = self.nuke("build", "--all")
		self.assertEqual(code, 0, out)
		folder = self.path("build", "bin", "Development-windows-x64")
		return tuple(subprocess.run([os.path.join(folder, *program)], capture_output=True, text=True).stdout.strip()
			for program in (("Server", "Server.exe"), ("Game", "game.exe")))

	def own_files(self):
		return {name: read(self.path(*name.split("/"))) for name in ("Server/go.mod", "Server/go.sum", "Game/Cargo.toml", "Game/Cargo.lock")}

	def test_an_edit_in_the_clone_builds_without_a_release_until_locked(self):
		self.engine_with_a_module_and_a_crate()
		self.game_with_go_and_rust()
		self.assertEqual(("netcode 0.1.0", "mathx 0.1.0"), self.built())
		before = self.own_files()

		code, out = self.nuke("package", "develop", "engine")
		self.assertEqual(code, 0, out)
		checkout = self.path("packages", "engine")
		gopls = json.loads(read(self.path("build", "develop", "gopls.json")))["build.env"]
		self.assertIn(json.dumps(os.path.normpath(os.path.join(checkout, "Netcode"))), read(gopls["GOWORK"]),
			"IED's gopls is pointed at the clone by develop itself, before any build")
		analyzer = json.loads(read(self.path("build", "develop", "rust-analyzer.json")))["cargo"]
		self.assertIn("[patch.", read(analyzer["configPath"]))
		write_files(checkout, {"Netcode/netcode.go": go_version("edited"), "Mathx/src/lib.rs": rust_version("edited")})
		self.assertEqual(("netcode edited", "mathx edited"), self.built())
		self.assertEqual(before, self.own_files(), "go.mod, go.sum, Cargo.toml and Cargo.lock stay as they are")
		self.assertIn(json.dumps(os.path.normpath(os.path.join(checkout, "Netcode"))), read(self.path("build", "develop", "Server", "go.work")))
		self.assertIn("[patch.", read(self.path("build", "develop", "Game", "cargo.toml")))

		git("checkout", "--quiet", "--", ".", cwd=checkout)
		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.exists(self.path("build", "develop")))
		self.assertEqual(("netcode 0.1.0", "mathx 0.1.0"), self.built())
		self.assertEqual(before, self.own_files())


if __name__ == "__main__":
	unittest.main()
