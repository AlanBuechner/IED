"""list and info: what the workspace contains, as text or --json (for IED and scripts)."""

import json

from ..errors import NukeError
from . import base


class List(base.Command):
	name = "list"
	description = "List the workspace's projects (--json for machine-readable output)"
	policy = base.NONE

	def finish(self, session, targets):
		workspace = session.workspace
		default = workspace.default_project()
		rows = [project_summary(session, p, default) for p in workspace.projects.values()]
		if session.json:
			session.log.line(json.dumps({"workspace": workspace.name, "dir": workspace.dir, "projects": rows}, indent=2))
			return 0
		session.log.line(f"{'Project':<24} {'Kind':<11} {'Depends':<28} Folder")
		session.log.line("-" * 96)
		for row in rows:
			flags = (" [default]" if row["default"] else "") + (" [tests]" if row["tests"] else "")
			session.log.line(f"{row['name']:<24} {row['kind']:<11} {', '.join(row['depends']) or '-':<28} {row['dir']}{flags}")
		session.log.line(f"{len(rows)} project(s) in workspace {workspace.name}")
		return 0


def project_summary(session, project, default):
	summary = _summary(session, project, default)
	if project.package is not None:
		origin = project.package
		summary["package"] = {"source": origin.source, "url": origin.url, "path": origin.package, "versioned": origin.line,
			"version": origin.version, "commit": origin.commit, "develop": origin.develop}
	return summary


def _summary(session, project, default):
	return {
		"name": project.name,
		"kind": project.kind,
		"language": project.language,
		"description": project.description,
		"group": project.group,
		"dir": project.dir,
		"manifest": project.file,
		"depends": list(project.depends),
		"tests": session.has_tests(project),
		"default": default is not None and default.name == project.name,
		"output": session.output(project) if project.kind != "module" else None,
	}


class Info(base.Command):
	name = "info"
	description = "Show the workspace, configuration, toolchain, output folders and commands (--json available)"
	policy = base.NONE

	def __init__(self, command_table):
		self.command_table = command_table

	def finish(self, session, targets):
		workspace = session.workspace
		try:
			toolchain = session.toolchain()
			toolchain_info = {"version": toolchain.version, "dir": toolchain.dir, "describe": toolchain.describe()}
		except NukeError as error:
			toolchain_info = {"error": str(error)}
		info = {
			"workspace": {"name": workspace.name, "dir": workspace.dir, "manifest": workspace.spec.file},
			"configuration": session.configuration.name,
			"configurations": [c.name for c in workspace.configurations],
			"arch": session.arch,
			"system": session.system,
			"toolchain": dict(toolchain_info, requested=workspace.spec.toolchain or "latest"),
			"toolchainStore": session.store.root,
			"bin": session.bin_root(),
			"int": session.int_root(),
			"commands": {c.name: c.description for c in self.command_table.values()},
		}
		if session.json:
			session.log.line(json.dumps(info, indent=2))
			return 0
		session.log.line(f"workspace      {workspace.name}  ({workspace.spec.file})")
		session.log.line(f"configuration  {session.configuration.name} {session.arch} {session.system}  (known: {', '.join(info['configurations'])})")
		session.log.line(f"toolchain      {toolchain_info.get('describe') or toolchain_info['error']}  (requested: {info['toolchain']['requested']})")
		if "dir" in toolchain_info:
			session.log.line(f"               {toolchain_info['dir']}")
		session.log.line(f"store          {session.store.root}")
		session.log.line(f"outputs        {info['bin']}")
		session.log.line(f"               {info['int']}")
		session.log.line("commands       " + ", ".join(sorted(info["commands"])))
		return 0
