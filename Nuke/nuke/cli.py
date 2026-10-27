"""The command line: parsing words into commands, a configuration and projects, then running them."""

import difflib
import json
import os
import sys
from dataclasses import dataclass, field

from . import NUKE_VERSION, configs, fsutil, graph, ied_files, installed, packages, toolchains, workspace as workspaces
from .commands import command_table
from .engine import execute, validate_phases
from .errors import NukeError, StepFailed
from .log import Log
from .session import Session

VALUE_OPTIONS = {"-p": "projects", "--project": "projects", "-c": "config", "--config": "config", "-a": "arch",
	"--arch": "arch", "-s": "system", "--system": "system", "-w": "workspace", "--workspace": "workspace", "--filter": "filter",
	"-j": "jobs", "--jobs": "jobs"}
JOBS_ENV = "NUKE_JOBS"
FLAG_OPTIONS = {"--all": "all", "--json": "json", "-v": "verbose", "--verbose": "verbose", "-h": "help", "--help": "help",
	"--version": "version"}
ARGS_SEPARATOR = "--"


@dataclass
class Invocation:
	commands: list = field(default_factory=list)
	command_args: list = field(default_factory=list)
	configs: list = field(default_factory=list)   # every configuration word or -c value
	arch: str = "x64"
	system: str = "windows"
	projects: list = field(default_factory=list)
	workspace: str = None
	filter: str = None
	jobs: str = None
	all: bool = False
	json: bool = False
	verbose: bool = False
	help: bool = False
	version: bool = False
	program_args: list = field(default_factory=list)


@dataclass
class Cli:
	"""What takes-args commands (init, toolchain, help) receive."""
	log: Log
	store: toolchains.ToolchainStore
	workspace: object = None
	cwd: str = "."


def split_program_args(argv):
	if ARGS_SEPARATOR in argv:
		index = argv.index(ARGS_SEPARATOR)
		return argv[:index], argv[index + 1:]
	return list(argv), []


def find_workspace_option(tokens, takes_args_names):
	"""The -w/--workspace value, looking only before a command that owns the rest of the line."""
	for i, token in enumerate(tokens):
		if token in takes_args_names:
			return None
		if token in ("-w", "--workspace") and i + 1 < len(tokens):
			return tokens[i + 1]
		if token.startswith("--workspace="):
			return token.split("=", 1)[1]
	return None


def carried_checkout(argv, cwd, nuke_folder=None):
	"""For the Nuke an IED install carries: the checkout at or above the command line's folder (-w, else `cwd`) that
	holds a Nuke of its own, which runs the command line instead (installed.py); None when this Nuke runs it."""
	own = nuke_folder or toolchains.nuke_folder()
	if not installed.is_install(own):
		return None
	takes_args_names = {name for name, c in command_table().items() if c.takes_args}
	workspace_dir = find_workspace_option(split_program_args(argv)[0], takes_args_names)
	checkout = installed.carrying_checkout(os.path.join(cwd, workspace_dir) if workspace_dir else cwd)
	return checkout if checkout and not fsutil.same_path(checkout, own) else None


def parse(argv, classify, takes_args):
	"""Parse `argv`. `classify(word)` returns "command", "config", "project" or None; `takes_args(word)` tells
	whether a command owns the rest of the line. Pure: no workspace or file access."""
	tokens, program_args = split_program_args(argv)
	inv = Invocation(program_args=program_args)
	i = 0
	while i < len(tokens):
		token = tokens[i]
		key, value = token, None
		if token.startswith("--") and "=" in token:
			key, value = token.split("=", 1)
		if key in VALUE_OPTIONS:
			if value is None:
				if i + 1 >= len(tokens):
					raise NukeError(f"{token} needs a value")
				value = tokens[i + 1]
				i += 1
			_set_option(inv, VALUE_OPTIONS[key], value)
		elif key in FLAG_OPTIONS:
			setattr(inv, FLAG_OPTIONS[key], True)
		elif token.startswith("-") and len(token) > 1:
			raise NukeError(f"unknown option {token} (nuke help lists the options)")
		else:
			kind = classify(token)
			if kind == "command":
				inv.commands.append(token)
				if takes_args(token):
					if len(inv.commands) > 1:
						raise NukeError(f"'{token}' takes the rest of the command line; run it on its own")
					inv.command_args = tokens[i + 1:] + ([ARGS_SEPARATOR] + program_args if program_args else [])
					return inv
			elif kind == "config":
				inv.configs.append(token)
			elif kind == "project":
				inv.projects.append(token)
			else:
				raise NukeError(_unknown_word_message(token, classify))
		i += 1
	return inv


def _set_option(inv, name, value):
	if name == "projects":
		inv.projects.append(value)
	elif name == "config":
		inv.configs.append(value)
	else:
		setattr(inv, name, value)


def _unknown_word_message(word, classify):
	candidates = getattr(classify, "vocabulary", [])
	hint = difflib.get_close_matches(word, candidates, n=1)
	suggestion = f" (did you mean '{hint[0]}'?)" if hint else ""
	return f"'{word}' is not a command, configuration or project{suggestion} (nuke help lists them)"


def make_classifier(table, workspace):
	configurations = workspace.configurations if workspace else configs.BUILT_IN
	project_names = {p.name.lower() for p in workspace.projects.values()} if workspace else set()

	def classify(word):
		if word in table:
			return "command"
		if configs.lookup(word, configurations):
			return "config"
		if word.lower() in project_names:
			return "project"
		return None

	classify.vocabulary = list(table) + [c.name for c in configurations] + [a for c in configurations for a in c.aliases] + \
		([p.name for p in workspace.projects.values()] if workspace else [])
	return classify


# ------------------------------------------------------------------------------------------------ #

def load_workspace_for(start, log, tolerate):
	root = workspaces.find_root(start) if start else None
	if root is None:
		return None
	try:
		return workspaces.load_workspace(root, packages.Options(log))
	except NukeError as error:
		if not tolerate:
			raise
		log.warn(str(error))
		return None


def process_exit_code(code):
	"""Windows exit codes are unsigned 32-bit (a crash is 0xC0000005); sys.exit takes a signed C long."""
	return code - (1 << 32) if code >= (1 << 31) else code


def main(argv=None, log=None, store=None, cwd=None):
	argv = list(sys.argv[1:] if argv is None else argv)
	log = log or Log()
	store = store or toolchains.ToolchainStore(toolchains.default_store_root())
	try:
		return _main(argv, log, store, cwd or os.getcwd())
	except NukeError as error:
		log.line(f"nuke: error: {error}")
		return 1
	except StepFailed as failure:
		log.line(f"nuke: {failure}")
		return failure.code
	except KeyboardInterrupt:
		log.line("nuke: interrupted")
		return 130


def _main(argv, log, store, cwd):
	builtins = command_table()
	takes_args_names = {name for name, c in builtins.items() if c.takes_args}
	workspace_dir = find_workspace_option(split_program_args(argv)[0], takes_args_names)
	first_word = next((t for t in argv if not t.startswith("-")), None)
	tolerate = first_word in builtins and not builtins[first_word].needs_workspace
	start = os.path.abspath(os.path.join(cwd, workspace_dir)) if workspace_dir else cwd
	if workspace_dir and not os.path.isfile(os.path.join(start, "nuke.toml")):
		raise NukeError(f"no nuke.toml in {start}")
	if "--json" in split_program_args(argv)[0]:
		log.json_mode()           # what loading the workspace says (installing packages) stays out of the JSON
	workspace = load_workspace_for(start, log, tolerate)
	table = command_table(workspace)
	inv = parse(argv, make_classifier(table, workspace), lambda word: table[word].takes_args)
	log.verbose = inv.verbose
	if inv.version:
		log.line(f"nuke {NUKE_VERSION}")
		return 0
	cli = Cli(log=log, store=store, workspace=workspace, cwd=cwd)
	if inv.help:
		return table["help"].main(cli, inv.commands[:1])
	if inv.commands and table[inv.commands[-1]].takes_args:
		return table[inv.commands[-1]].main(cli, inv.command_args)
	if workspace is None:
		raise NukeError(f"no nuke.toml with a [workspace] (or [project]) in {start} or any folder above it; "
			"`nuke init` creates one")
	return run_commands(workspace, table, inv, log, store)


def run_commands(workspace, table, inv, log, store):
	graph.validate(workspace)
	validate_phases(workspace, table)
	configuration = choose_configuration(inv.configs, workspace)
	session = Session(workspace, configuration, configs.require_architecture(inv.arch), configs.require_system(inv.system),
		log, store, args=inv.program_args, test_filter=inv.filter, jobs=choose_jobs(inv.jobs))
	session.json = inv.json
	if inv.json:
		log.json_mode()
	selection = [p.name for p in workspace.projects.values()] if inv.all else [workspace.project(n).name for n in inv.projects]
	commands = inv.commands or ["build"]
	log.note(f"workspace {workspace.name}, {configuration.name} {session.arch} {session.system}")
	ied_files.refresh(workspace, store, log)
	try:
		for name in commands:
			code = execute(session, table, name, selection)
			if code:
				return code
		return 0
	finally:
		_write_trace(session)


def choose_jobs(value):
	"""-j N, else NUKE_JOBS, else None (each driver's own choice). A count below 1 is refused."""
	value = value if value is not None else os.environ.get(JOBS_ENV)
	if value is None or value == "":
		return None
	try:
		jobs = int(value)
	except ValueError:
		raise NukeError(f"-j wants a number of jobs (got '{value}')") from None
	if jobs < 1:
		raise NukeError(f"-j wants at least 1 job (got {jobs})")
	return jobs


def choose_configuration(words, workspace):
	chosen = {configs.require(w, workspace.configurations).name: w for w in words}
	if len(chosen) > 1:
		raise NukeError(f"two configurations given: {' and '.join(chosen.values())}")
	return configs.require(words[0], workspace.configurations) if words else workspace.default_configuration


def _write_trace(session):
	if not session.trace:
		return
	events = []
	for name, start, end in session.trace:
		events.append({"name": name, "ph": "X", "pid": 0, "tid": 0, "ts": int(start * 1e6), "dur": int((end - start) * 1e6)})
	path = fsutil.join(session.int_root(), "nuke-trace.json")
	fsutil.write_if_changed(path, json.dumps({"traceEvents": events}) + "\n")
