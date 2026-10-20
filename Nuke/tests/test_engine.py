"""Commands, phases and steps, driven through the CLI with custom projects (no compiler involved)."""

import os
import unittest

from support import read, WorkspaceTest

RECORD = "import sys\nopen(sys.argv[1], 'a').write(' '.join(sys.argv[2:]) + '\\n')\n"


def step(phase, *words, extra=""):
	args = ", ".join(f'"{w}"' for w in words)
	return f'[[project.steps]]\nphase = "{phase}"\nrun = ["${{python}}", "${{workspace.dir}}/record.py", "${{workspace.dir}}/log.txt", {args}]\n{extra}\n'


class EngineTests(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.files({"record.py": RECORD})

	def log(self):
		path = self.path("log.txt")
		return read(path).split("\n")[:-1] if os.path.exists(path) else []

	def workspace(self, extra=""):
		self.files({
			"nuke.toml": "[workspace]\ndefault-project = \"App\"\n" + extra + ("" if "[commands.kit]" in extra else "[commands.kit]\n"),
			"Lib/nuke.toml": "[project]\nkind = \"custom\"\n" + step("pre-build", "Lib", "pre") + step("build", "Lib", "build"),
			"App/nuke.toml": "[project]\nkind = \"custom\"\ndepends = [\"Lib\"]\n" + step("build", "App", "build")
				+ step("post-build", "App", "post") + step("run", "App", "run", "${args}") + step("kit", "App", "kit")
				+ step("pre-build", "App", "dist-only", extra='configs = ["Dist"]'),
		})

	def test_build_runs_phases_in_dependency_order(self):
		self.workspace()
		code, out = self.nuke("build")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.log(), ["Lib pre", "Lib build", "App build", "App post"])

	def test_chained_commands_build_once_and_pass_program_args(self):
		self.workspace()
		code, out = self.nuke("build", "run", "--", "--fast")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.log(), ["Lib pre", "Lib build", "App build", "App post", "App run --fast"])

	def test_configuration_filters_steps(self):
		self.workspace()
		self.nuke("build", "dist")
		self.assertIn("App dist-only", self.log())

	def test_custom_command_requires_and_workspace_steps(self):
		self.workspace('[commands.kit]\ndescription = "Package"\nrequires = ["build"]\n'
			'steps = [{ run = ["${python}", "${workspace.dir}/record.py", "${workspace.dir}/log.txt", "ws", "kit", "${config}"] }]\n')
		code, out = self.nuke("kit", "release")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.log()[-2:], ["App kit", "ws kit Dist"])
		self.assertEqual(self.log()[0], "Lib pre")

	def test_command_project_override(self):
		self.workspace('[commands.libonly]\nrequires = ["build"]\nprojects = ["Lib"]\n')
		code, out = self.nuke("libonly")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.log(), ["Lib pre", "Lib build"])

	def test_failure_stops_with_the_step_exit_code(self):
		self.files({"nuke.toml": "[workspace]\n", "App/nuke.toml": "[project]\nkind = \"custom\"\n"
			"[[project.steps]]\nphase = \"build\"\nrun = [\"${python}\", \"-c\", \"import sys; sys.exit(7)\"]\n"
			+ step("post-build", "never")})
		code, out = self.nuke("build", "run")
		self.assertEqual(code, 7)
		self.assertIn("build step", out)
		self.assertEqual(self.log(), [])

	def test_unknown_phase_is_reported(self):
		self.files({"nuke.toml": "[workspace]\n", "App/nuke.toml": "[project]\nkind = \"custom\"\n" + step("post-biuld", "x")})
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("did you mean 'post-build'", out)

	def test_redefining_a_builtin_command_is_an_error(self):
		self.files({"nuke.toml": "[workspace]\n[commands.build]\n", "App/nuke.toml": "[project]\nkind = \"custom\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("built-in command", out)

	def test_python_steps_and_command_functions(self):
		self.files({
			"nuke.toml": "[workspace]\nscripts = [\"ext.py\"]\n",
			"ext.py": "def register(nuke):\n"
				"\t@nuke.command('stamp', requires=['build'])\n"
				"\tdef stamp(ctx):\n"
				"\t\topen(ctx.expand('${workspace.dir}/log.txt'), 'a').write('stamped ' + ','.join(p.name for p in ctx.targets) + '\\n')\n",
			"App/nuke.toml": "[project]\nkind = \"custom\"\n[[project.steps]]\nphase = \"build\"\npython = \"steps.py:make\"\n",
			"App/steps.py": "def make(ctx):\n\topen(ctx.expand('${workspace.dir}/log.txt'), 'a').write(ctx.project.name + ' ' + ctx.config + '\\n')\n",
		})
		code, out = self.nuke("stamp")
		self.assertEqual(code, 0, out)
		self.assertEqual(self.log(), ["App Development", "stamped App"])

	def test_python_step_exceptions_become_errors(self):
		self.files({"nuke.toml": "[workspace]\n", "App/nuke.toml": "[project]\nkind = \"custom\"\n"
			"[[project.steps]]\nphase = \"build\"\npython = \"steps.py:make\"\n", "App/steps.py": "def make(ctx):\n\traise RuntimeError('nope')\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("RuntimeError: nope", out)

	def test_no_default_project(self):
		self.files({"nuke.toml": "[workspace]\n", "A/nuke.toml": "[project]\nkind = \"custom\"\n", "B/nuke.toml": "[project]\nkind = \"custom\"\n"})
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("no default project", out)
		self.assertEqual(self.nuke("build", "A")[0], 0)
		self.assertEqual(self.nuke("build", "--all")[0], 0)

	def test_no_workspace(self):
		code, out = self.nuke("build")
		self.assertEqual(code, 1)
		self.assertIn("nuke init", out)

	def test_list_and_info_json(self):
		self.workspace()
		import json
		code, out = self.nuke("list", "--json")
		self.assertEqual(code, 0, out)
		listing = json.loads(out)
		self.assertEqual([p["name"] for p in listing["projects"]], ["App", "Lib"])
		self.assertTrue(listing["projects"][0]["default"])
		code, out = self.nuke("info", "--json")
		info = json.loads(out)
		self.assertIn("build", info["commands"])
		self.assertEqual(info["configuration"], "Development")


class ProgramPathTests(WorkspaceTest):
	def test_relative_program_resolves_against_the_step_folder(self):
		from nuke.steps import program_relative_to
		self.assertEqual(program_relative_to(["tools/cook.exe", "-x"], "C:/ws/App"), ["C:/ws/App/tools/cook.exe", "-x"])
		self.assertEqual(program_relative_to(["python", "x.py"], "C:/ws"), ["python", "x.py"])
		self.assertEqual(program_relative_to(["C:/abs/tool.exe"], "C:/ws"), ["C:/abs/tool.exe"])

	@unittest.skipUnless(os.name == "nt", "a .cmd program")
	def test_step_runs_a_program_in_a_subfolder_of_its_project(self):
		manifest = '[project]\nkind = "custom"\n[[project.steps]]\nphase = "build"\nrun = ["tools/hello.cmd"]\n'
		self.files({"nuke.toml": "[workspace]\n", "App/nuke.toml": manifest, "App/tools/hello.cmd": "@echo hello from tools\r\n"})
		code, out = self.nuke("build", cwd=self.tmp)
		self.assertEqual(code, 1)
		code, out = self.nuke("build", "-w", "ws", cwd=self.tmp)
		self.assertEqual(code, 0, out)
		self.assertIn("hello from tools", out)


class IncrementalStepTests(WorkspaceTest):
	def setUp(self):
		super().setUp()
		self.files({
			"nuke.toml": "[workspace]\n",
			"App/nuke.toml": "[project]\nkind = \"custom\"\n[[project.steps]]\nphase = \"build\"\nname = \"cook\"\n"
				"run = [\"${python}\", \"cook.py\"]\ninputs = [\"assets/**/*.txt\"]\noutputs = [\"${bin}/cooked.txt\"]\n",
			"App/cook.py": "import glob, os\nos.makedirs(os.environ.get('OUT', '.'), exist_ok=True)\n"
				"print('cooking')\n",
			"App/assets/a.txt": "a",
		})
		bin_dir = self.path("build", "bin", "Development-windows-x64", "App").replace("\\", "/")
		self.files({"App/cook.py": f"import os\nos.makedirs(r'{bin_dir}', exist_ok=True)\n"
			f"open(r'{bin_dir}/cooked.txt', 'w').write('x')\nprint('cooking')\n"})

	def test_skips_when_outputs_are_newer_than_inputs(self):
		code, out = self.nuke("build")
		self.assertIn("cooking", out)
		code, out = self.nuke("build")
		self.assertEqual(code, 0)
		self.assertIn("build step 'cook': up to date", out)
		self.assertNotIn("cooking", out)

	def test_reruns_when_an_input_changes_or_the_output_is_gone(self):
		self.nuke("build")
		later = os.path.getmtime(self.path("App", "assets", "a.txt")) + 10
		os.utime(self.path("App", "assets", "a.txt"), (later, later))
		self.assertIn("cooking", self.nuke("build")[1])
		os.remove(self.path("build", "bin", "Development-windows-x64", "App", "cooked.txt"))
		self.assertIn("cooking", self.nuke("build")[1])

	def test_reruns_when_the_configuration_changes_the_command(self):
		self.nuke("build")
		self.assertIn("cooking", self.nuke("build", "debug")[1])


if __name__ == "__main__":
	unittest.main()
