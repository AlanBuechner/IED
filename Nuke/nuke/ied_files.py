"""IED's files for a workspace (`nuke init --ied`; IED docs/03): <Name>.ied.sln at the root, a project file per project
under build/ied/ and, in the solution's localPlugins, the IED plugin of the workspace's toolchain (<toolchain>/ied) and
of each language package the workspace pins in [languages] (IED docs/26 section 6). The generator rewrites what it
wrote when the workspace changes, but never a file edited by hand since, nor one it did not write:
.ied/local/generated.json keeps the digest of each file it wrote."""

import hashlib
import json
import os
import re

from . import fsutil, languages
from .errors import NukeError

PROJECTS_DIR = "build/ied"
SHARED_VARIABLES = {"config", "arch"}   # a custom project's output may use these: IED's Nuke presets define them too
RECORD = ".ied/local/generated.json"
PLUGIN_ID = "c4"
BASE_PRESET = "nuke"  # IED's own Nuke preset (its ied.nuke plugin): Build, Run, Test and the toolbar run Nuke
KINDS = {"executable": "executable", "module": "library", "library": "library", "custom": "other"}
ITEMS = ("README.md", "AGENTS.md", "nuke.toml")


def solution_path(workspace):
	return fsutil.join(workspace.dir, f"{workspace.name}.ied.sln")


def project_path(workspace, project):
	return fsutil.join(workspace.dir, PROJECTS_DIR, f"{project.name}.ied.proj")


def _relative(path, start):
	return os.path.relpath(path, start).replace("\\", "/")


def _inside(path, folder):
	relative = os.path.relpath(os.path.abspath(path), os.path.abspath(folder))
	return not relative.startswith("..") and not os.path.isabs(relative)


def plugin_dir(toolchain):
	"""The toolchain's IED plugin folder, or None when it ships none (an older toolchain)."""
	folder = fsutil.join(toolchain.dir, "ied")
	return folder if os.path.isfile(fsutil.join(folder, "ied-plugin.json")) else None


def _where(folder, workspace):
	return _relative(folder, workspace.dir) if _inside(folder, workspace.dir) else folder.replace("\\", "/")


def plugin_id(folder):
	"""The id an IED plugin folder's manifest gives (JSON with // comments), or None."""
	text = fsutil.read_text(fsutil.join(folder, "ied-plugin.json"))
	if text is None:
		return None
	body = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("//"))
	try:
		return json.loads(body).get("id")
	except ValueError:
		return None


def pinned_plugins(workspace):
	"""The IED plugins of the language packages the workspace pins in [languages] (IED loads its own and the
	workspace's .ied/plugins/ by itself): [(id, folder)]."""
	pinned = []
	for package in workspace.language_packages().values():
		identifier = plugin_id(package.folder) if package.pinned else None
		if identifier:
			pinned.append((identifier, package.folder))
	return pinned


def solution(workspace, plugin):
	body = {"version": 1, "name": workspace.name, "projects": [{"file": f"{PROJECTS_DIR}/*.ied.proj", "optional": True}]}
	items = [item for item in ITEMS if os.path.isfile(fsutil.join(workspace.dir, item))]
	if items:
		body["items"] = items
	local = ([{"id": PLUGIN_ID, "path": _where(plugin, workspace)}] if plugin else []) + [{"id": i, "path": _where(f, workspace)} for i, f in pinned_plugins(workspace)]
	if local:
		body["localPlugins"] = local
	body["generator"] = {"run": ["nuke", "init", "--ied"], "generatedPaths": [f"{PROJECTS_DIR}/"]}
	return body


def preset(workspace, spec):
	"""The task preset of a project: its language's Nuke preset, which extends IED's `nuke` with the language's problem
	matchers and launch configuration (IED docs/26 section 6) - the one its package names, else `<language>.nuke` - and
	`nuke` itself without a language."""
	if not spec.language:
		return BASE_PRESET
	package = workspace.language_packages().get(spec.language) if spec.is_driven else None
	return package.preset if package else f"{spec.language}.nuke"


def output(workspace, spec):
	"""The program a project's build leaves, with ${config} and ${arch} for IED to fill (`buildSystem.output`, which
	IED's ${projectOutput} expands); None when there is none Nuke can name."""
	folder = f"${{workspaceDir}}/build/bin/${{config}}-windows-${{arch}}/{spec.name}"
	if spec.is_c4 and spec.kind == "executable":
		return f"{folder}/{spec.name}.exe"
	if spec.is_driven:
		try:
			driver = languages.load_driver(workspace.language_package(spec.language))
		except NukeError:
			return None
		name = driver.program(languages.DriverContext(spec, None, "x64", "windows", "", "", None))
		return f"{folder}/{name}" if name else None
	if spec.kind == "custom" and spec.output and set(re.findall(r"\$\{([^}]*)\}", spec.output)) <= SHARED_VARIABLES:
		path = spec.output if os.path.isabs(spec.output) else os.path.join(spec.dir, spec.output)
		return "${workspaceDir}/" + _relative(path, workspace.dir) if _inside(path, workspace.dir) else path.replace("\\", "/")
	return None


def project(workspace, spec):
	folder = fsutil.join(workspace.dir, PROJECTS_DIR)
	build_system = {"id": "nuke", "projectName": spec.name}
	program = output(workspace, spec)
	if program:
		build_system["output"] = program
	body = {"version": 1, "name": spec.name, "root": _relative(spec.dir, folder), "kind": KINDS.get(spec.kind, "other"),
		"buildSystem": build_system, "presets": [preset(workspace, spec)]}
	if spec.language:
		body["language"] = spec.language
	if spec.depends:
		body["references"] = list(spec.depends)
	return body


def _text(body):
	return json.dumps(body, indent="\t") + "\n"


def _digest(text):
	return hashlib.sha256(text.encode("utf-8")).hexdigest()


class Generated:
	"""What the generator wrote before (workspace-relative path -> digest), and the files it may not touch."""

	def __init__(self, workspace):
		self.workspace = workspace
		self.path = fsutil.join(workspace.dir, RECORD)
		self.digests = json.loads(fsutil.read_text(self.path) or "{}")
		self.kept = []

	def write(self, path, text):
		key = _relative(path, self.workspace.dir)
		current = fsutil.read_text(path)
		if current is not None and current != text and self.digests.get(key) != _digest(current):
			self.kept.append(key)
			return False
		self.digests[key] = _digest(text)
		return fsutil.write_if_changed(path, text)

	def remove(self, path):
		key = _relative(path, self.workspace.dir)
		current = fsutil.read_text(path)
		if current is None or self.digests.get(key) != _digest(current):
			self.kept.append(key)
			return False
		os.remove(path)
		del self.digests[key]
		return True

	def save(self):
		fsutil.write_if_changed(self.path, json.dumps(self.digests, indent=2, sort_keys=True) + "\n")


def generate(workspace, toolchain, log):
	"""Writes the solution and project files; returns the number of files written or removed."""
	generated = Generated(workspace)
	plugin = plugin_dir(toolchain) if toolchain else None
	changed = int(generated.write(solution_path(workspace), _text(solution(workspace, plugin))))
	wanted = set()
	for spec in workspace.projects.values():
		path = project_path(workspace, spec)
		wanted.add(os.path.normcase(path))
		changed += int(generated.write(path, _text(project(workspace, spec))))
	for path in fsutil.files_under(fsutil.join(workspace.dir, PROJECTS_DIR), [".proj"]):
		if path.endswith(".ied.proj") and os.path.normcase(path) not in wanted:
			changed += int(generated.remove(path))
	generated.save()
	for key in generated.kept:
		log.warn(f"{key} was not written by `nuke init --ied` or was edited since; left as it is (delete it to regenerate)")
	if toolchain and not plugin:
		log.warn(f"toolchain {toolchain.version} ships no IED plugin; the solution has no C4 language support (use a newer toolchain)")
	elif plugin and not _inside(plugin, workspace.dir):
		log.warn(f"the C4 plugin is outside the workspace ({plugin}); the solution's localPlugins path is this machine's")
	return changed
