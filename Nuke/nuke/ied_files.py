"""IED's files for a workspace (`nuke init --ied`; IED docs/03): <Name>.iedsln at the root, a project file per project
under build/ied/ - and one of kind "plugin" for each IED plugin the workspace develops ([workspace] ied-plugins), in the
"IED Plugins" folder - and, in the solution's localPlugins, the IED plugin of the workspace's toolchain
(<toolchain>/ied) and of each language package the workspace pins in [languages] (IED docs/26 section 6). The generator
rewrites what it wrote when the workspace changes, but never a file edited by hand since, nor one it did not write:
.ied/local/generated.json keeps the digest of each file it wrote. The files it wrote under the names before
(<Name>.ied.sln, *.ied.proj) go once the new ones are written."""

import glob
import hashlib
import json
import os
import re
import tomllib
from dataclasses import dataclass, replace

from . import develop_files, fsutil, languages
from .c4 import ide_bridge
from .errors import NukeError
from .packages.store import web_links

PROJECTS_DIR = "build/ied"
SHARED_VARIABLES = {"config", "arch"}   # a custom project's output may use these: IED's Nuke presets define them too
RECORD = ".ied/local/generated.json"
PLUGIN_ID = "c4"
BASE_PRESET = "nuke"  # IED's own Nuke preset (its ied.nuke plugin): Build, Run, Test and the toolbar run Nuke
KINDS = {"executable": "executable", "module": "library", "library": "library", "custom": "other"}
ITEMS = ("README.md", "AGENTS.md", "nuke.toml")
PLUGIN_GROUP = "IED Plugins"
PACKAGE_GROUP = "Packages"
PLUGIN_MANIFEST = "ied-plugin.json"


@dataclass(frozen=True)
class PluginProject:
	"""An IED plugin the workspace develops, as a project of its solution: its files, no build (IED builds and loads it)."""
	name: str
	dir: str
	group: str = PLUGIN_GROUP


def solution_path(workspace):
	return fsutil.join(workspace.dir, f"{workspace.name}.iedsln")


def old_solution_path(workspace):
	return fsutil.join(workspace.dir, f"{workspace.name}.ied.sln")


def project_path(workspace, project):
	return fsutil.join(workspace.dir, PROJECTS_DIR, f"{project.name}.iedproj")


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


def listed_projects(workspace):
	"""The projects IED's solution shows: those inside the workspace (a language package's library outside it, among IED's
	plugins, builds with the workspace but has no files the Solution Explorer could list)."""
	return [spec for spec in workspace.projects.values() if _inside(spec.dir, workspace.dir)]


def _entry_plugin_folders(workspace, entry):
	"""The folders an ied-plugins entry (a folder or a glob, relative to the workspace) finds that hold an IED plugin."""
	pattern = os.path.join(workspace.dir, entry)
	candidates = sorted(glob.glob(pattern)) if glob.has_magic(entry) else [pattern]
	return [fsutil.norm(folder) for folder in candidates if os.path.isfile(os.path.join(folder, PLUGIN_MANIFEST))]


def plugin_folders(workspace):
	"""The folders of the IED plugins [workspace] ied-plugins names, each once."""
	found = []
	for entry in workspace.spec.ied_plugins:
		found += [folder for folder in _entry_plugin_folders(workspace, entry) if not any(fsutil.same_path(folder, f) for f in found)]
	return found


def plugin_projects(workspace):
	"""The IED plugins the workspace develops as projects of its solution, named by their ids - "<id> plugin" where a
	project has that name already, as project files and IED's project settings are files named after a project and
	Windows folds case (the c4 plugin beside the C4 compiler). A plugin whose manifest names no id is left out."""
	taken = {spec.name.lower() for spec in listed_projects(workspace)}
	projects = []
	for folder in plugin_folders(workspace):
		identifier = plugin_id(folder)
		if not identifier:
			continue
		name = identifier if identifier.lower() not in taken else f"{identifier} plugin"
		taken.add(name.lower())
		projects.append(PluginProject(name, folder))
	return projects


def groups(projects, items=()):
	"""The solution's display folders from the projects' `group` ("C4", "Engine/Tools" nests): sorted by name, each with
	its projects by name, the files `items` puts there ((group, workspace-relative file)) and the folders inside it; []
	when no project names one."""
	root = {"groups": {}, "projects": [], "items": []}

	def node_of(group):
		node = root
		for name in [part for part in group.replace("\\", "/").split("/") if part]:
			node = node["groups"].setdefault(name, {"groups": {}, "projects": [], "items": []})
		return node
	for spec in projects:
		if spec.group:
			node_of(spec.group)["projects"].append(spec.name)
	for group, path in items:
		if group:
			node_of(group)["items"].append(path)

	def written(node):
		folders = []
		for name in sorted(node["groups"], key=str.lower):
			inner = node["groups"][name]
			body = {"name": name}
			if inner["projects"]:
				body["projects"] = sorted(inner["projects"], key=str.lower)
			if inner["groups"]:
				body["groups"] = written(inner)
			if inner["items"]:
				body["items"] = sorted(set(inner["items"]), key=str.lower)
			folders.append(body)
		return folders
	return written(root)


def package_items(workspace, specs):
	"""(group, workspace-relative manifest) of every package manifest that is no project's own - a folder above projects
	whose nuke.toml has [package] alone (Claymore's Core/nuke.toml, an installed Renderer/nuke.toml) - for the solution
	folder of that package, so the Solution Explorer shows every package's manifest."""
	found = {}
	for spec in specs:
		if not _inside(spec.dir, workspace.dir):
			continue
		folder, top = os.path.dirname(spec.dir), (spec.package.folder if spec.package else workspace.dir)
		while _inside(folder, workspace.dir) and fsutil.is_within(folder, top) and not fsutil.same_path(folder, workspace.dir):
			if folder not in found:
				found[folder] = _package_only(folder) and (shown_folder(workspace.dir, spec, folder), _relative(fsutil.join(folder, "nuke.toml"), workspace.dir))
			folder = os.path.dirname(folder)
	return [item for item in found.values() if item]


def _package_only(folder):
	try:
		with open(os.path.join(folder, "nuke.toml"), "rb") as f:
			data = tomllib.load(f)
	except (OSError, tomllib.TOMLDecodeError):
		return False
	return "package" in data and "project" not in data


def solution(workspace, plugin, plugins=()):
	"""The solution: its projects' files, folders (the projects' groups, the plugins' "IED Plugins", the packages'
	manifests), items, local plugins and how it is made again."""
	body = {"version": 1, "name": workspace.name, "projects": [{"file": f"{PROJECTS_DIR}/*.iedproj", "optional": True}]}
	listed = listed_projects(workspace)
	folders = groups([shown(spec) for spec in listed] + list(plugins), package_items(workspace, listed))
	if folders:
		body["groups"] = folders
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


def plugin_project(workspace, plugin):
	"""The project file of an IED plugin the workspace develops: its folder, kind plugin (never the startup project)."""
	folder = fsutil.join(workspace.dir, PROJECTS_DIR)
	return {"version": 1, "name": plugin.name, "root": _relative(plugin.dir, folder), "kind": "plugin"}


def _repo_path(origin, folder):
	"""Where `folder` (inside a package's install or clone) is in its repository."""
	relative = os.path.relpath(folder, origin.folder).replace("\\", "/")
	relative = "" if relative == "." else relative
	return relative if origin.develop else "/".join(part for part in (origin.line, relative) if part)


def shown(spec):
	"""A package's project shows in the Solution Explorer's "Packages" folder, under its source and the folders it sits
	in in its repository (WindowingWin32, at Windowing/Win32, in Packages/claymore/Windowing), whatever group its own
	repository gives it."""
	if spec.package is None:
		return spec
	parent = _repo_path(spec.package, spec.dir).rpartition("/")[0]
	return replace(spec, group="/".join(part for part in (PACKAGE_GROUP, spec.package.source, parent) if part))


def shown_folder(workspace_dir, spec, folder):
	"""The solution folder for `folder`, a folder at or above the project `spec`: in "Packages/<source>/<its path in the
	repository>" for a package's, else its path in the workspace."""
	if spec.package is None:
		return _relative(folder, workspace_dir)
	return "/".join(part for part in (PACKAGE_GROUP, spec.package.source, _repo_path(spec.package, folder)) if part)


def project(workspace, spec):
	folder = fsutil.join(workspace.dir, PROJECTS_DIR)
	build_system = {"id": "nuke", "projectName": spec.name}
	program = output(workspace, spec)
	if program:
		build_system["output"] = program
	if spec.package is not None:
		origin = spec.package
		build_system["package"] = {"source": origin.source, "path": origin.package, "version": origin.version,
			"commit": origin.commit, "mode": "develop" if origin.develop else "locked"}
	body = {"version": 1, "name": spec.name, "root": _relative(spec.dir, folder), "kind": KINDS.get(spec.kind, "other"),
		"buildSystem": build_system, "presets": [preset(workspace, spec)]}
	if spec.language:
		body["language"] = spec.language
	if spec.depends:
		body["references"] = list(spec.depends)
	used = packages_used(workspace, spec)
	if used:
		body["packages"] = used
	return body


def packages_used(workspace, spec):
	"""The packages a project uses directly - those of the projects in its `depends` - for its Dependencies node in the
	Solution Explorer: name, version, source (the repository), detail and the version's page on its host."""
	found = {}
	for name in spec.depends:
		dependency = workspace.find_project(name)
		origin = dependency.package if dependency is not None else None
		if origin is None or (origin.url, origin.package) in found:
			continue
		ref = origin.commit if "+" in origin.version else (f"{origin.line}/v{origin.version}" if origin.line else f"v{origin.version}")
		item = {"name": origin.package or origin.source, "version": origin.version, "source": origin.url,
			"detail": f"{origin.source}, {'developed in a clone' if origin.develop else 'locked'}", "readOnly": not origin.develop}
		link = web_links(origin.url, ref, origin.package).get("tree")
		if link:
			item["link"] = link
		found[(origin.url, origin.package)] = item
	return sorted(found.values(), key=lambda item: item["name"].lower())


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


class _Quiet:
	"""A log whose warnings are details: a refresh repeats on every command, so what `nuke init --ied` warns about
	once shows only with -v."""

	def __init__(self, log):
		self.log = log

	def note(self, text):
		self.log.detail(text)

	def warn(self, text):
		self.log.detail(f"IED solution: {text}")

	def detail(self, text):
		self.log.detail(text)


def generated_by_nuke(workspace):
	"""Whether the workspace's IED solution says `nuke init --ied` makes it (its `generator`), wherever it was written."""
	try:
		body = json.loads(fsutil.read_text(solution_path(workspace)) or "null")
	except ValueError:
		return False
	return isinstance(body, dict) and (body.get("generator") or {}).get("run") == ["nuke", "init", "--ied"]


def refresh(workspace, store, log):
	"""The IED files made again on every command, as a build system keeps the IDE files it generates in step with its
	manifests: a pull that adds a project or an IED plugin needs no `nuke init --ied` before IED shows it. Only for a
	workspace whose solution Nuke makes (and with the bridge on, `ide`), and only with its toolchain found - without it
	the solution would lose its C4 plugin. Returns the number of files written or removed. What the language servers
	read of the develop checkouts is written for any workspace (develop_files.py)."""
	develop_files.write(workspace, log)
	if workspace.spec.ide:
		pruned = ide_bridge.prune(workspace)
		if pruned:
			log.detail(f"removed the language-server options of {pruned} project(s) the workspace no longer has")
	if not workspace.spec.ide or not generated_by_nuke(workspace):
		return 0
	try:
		toolchain = store.resolve(workspace.spec.toolchain, workspace.dir)
	except NukeError as error:
		log.detail(f"IED solution not refreshed: {error}")
		return 0
	changed = generate(workspace, toolchain, _Quiet(log))
	if changed:
		log.note(f"IED solution refreshed: {changed} file(s) written or removed to match the manifests")
	return changed


def generate(workspace, toolchain, log):
	"""Writes the solution and project files; returns the number of files written or removed."""
	generated = Generated(workspace)
	plugin = plugin_dir(toolchain) if toolchain else None
	plugins = plugin_projects(workspace)
	changed = int(generated.write(solution_path(workspace), _text(solution(workspace, plugin, plugins))))
	wanted = set()
	for spec in listed_projects(workspace):
		path = project_path(workspace, spec)
		wanted.add(os.path.normcase(path))
		changed += int(generated.write(path, _text(project(workspace, spec))))
	for developed in plugins:
		path = project_path(workspace, developed)
		wanted.add(os.path.normcase(path))
		changed += int(generated.write(path, _text(plugin_project(workspace, developed))))
	for path in fsutil.files_under(fsutil.join(workspace.dir, PROJECTS_DIR), [".proj", ".iedproj"]):
		if (path.endswith(".ied.proj") or path.endswith(".iedproj")) and os.path.normcase(path) not in wanted:
			changed += int(generated.remove(path))
	if os.path.isfile(old_solution_path(workspace)):
		changed += int(generated.remove(old_solution_path(workspace)))
	generated.save()
	for key in generated.kept:
		log.warn(f"{key} was not written by `nuke init --ied` or was edited since; left as it is (delete it to regenerate)")
	for entry in workspace.spec.ied_plugins:
		if not _entry_plugin_folders(workspace, entry):
			log.warn(f"{workspace.spec.file}: ied-plugins entry '{entry}' finds no folder with an {PLUGIN_MANIFEST}")
	for folder in plugin_folders(workspace):
		if not plugin_id(folder):
			log.warn(f"{_where(folder, workspace)}/{PLUGIN_MANIFEST} names no plugin id; the solution leaves that plugin out")
	if toolchain and not plugin:
		log.warn(f"toolchain {toolchain.version} ships no IED plugin; the solution has no C4 language support (use a newer toolchain)")
	elif plugin and not _inside(plugin, workspace.dir):
		log.warn(f"the C4 plugin is outside the workspace ({plugin}); the solution's localPlugins path is this machine's")
	return changed
