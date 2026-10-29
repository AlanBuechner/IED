"""Compiling one C4 program (or test harness): the C4.exe command line, the incremental check and the run.

A project is rebuilt when its output is missing, the command line or build description changed, or any tracked
file - the workspace sources and the toolchain's compiler, default library and runtime - was added, removed or
changed since the last successful compile began: their states (mtime and size) are taken before the compiler runs
and written only after it succeeds, so an edit made while it ran (another build finishing later included) and a
failed compile are never mistaken for an up-to-date build."""

import os
import re
from dataclasses import dataclass

from .. import fsutil, process
from ..errors import StepFailed
from ..variables import expand, expand_argv
from . import description

ARGS_STAMP = "compile.args"
DEPENDENCY_STAMP = "deps.d"
DESCRIPTION_FILE = "projects.toml"
TEST_RESULTS_FILE = "results.json"
# The toolchain's parts a program is made from: a new CBind binds the program's foreign headers again (C4 keeps each
# binding while CBind is unchanged), so a CBind fix reaches every program at its next build.
TOOLCHAIN_TRACKED = ("Bedrock", "C4Runtime", "CBind")


@dataclass(frozen=True)
class CompileJob:
	label: str
	argv: tuple
	output: str
	int_dir: str
	description_path: str
	description_text: str
	tracked: tuple
	modules: tuple
	test_results: str = None
	cwd: str = None


def plan(session, project, test=False):
	toolchain = session.toolchain()
	configuration = session.configuration
	int_dir = session.int_dir(project) + ("/tests" if test else "")
	output = session.test_output(project) if test else session.output(project)
	modules = description.modules_for(session.workspace, project, configuration, lambda module, text: expand(text, session.variables(module)))
	description_path = fsutil.join(int_dir, DESCRIPTION_FILE)
	argv = toolchain.compiler_command() + ["--projects", description_path, f"-O{configuration.optimize}"]
	if configuration.debug_info:
		argv.append("-g")
	if configuration.incremental and toolchain.accepts("--incremental"):
		argv.append("--incremental")
	results = None
	if test:
		results = fsutil.join(int_dir, TEST_RESULTS_FILE)
		argv += ["-test", "-test-json", results]
	for name, value in configuration.defines.items():
		argv += ["-D", f"{name}={value}"]
	for link in collect_links(session, project):
		argv += ["-l", link]
	if project.subsystem != "console" and not test:
		argv += ["--subsystem", project.subsystem]
	argv += expand_argv(compiler_args(project, configuration), session.variables(project))
	argv += ["--int-dir", int_dir, "-o", output]
	tracked = description.tracked_files(modules) + _toolchain_files(toolchain)
	return CompileJob(label=f"{project.name}{' tests' if test else ''}", argv=tuple(argv), output=output, int_dir=int_dir,
		description_path=description_path, description_text=description.render(project.name, modules),
		tracked=tuple(tracked), modules=tuple(modules), test_results=results, cwd=project.dir)


def collect_links(session, project):
	"""Every module's links, each expanded with that module's variables (a path in its own ${int})."""
	links = []
	for module in session.c4_modules(project):
		override = module.config_overrides.get(session.configuration.name)
		for link in expand_argv(list(module.links + (override.links if override else ())), session.variables(module)):
			if link not in links:
				links.append(link)
	return links


def compiler_args(project, configuration):
	override = project.config_overrides.get(configuration.name)
	return list(project.compiler_args) + list(override.compiler_args if override else ())


def _toolchain_files(toolchain):
	files = [toolchain.compiler_path()]
	for folder in TOOLCHAIN_TRACKED:
		files.extend(fsutil.files_under(fsutil.join(toolchain.dir, folder)))
	return files


def _stamp_text(job):
	return "\n".join(job.argv) + "\n"


def is_up_to_date(job):
	if not os.path.isfile(job.output):
		return False
	if fsutil.read_text(fsutil.join(job.int_dir, ARGS_STAMP)) != _stamp_text(job):
		return False
	if fsutil.read_text(job.description_path) != job.description_text:
		return False
	return fsutil.read_text(fsutil.join(job.int_dir, DEPENDENCY_STAMP)) == fsutil.input_states(job.tracked)


# The compiler's progress ("[10:2:31] ENGINE: Phase 1: Parsing"): shown with --verbose; its errors and warnings always.
COMPILER_PROGRESS = re.compile(r"^\[\d+:\d+:\d+\] ENGINE: (?!.*\b(?:error|warning)s?\b)")


def compile_program(session, job):
	"""Compile when out of date. Returns True if the compiler ran; raises StepFailed when it fails."""
	if is_up_to_date(job):
		session.log.note(f"{job.label}: up to date")
		return False
	fsutil.ensure_dir(job.int_dir)
	fsutil.ensure_dir(os.path.dirname(job.output))
	for stamp in (ARGS_STAMP, DEPENDENCY_STAMP):
		if os.path.exists(fsutil.join(job.int_dir, stamp)):
			os.remove(fsutil.join(job.int_dir, stamp))
	fsutil.write_if_changed(job.description_path, job.description_text)
	aside = fsutil.move_aside_if_running(job.output)
	if aside:
		session.log.note(f"{os.path.basename(job.output)} is running: moved aside to {os.path.basename(aside)}")
	session.log.note(f"compiling {job.label} with {session.toolchain().describe()}")
	inputs = fsutil.input_states(job.tracked)
	code = process.run(list(job.argv), session.log, cwd=job.cwd, chatter=COMPILER_PROGRESS)
	if code != 0:
		raise StepFailed(f"compiling {job.label}", code)
	fsutil.write_if_changed(fsutil.join(job.int_dir, ARGS_STAMP), _stamp_text(job))
	fsutil.write_if_changed(fsutil.join(job.int_dir, DEPENDENCY_STAMP), inputs)
	return True
