"""Executing commands: required commands first, then for every target project its pre-<cmd> steps, the
command's built-in work, its <cmd> and post-<cmd> steps; then the workspace's <cmd> and post-<cmd> steps.
Each (command, project) pair runs at most once per invocation, so `nuke build run` builds once."""

import difflib

from . import graph
from .commands import base
from .errors import NukeError
from .steps import run_steps


def known_phases(command_names):
	phases = set()
	for name in command_names:
		phases.update({name, f"pre-{name}", f"post-{name}"})
	return phases


def validate_phases(workspace, command_names):
	phases = known_phases(command_names)
	all_steps = list(workspace.steps) + [(s) for p in workspace.projects.values() for s in p.steps]
	for step in all_steps:
		if step.phase not in phases:
			hint = difflib.get_close_matches(step.phase, sorted(phases), n=1)
			suggestion = f" (did you mean '{hint[0]}'?)" if hint else ""
			raise NukeError(f"{step.origin}: step '{step.name}' has unknown phase '{step.phase}'{suggestion}")


def choose_targets(session, command, selection):
	"""The projects `command` works on, in the order it works on them."""
	workspace = session.workspace
	names = list(command.project_override) if command.project_override is not None else selection
	if command.policy == base.NONE:
		return []
	if command.policy == base.TESTS:
		chosen = [workspace.project(n) for n in names] if names else list(workspace.projects.values())
		untested = [p.name for p in chosen if not session.has_tests(p)]
		if names and untested:
			raise NukeError(f"{', '.join(untested)} {'has' if len(untested) == 1 else 'have'} no tests")
		return [p for p in chosen if session.has_tests(p)]
	if command.policy == base.EVERYTHING and not names:
		return list(workspace.projects.values())
	roots = [workspace.project(n) for n in names] if names else [_default_project(workspace)]
	if command.policy == base.SELECTED:
		return roots
	return graph.build_order(workspace, roots)


def _default_project(workspace):
	project = workspace.default_project()
	if project is None:
		raise NukeError(f"workspace {workspace.name} has no default project: pass a project name (or -p NAME), "
			"or set default-project in [workspace]")
	return project


def execute(session, commands, name, selection):
	"""Run command `name` (and what it requires). Returns its exit code; StepFailed and NukeError propagate."""
	command = commands[name]
	required_selection = list(command.project_override) if command.project_override is not None else selection
	for required in command.requires:
		if required not in commands:
			raise NukeError(f"command '{name}' requires unknown command '{required}'")
		code = execute(session, commands, required, required_selection)
		if code:
			return code
	targets = choose_targets(session, command, selection)
	workspace = session.workspace
	first_time = (name, None) not in session.done
	if first_time:
		run_steps(session, workspace.steps, f"pre-{name}")
	for project in targets:
		if (name, project.name) in session.done:
			continue
		session.done.add((name, project.name))
		with session.timed(f"{name} {project.name}"):
			run_steps(session, project.steps, f"pre-{name}", project)
			command.work(session, project)
			run_steps(session, project.steps, name, project)
			run_steps(session, project.steps, f"post-{name}", project)
	if not first_time:
		return 0
	session.done.add((name, None))
	run_steps(session, workspace.steps, name)
	run_steps(session, workspace.steps, f"post-{name}")
	return command.finish(session, targets)
