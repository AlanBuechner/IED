import unittest

import support  # noqa: F401
from nuke import cli
from nuke.errors import NukeError

COMMANDS = {"build", "run", "test", "clean", "toolchain", "init", "kit"}
CONFIGS = {"debug", "dev", "development", "dist", "release"}
PROJECTS = {"game", "engine"}


def classify(word):
	if word in COMMANDS:
		return "command"
	if word.lower() in CONFIGS:
		return "config"
	if word.lower() in PROJECTS:
		return "project"
	return None


classify.vocabulary = sorted(COMMANDS | CONFIGS | PROJECTS)


def parse(*argv):
	return cli.parse(list(argv), classify, lambda word: word in ("toolchain", "init"))


class ParseTests(unittest.TestCase):
	def test_chained_commands_config_and_project_words(self):
		inv = parse("build", "run", "dev", "Game")
		self.assertEqual(inv.commands, ["build", "run"])
		self.assertEqual(inv.configs, ["dev"])
		self.assertEqual(inv.projects, ["Game"])

	def test_options(self):
		inv = parse("-p", "Engine", "--project=Game", "-c", "Debug", "-a", "x64", "-s", "windows", "--filter", "math.*",
			"--all", "--json", "-v", "test")
		self.assertEqual(inv.projects, ["Engine", "Game"])
		self.assertEqual((inv.configs, inv.filter, inv.all, inv.json, inv.verbose), (["Debug"], "math.*", True, True, True))

	def test_program_args_after_separator(self):
		inv = parse("run", "--", "--fullscreen", "build")
		self.assertEqual(inv.commands, ["run"])
		self.assertEqual(inv.program_args, ["--fullscreen", "build"])

	def test_takes_args_command_owns_the_rest(self):
		inv = parse("toolchain", "install", "dev", "--force")
		self.assertEqual(inv.commands, ["toolchain"])
		self.assertEqual(inv.command_args, ["install", "dev", "--force"])

	def test_takes_args_command_cannot_follow_others(self):
		with self.assertRaisesRegex(NukeError, "on its own"):
			parse("build", "toolchain", "list")

	def test_no_words_means_no_commands(self):
		self.assertEqual(parse().commands, [])

	def test_every_configuration_word_is_kept(self):
		self.assertEqual(parse("build", "dev", "-c", "Debug").configs, ["dev", "Debug"])

	def test_unknown_word_suggests(self):
		with self.assertRaisesRegex(NukeError, r"'biuld' is not a command.*did you mean 'build'"):
			parse("biuld")

	def test_unknown_option_and_missing_value(self):
		with self.assertRaisesRegex(NukeError, "unknown option"):
			parse("--frobnicate")
		with self.assertRaisesRegex(NukeError, "needs a value"):
			parse("build", "-p")

	def test_workspace_option_found_before_takes_args_command(self):
		self.assertEqual(cli.find_workspace_option(["-w", "x", "build"], {"toolchain"}), "x")
		self.assertEqual(cli.find_workspace_option(["--workspace=y"], set()), "y")
		self.assertIsNone(cli.find_workspace_option(["toolchain", "-w", "x"], {"toolchain"}))

	def test_exit_codes_fit_a_signed_long(self):
		self.assertEqual(cli.process_exit_code(3), 3)
		self.assertEqual(cli.process_exit_code(0xC0000005), 0xC0000005 - (1 << 32))


if __name__ == "__main__":
	unittest.main()
