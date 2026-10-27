"""Packages against real git repositories made in a temporary folder: versions from tags, sub-packages, dependencies
inside a repository and across them, the lock and its offline fast path, updates, developing a source in place,
releasing, and what IED is told."""

import json
import os
import stat
import subprocess
import unittest

from support import read, write_files, WorkspaceTest

from nuke.errors import NukeError
from nuke.packages import semver

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
	text = ""
	if package is not None:
		text += f"[package]\ndescription = \"{package}\"\n"
		if dependencies:
			text += "[package.dependencies]\n" + "".join(f"{k} = {v}\n" for k, v in dependencies.items())
	text += f"[project]\nname = \"{name}\"\nkind = \"module\"\n"
	if depends:
		text += "depends = [" + ", ".join(f'"{d}"' for d in depends) + "]\n"
	return text


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
		sources = sources or {"engine": self.engine.url}
		self.files({
			"nuke.toml": "[workspace]\nname = \"Game\"\ntoolchain = \"1.0.0\"\ndefault-project = \"Game\"\n"
				"[sources]\n" + "".join(f"{k} = \"{v}\"\n" for k, v in sources.items())
				+ "[packages]\n" + "".join(f"{k} = {v}\n" for k, v in packages.items()),
			"Game/nuke.toml": "[project]\ndepends = [" + ", ".join(f'"{d}"' for d in depends) + "]\n",
			"Game/src/main.cf": "int32 main() { return 0; }\n",
		})

	def projects(self):
		code, out = self.nuke("list", "--json")
		self.assertEqual(code, 0, out)
		return {p["name"]: p for p in json.loads(out)["projects"]}

	def lock(self):
		import tomllib
		with open(self.path("nuke.lock"), "rb") as f:
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
			"Renderer/nuke.toml": module("Renderer", ["Windowing"], package="the renderer"),
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
	def test_short_form_needs_a_source(self):
		self.files({"nuke.toml": "[workspace]\n[packages]\nWindowing = \"^1.0\"\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("must name its source", out)

	def test_unknown_source_is_named(self):
		self.files({"nuke.toml": "[workspace]\n[sources]\nengine = \"x\"\n[packages]\nWindowing = \"engin@^1.0\"\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("did you mean 'engine'", out)

	def test_package_tables_belong_to_the_workspace_file(self):
		self.files({"nuke.toml": "[workspace]\n", "Game/nuke.toml": "[project]\n[packages]\nX = \"a@1\"\n"})
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("belong to the workspace file", out)


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
		self.projects()
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

	def test_same_repository_dependencies_are_inferred_from_tags(self):
		self.windowing()
		self.workspace({"Renderer": "\"engine@^1.0\"", "Windowing": "\"engine@^0.1\""}, depends=("Renderer",))
		code, out = self.nuke("list")
		self.assertEqual(code, 1)
		self.assertIn("no version of engine:Windowing", out)
		self.assertIn("the workspace wants ^0.1", out)
		self.assertIn("engine:Renderer 1.0.0", out)

	def test_backtracking_finds_an_older_dependent(self):
		self.windowing()
		self.engine.commit({"Renderer/src/Renderer/renderer.ch": "// 1.1\n"}, tags=["Renderer/v1.1.0"])
		# An older Renderer, made on a branch from Windowing 0.1.0: the only one that works with Windowing ^0.1.
		git("checkout", "--quiet", "-b", "old", "Windowing/v0.1.0", cwd=self.engine.work)
		self.engine.commit({"Renderer/nuke.toml": module("Renderer", ["Windowing"], package="old renderer"),
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
			"nuke.toml": f"[workspace]\n[sources]\nstrings = \"{strings.url}\"\n",
			"Console/nuke.toml": module("Console", ["Strings"], package="console",
				dependencies={"Strings": "{ source = \"strings\", version = \"^1.1\" }"}),
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
		packages = next(g for g in solution["groups"] if g["name"] == "Packages")
		engine = packages["groups"][0]
		self.assertEqual(engine["name"], "engine")
		windowing = engine["groups"][0]
		self.assertEqual((windowing["name"], windowing["projects"]), ("Windowing", ["Windowing"]))
		self.assertEqual(windowing["groups"][0]["projects"], ["Win32"])
		project = json.loads(read(self.path("build", "ied", "Win32.iedproj")))
		self.assertEqual(project["buildSystem"]["package"]["mode"], "locked")
		self.assertEqual(project["buildSystem"]["package"]["version"], "0.2.0")
		self.assertEqual(project["root"], "../../packages/engine/Windowing@0.2.0/Windowing/Win32")


class CommandTests(PackageTest):
	def test_add_writes_the_newest_release_and_remove_drops_it(self):
		self.windowing()
		self.workspace({}, depends=())
		code, out = self.nuke("package", "add", "engine", "Windowing")
		self.assertEqual(code, 0, out)
		self.assertIn('Windowing = "engine@^0.2.0"', read(self.path("nuke.toml")))
		code, out = self.nuke("package", "add", self.engine.url, "Renderer", "^1.0")
		self.assertEqual(code, 0, out)
		self.assertIn('Renderer = "engine@^1.0"', read(self.path("nuke.toml")))
		code, out = self.nuke("package", "remove", "Renderer")
		self.assertEqual(code, 0, out)
		self.assertNotIn("Renderer", read(self.path("nuke.toml")))

	def test_status(self):
		self.windowing()
		self.workspace({"Windowing": "\"engine@^0.1\""})
		self.projects()
		code, out = self.nuke("package", "status", "--fetch")
		self.assertEqual(code, 0, out)
		self.assertIn("engine:Windowing", out)
		self.assertIn("0.2.0 available", out)


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
		self.assertIn('Windowing = "engine@^0.3.0"', read(self.path("nuke.toml")))
		self.assertEqual(self.lock()[("engine", "Windowing")]["version"], "0.3.0")

		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 0, out)
		self.assertFalse(os.path.isdir(os.path.join(checkout, ".git")))
		self.assertEqual(read(self.path("packages", "engine", "Windowing@0.3.0", "Windowing", "src", "Windowing", "window.ch")), "// edited in the game\n")

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

	def test_a_breaking_release_releases_its_dependents_again(self):
		self.engine.commit({"nuke.toml": "[workspace]\n",
			"Windowing/nuke.toml": module("Windowing", package="windows"), "Windowing/src/Windowing/w.ch": "",
			"Input/nuke.toml": module("Input", ["Windowing"], package="input"), "Input/src/Input/i.ch": ""},
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
		code, out = self.nuke("package", "lock", "engine")
		self.assertEqual(code, 0, out)
		lock = self.lock()
		self.assertEqual((lock[("engine", "Windowing")]["version"], lock[("engine", "Input")]["version"]), ("0.2.0", "0.1.1"))

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


if __name__ == "__main__":
	unittest.main()
