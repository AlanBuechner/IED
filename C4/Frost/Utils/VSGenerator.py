"""
VSGenerator — Direct Visual Studio project file generator.

Generates deterministic, valid Visual Studio 2022 solution and NMake-style
project files directly from Python without requiring Premake.

Produces:
  - .sln (solution file with groups and nested projects)
  - .vcxproj (NMake-style C++ project files delegating to Build.py)
  - .vcxproj.filters (file filter trees for Solution Explorer)
  - .vcxproj.user (per-user debugger & show-all-files settings)

All GUIDs are stable content-based hashes so Visual Studio tracking is
preserved across regeneration runs.
"""

import hashlib
import os
from pathlib import Path

# ---------------------------------------------------------------------------
# Deterministic GUID generation
# ---------------------------------------------------------------------------

def _content_guid(key: str) -> str:
	"""Return a deterministic 32-hex-char GUID from *key*.

	The format mimics real Visual Studio GUIDs::

		{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}

	The hash is derived from ``sha256(key.encode())`` so the same key always
	produces the same GUID across runs and machines.
	"""
	h = hashlib.sha256(key.encode("utf-8")).hexdigest()
	# VS GUIDs use the pattern: 8-4-4-4-12 hex chars with specific variant bits
	# We construct a valid-looking GUID from the hash
	return (
		"{"
		+ h[0:8]
		+ "-"
		+ h[8:12]
		+ "-"
		+ h[12:16]
		+ "-"
		+ h[16:20]
		+ "-"
		+ h[20:32]
		+ "}"
	)


def GenerateSolutionGUID(solution_name: str) -> str:
	"""Return a stable GUID for the solution itself."""
	return _content_guid(f"solution:{solution_name}")


def GenerateProjectTypeGUID() -> str:
	"""Return the VS type GUID for C++ projects.

	This is the standard Visual Studio constant, not content-based.
	"""
	return "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}"


def GenerateSolutionFolderGUID() -> str:
	"""Return the VS type GUID for solution folders (groups).

	This is the standard Visual Studio constant, not content-based.
	"""
	return "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"


def GenerateProjectGUID(project_name: str) -> str:
	"""Return a stable GUID for a project entry in the solution."""
	return _content_guid(f"project:{project_name}")


# ---------------------------------------------------------------------------
# Solution file (.sln) generation
# ---------------------------------------------------------------------------

class _SolutionEntry:
	"""Internal representation of one project or group in the solution."""

	def __init__(self, name: str, is_group: bool, path: str = "",
				 parent_group: str | None = None):
		self.name: str = name
		self.is_group: bool = is_group
		self.path: str = path  # relative vcxproj path (empty for groups)
		self.parent_group: str | None = parent_group
		self.guid: str = ""


def GenerateSolutionFile(solution_name: str,
						 projects: list[dict],
						 configurations: list[str],
						 architecture: str = "x64",
						 output_path: str | None = None) -> str:
	"""Generate a Visual Studio solution file.

	Args:
		solution_name: Name for the solution (used as .sln stem).
		projects: List of project dicts with keys:

			- ``name`` (str): Display/project name.
			- ``vcxproj_path`` (str): Relative path to the .vcxproj file
			  from the solution directory (e.g.
			  ``"..\\Engine\\Engine.vcxproj"``).
			- ``group`` (str | None): Dot-separated group path, e.g.
			  ``"Frost.Core"``.  Projects with the same group are nested
			  under a common solution folder.  ``None`` or ``""`` means no
			  grouping.
		configurations: Configuration names, e.g.
		  ``["Debug", "Development", "Dist"]``.
		architecture: Platform string, typically ``"x64"``.
		output_path: If given, write the .sln to this absolute path.  When
		  ``None`` the caller must use the returned string.

	Returns:
		The complete .sln file content as a string.
	"""
	lines: list[str] = []
	lines.append("")
	lines.append("Microsoft Visual Studio Solution File, Format Version 12.00")
	lines.append("# Visual Studio Version 16")

	# ------------------------------------------------------------------
	# Collect groups and entries
	# ------------------------------------------------------------------
	proj_type_guid = GenerateProjectTypeGUID()
	folder_type_guid = GenerateSolutionFolderGUID()

	# Build the set of group paths (full dot-separated, e.g. "Frost.Core")
	group_paths: list[str] = []  # ordered, de-duplicated
	group_guid_map: dict[str, str] = {}

	entries: list[_SolutionEntry] = []

	for proj in projects:
		name = proj["name"]
		group = proj.get("group") or ""
		vcxproj_path = proj["vcxproj_path"]

		# Register all ancestor groups
		if group:
			parts = group.split(".")
			for i in range(1, len(parts) + 1):
				partial = ".".join(parts[:i])
				if partial not in group_guid_map:
					group_paths.append(partial)
					group_guid_map[partial] = _content_guid(f"group:{partial}")

		# Project entry
		entry = _SolutionEntry(
			name=name,
			is_group=False,
			path=vcxproj_path,
			parent_group=group if group else None,
		)
		entry.guid = GenerateProjectGUID(name)
		entries.append(entry)

	# Group folder entries (sorted for determinism)
	group_entries: list[_SolutionEntry] = []
	for gp in sorted(group_paths):
		ge = _SolutionEntry(
			name=gp.split(".")[-1],  # display name is the last segment
			is_group=True,
			parent_group=".".join(gp.split(".")[:-1]) if "." in gp else None,
		)
		ge.guid = group_guid_map[gp]
		group_entries.append(ge)

	# ------------------------------------------------------------------
	# Write Project() blocks: groups first, then projects
	# ------------------------------------------------------------------
	for ge in group_entries:
		lines.append(
			f'Project("{folder_type_guid}") = "{ge.name}", '
			f'"{ge.name}", "{ge.guid}"'
		)
		lines.append("EndProject")

	for entry in entries:
		lines.append(
			f'Project("{proj_type_guid}") = "{entry.name}", '
			f'"{entry.path}", "{entry.guid}"'
		)
		lines.append("EndProject")

	# ------------------------------------------------------------------
	# Global section
	# ------------------------------------------------------------------
	lines.append("Global")

	# SolutionConfigurationPlatforms
	lines.append("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution")
	for cfg in configurations:
		lines.append(f"\t\t{cfg}|{architecture} = {cfg}|{architecture}")
	lines.append("\tEndGlobalSection")

	# ProjectConfigurationPlatforms
	lines.append("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution")

	for ge in group_entries:
		for cfg in configurations:
			lines.append(
				f"\t\t{ge.guid}.{cfg}|{architecture}.ActiveCfg = "
				f"{cfg}|{architecture}"
			)
	for entry in entries:
		for cfg in configurations:
			lines.append(
				f"\t\t{entry.guid}.{cfg}|{architecture}.ActiveCfg = "
				f"{cfg}|{architecture}"
			)
			lines.append(
				f"\t\t{entry.guid}.{cfg}|{architecture}.Build.0 = "
				f"{cfg}|{architecture}"
			)
	lines.append("\tEndGlobalSection")

	# SolutionProperties
	lines.append("\tGlobalSection(SolutionProperties) = preSolution")
	lines.append("\t\tHideSolutionNode = FALSE")
	lines.append("\tEndGlobalSection")

	# NestedProjects (group hierarchy + project-to-group nesting)
	lines.append("\tGlobalSection(NestedProjects) = preSolution")
	# Group parent-child nesting
	for ge in group_entries:
		if ge.parent_group and ge.parent_group in group_guid_map:
			parent_guid = group_guid_map[ge.parent_group]
			lines.append(f"\t\t{ge.guid} = {parent_guid}")
	# Project-to-group nesting
	for entry in entries:
		if entry.parent_group and entry.parent_group in group_guid_map:
			parent_guid = group_guid_map[entry.parent_group]
			lines.append(f"\t\t{entry.guid} = {parent_guid}")
	lines.append("\tEndGlobalSection")

	lines.append("EndGlobal")

	content = "\r\n".join(lines) + "\r\n"

	if output_path:
		os.makedirs(os.path.dirname(output_path), exist_ok=True)
		# Write with UTF-8 BOM as VS expects for .sln files
		with open(output_path, "w", encoding="utf-8-sig", newline="") as f:
			f.write(content)

	return content


# ---------------------------------------------------------------------------
# NMake-style .vcxproj generation
# ---------------------------------------------------------------------------

def _vs_condition(cfg: str, arch: str) -> str:
	"""Return a valid MSBuild Condition attribute string.

	Example output::

		'$(Configuration)|$(Platform)'=='Debug|x64'
	"""
	return f"'$(Configuration)|$(Platform)'=='{cfg}|{arch}'"


def GenerateVcxprojFile(
	project_name: str,
	configurations: list[str],
	architecture: str = "x64",
	build_tool_path: str = "",
	project_dir_arg: str = "",
	is_root_project: bool = False,
	defines: list[str] | None = None,
	include_paths: list[str] | None = None,
	cpp_version: str = "c++20",
	source_files: list[str] | None = None,
	header_files: list[str] | None = None,
	output_path: str | None = None,
) -> str:
	"""Generate an NMake-style Visual Studio project file (.vcxproj).

	The generated file delegates Build / Rebuild / Clean to the Python build
	system and provides IntelliSense-level metadata (defines, includes,
	source/header lists).

	Args:
		project_name: Project name (used for GUID and output naming).
		configurations: Configuration names.
		architecture: Platform string (typically ``"x64"``).
		build_tool_path: Path to the build script/bat used in NMake commands.
			For root projects this is typically ``"Build.bat"``; for
			dependencies it includes the full python + Build.py path.
		project_dir_arg: The argument passed as ``-p`` to the build command.
			For root projects this is the absolute project path; for
			dependencies it can be the short project name.
		is_root_project: Whether this is the root/target project (affects
			whether Build.bat or full python path is used).
		defines: Preprocessor definitions for IntelliSense patching.
		include_paths: Include search paths for IntelliSense.
		cpp_version: C++ standard version string (e.g. ``"c++20"``).
		source_files: List of relative .cpp/.c file paths for the project.
		header_files: List of relative .h/.hpp file paths.
		output_path: If given, write the file here.

	Returns:
		The complete .vcxproj content as a string.
	"""
	ns = "http://schemas.microsoft.com/developer/msbuild/2003"
	project_guid = GenerateProjectGUID(project_name)

	lines: list[str] = []
	lines.append(
		f'<Project xmlns="{ns}" DefaultTargets="Build">'
	)

	# ---- ProjectConfigurations ----
	lines.append('  <ItemGroup Label="ProjectConfigurations">')
	for cfg in configurations:
		lines.append(f'    <ProjectConfiguration Include="{cfg}|{architecture}">')
		lines.append(f"      <Configuration>{cfg}</Configuration>")
		lines.append(f"      <Platform>{architecture}</Platform>")
		lines.append(f"    </ProjectConfiguration>")
	lines.append("  </ItemGroup>")

	# ---- Globals ----
	lines.append('  <PropertyGroup Label="Globals">')
	lines.append(f"    <ProjectGuid>{project_guid}</ProjectGuid>")
	lines.append(
		"    <IgnoreWarnCompileDuplicatedFilename>true"
		"</IgnoreWarnCompileDuplicatedFilename>"
	)
	lines.append("    <Keyword>MakeFileProj</Keyword>")
	lines.append("  </PropertyGroup>")

	# ---- Default props import ----
	lines.append(
		'  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />'
	)

	# ---- Per-configuration PropertyGroups (Configuration type) ----
	for cfg in configurations:
		cond = _vs_condition(cfg, architecture)
		lines.append(
			f'  <PropertyGroup Condition="{cond}" Label="Configuration">'
		)
		lines.append("    <ConfigurationType>Makefile</ConfigurationType>")
		lines.append("    <UseDebugLibraries>false</UseDebugLibraries>")
		lines.append("    <PlatformToolset>v143</PlatformToolset>")
		lines.append("    <OutDir>bin-int\\vs\\</OutDir>")
		lines.append(f"    <IntDir>bin-int\\vs\\{cfg}\\</IntDir>")
		lines.append("  </PropertyGroup>")

	# ---- Props import ----
	lines.append(
		'  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />'
	)

	# ---- ExtensionSettings ----
	lines.append('  <ImportGroup Label="ExtensionSettings">')
	lines.append("  </ImportGroup>")

	# ---- PropertySheets per config ----
	for cfg in configurations:
		cond = _vs_condition(cfg, architecture)
		lines.append(
			f'  <ImportGroup Label="PropertySheets" Condition="{cond}">'
		)
		lines.append(
			'    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform)'
			'.user.props" '
			'Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform)'
			'.user.props\')" '
			'Label="LocalAppDataPlatform" />'
		)
		lines.append("  </ImportGroup>")

	# ---- UserMacros ----
	lines.append('  <PropertyGroup Label="UserMacros" />')

	# ---- Per-configuration NMake settings ----
	for cfg in configurations:
		cond = _vs_condition(cfg, architecture)
		lines.append(f'  <PropertyGroup Condition="{cond}">')
		lines.append(f"    <NMakeOutput>$(OutDir){project_name}</NMakeOutput>")

		build_cmd = (
			f'"{build_tool_path}" -fb -c {cfg} -a x86_64 -p {project_dir_arg}'
		)
		rebuild_cmd = (
			f'"{build_tool_path}" -rebuild -c {cfg} -a x86_64 '
			f'-p {project_dir_arg}'
		)
		clean_cmd = (
			f'"{build_tool_path}" -clean -c {cfg} -a x86_64 '
			f'-p {project_dir_arg}'
		)

		lines.append(
			f"    <NMakeBuildCommandLine>{build_cmd}</NMakeBuildCommandLine>"
		)
		lines.append(
			f"    <NMakeReBuildCommandLine>{rebuild_cmd}"
			f"</NMakeReBuildCommandLine>"
		)
		lines.append(
			f"    <NMakeCleanCommandLine>{clean_cmd}"
			f"</NMakeCleanCommandLine>"
		)

		# Preprocessor definitions (for IntelliSense)
		if defines:
			def_str = "STRIP_CLANG;" + ";".join(defines) + ";"
			lines.append(
				f"    <NMakePreprocessorDefinitions>{def_str}"
				f"</NMakePreprocessorDefinitions>"
			)

		# Include search paths (for IntelliSense)
		if include_paths:
			inc_str = ";".join(include_paths)
			lines.append(
				f"    <NMakeIncludeSearchPath>{inc_str}</NMakeIncludeSearchPath>"
			)

		# Additional options
		lines.append(f"    <AdditionalOptions>/std:{cpp_version}</AdditionalOptions>")
		lines.append("  </PropertyGroup>")

	# ---- ItemDefinitionGroup ----
	lines.append("  <ItemDefinitionGroup>")
	lines.append("  </ItemDefinitionGroup>")

	# ---- Header files (ClInclude) ----
	if header_files:
		lines.append("  <ItemGroup>")
		for hf in sorted(header_files):
			rel = _make_backslash(hf)
			lines.append(f'    <ClInclude Include="{rel}" />')
		lines.append("  </ItemGroup>")

	# ---- Source files (ClCompile) ----
	if source_files:
		lines.append("  <ItemGroup>")
		for sf in sorted(source_files):
			rel = _make_backslash(sf)
			lines.append(f'    <ClCompile Include="{rel}" />')
		lines.append("  </ItemGroup>")

	# ---- Targets import ----
	lines.append(
		'  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />'
	)
	lines.append('  <ImportGroup Label="ExtensionTargets">')
	lines.append("  </ImportGroup>")
	lines.append("</Project>")

	content = "\r\n".join(lines) + "\r\n"

	if output_path:
		os.makedirs(os.path.dirname(output_path), exist_ok=True)
		with open(output_path, "w", encoding="utf-8", newline="") as f:
			f.write(content)

	return content


# ---------------------------------------------------------------------------
# .vcxproj.filters generation
# ---------------------------------------------------------------------------

def GenerateVcxprojFiltersFile(
	header_files: list[str] | None = None,
	source_files: list[str] | None = None,
	output_path: str | None = None,
) -> str:
	"""Generate a Visual Studio filters file (.vcxproj.filters).

	Filters are derived from directory structure relative to ``src/``.
	Each unique directory path becomes a Filter with its own stable GUID.

	Args:
		header_files: Relative header file paths.
		source_files: Relative source file paths.
		output_path: If given, write the file here.

	Returns:
		The complete .vcxproj.filters content as a string.
	"""
	lines: list[str] = []
	lines.append('<?xml version="1.0" encoding="utf-8"?>')
	lines.append(
		'<Project ToolsVersion="4.0" '
		'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
	)

	# Collect all filter paths from file directories
	all_files: list[str] = []
	if header_files:
		all_files.extend(header_files)
	if source_files:
		all_files.extend(source_files)

	# Determine filter paths (directory components relative to src/)
	filter_paths: list[str] = []
	for fpath in all_files:
		rel = _strip_src(fpath)
		parts = Path(rel).parts
		for i in range(1, len(parts) + 1):
			partial = "\\".join(parts[:i])
			if partial not in filter_paths:
				filter_paths.append(partial)

	# First ItemGroup: Filter definitions
	lines.append("  <ItemGroup>")
	for fp in sorted(filter_paths):
		guid = _content_guid(f"filter:{fp}")
		lines.append(f'    <Filter Include="{fp}">')
		lines.append(f"      <UniqueIdentifier>{guid}</UniqueIdentifier>")
		lines.append("    </Filter>")
	lines.append("  </ItemGroup>")

	# Second ItemGroup: ClInclude entries
	if header_files:
		lines.append("  <ItemGroup>")
		for hf in sorted(header_files):
			rel = _make_backslash(hf)
			filter_path = _get_filter_for_file(rel)
			lines.append(f'    <ClInclude Include="{rel}">')
			if filter_path:
				lines.append(f"      <Filter>{filter_path}</Filter>")
			lines.append("    </ClInclude>")
		lines.append("  </ItemGroup>")

	# Third ItemGroup: ClCompile entries
	if source_files:
		lines.append("  <ItemGroup>")
		for sf in sorted(source_files):
			rel = _make_backslash(sf)
			filter_path = _get_filter_for_file(rel)
			lines.append(f'    <ClCompile Include="{rel}">')
			if filter_path:
				lines.append(f"      <Filter>{filter_path}</Filter>")
			lines.append("    </ClCompile>")
		lines.append("  </ItemGroup>")

	lines.append("</Project>")

	content = "\r\n".join(lines) + "\r\n"

	if output_path:
		os.makedirs(os.path.dirname(output_path), exist_ok=True)
		with open(output_path, "w", encoding="utf-8", newline="") as f:
			f.write(content)

	return content


# ---------------------------------------------------------------------------
# .vcxproj.user generation
# ---------------------------------------------------------------------------

def GenerateVcxprojUserFile(
	project_name: str,
	configurations: list[str],
	architecture: str = "x64",
	executable_paths: dict[str, str] | None = None,
	output_path: str | None = None,
) -> str:
	"""Generate a Visual Studio per-user project file (.vcxproj.user).

	Sets debugger command to the built executable (for EXE projects) and
	enables ShowAllFiles.

	Args:
		project_name: Project name.
		configurations: Configuration names.
		architecture: Platform string.
		executable_paths: Mapping from configuration name to absolute path of
			the built executable for that configuration.  If ``None`` or if a
			given configuration is absent, no debugger command is set for that
			configuration.
		output_path: If given, write the file here.

	Returns:
		The complete .vcxproj.user content as a string.
	"""
	lines: list[str] = []
	lines.append('<?xml version="1.0" encoding="utf-8"?>')
	lines.append(
		'<Project ToolsVersion="Current" '
		'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">'
	)

	for cfg in configurations:
		cond = _vs_condition(cfg, architecture)
		lines.append(f'  <PropertyGroup Condition="{cond}">')
		exe_path = executable_paths.get(cfg) if executable_paths else None
		if exe_path:
			lines.append(
				f"    <LocalDebuggerCommand>{exe_path}"
				f"</LocalDebuggerCommand>"
			)
		lines.append("    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>")
		lines.append(
			"    <LocalDebuggerCommandArguments>"
			"</LocalDebuggerCommandArguments>"
		)
		lines.append("  </PropertyGroup>")

	# ShowAllFiles property group
	lines.append("  <PropertyGroup>")
	lines.append("    <ShowAllFiles>true</ShowAllFiles>")
	lines.append("  </PropertyGroup>")

	lines.append("</Project>")

	content = "\r\n".join(lines) + "\r\n"

	if output_path:
		os.makedirs(os.path.dirname(output_path), exist_ok=True)
		with open(output_path, "w", encoding="utf-8", newline="") as f:
			f.write(content)

	return content


# ---------------------------------------------------------------------------
# Main entry point — Generate all VS files for loaded projects
# ---------------------------------------------------------------------------

def GenerateAllVSFiles(
	solution_name: str,
	solution_dir: str,
	projects_info: list[dict],
	configurations: list[str],
	architecture: str = "x64",
) -> None:
	"""Generate a complete set of Visual Studio files for all projects.

	This is the primary entry point called by ``Build.py``

	Args:
		solution_name: Name for the .sln file (without extension).
		solution_dir: Directory where the .sln should be placed.  Individual
			project files (.vcxproj, etc.) are written alongside each project's
			existing directory.
		projects_info: List of project info dicts with keys:

			- ``name`` (str): Project name.
			- ``folder`` (str): Absolute path to the project folder.
			- ``group`` (str | None): Dot-separated group for solution nesting.
			- ``vcxproj_rel_path`` (str): Path to .vcxproj relative to the
			  solution directory (for the .sln entry).
			- ``build_tool_path`` (str): Build command prefix.
			- ``project_dir_arg`` (str): The ``-p`` argument value.
			- ``is_root_project`` (bool): Whether this is the root project.
			- ``defines`` (list[str]): Preprocessor definitions.
			- ``include_paths`` (list[str]): Include search paths.
			- ``source_files`` (list[str]): Relative source file paths.
			- ``header_files`` (list[str]): Relative header file paths.
			- ``executable_paths`` (dict[str, str] | None): Mapping from configuration name to path of built executable
			  (for .vcxproj.user debugger).
		configurations: Configuration names.
		architecture: Platform string.
	"""
	# Generate solution file
	sln_path = os.path.join(solution_dir, f"{solution_name}.sln").replace("\\", "/")

	# Build project list for solution
	sln_projects: list[dict] = []
	for pi in projects_info:
		sln_projects.append({
			"name": pi["name"],
			"vcxproj_path": pi["vcxproj_rel_path"],
			"group": pi.get("group"),
		})

	GenerateSolutionFile(
		solution_name=solution_name,
		projects=sln_projects,
		configurations=configurations,
		architecture=architecture,
		output_path=sln_path,
	)
	print(f"Generated solution: {sln_path}")

	# Generate per-project files
	for pi in projects_info:
		proj_folder = pi["folder"]
		proj_name = pi["name"]

		# .vcxproj
		vcxproj_path = os.path.join(
			proj_folder, f"{proj_name}.vcxproj"
		).replace("\\", "/")
		GenerateVcxprojFile(
			project_name=proj_name,
			configurations=configurations,
			architecture=architecture,
			build_tool_path=pi.get("build_tool_path", ""),
			project_dir_arg=pi.get("project_dir_arg", ""),
			is_root_project=pi.get("is_root_project", False),
			defines=pi.get("defines"),
			include_paths=pi.get("include_paths"),
			cpp_version=pi.get("cpp_version", "c++20"),
			source_files=pi.get("source_files"),
			header_files=pi.get("header_files"),
			output_path=vcxproj_path,
		)

		# .vcxproj.filters
		filters_path = os.path.join(
			proj_folder, f"{proj_name}.vcxproj.filters"
		).replace("\\", "/")
		GenerateVcxprojFiltersFile(
			header_files=pi.get("header_files"),
			source_files=pi.get("source_files"),
			output_path=filters_path,
		)

		# .vcxproj.user
		user_path = os.path.join(
			proj_folder, f"{proj_name}.vcxproj.user"
		).replace("\\", "/")
		GenerateVcxprojUserFile(
			project_name=proj_name,
			configurations=configurations,
			architecture=architecture,
			executable_paths=pi.get("executable_paths"),
			output_path=user_path,
		)

		print(f"Generated project files for: {proj_name}")


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _make_backslash(path_str: str) -> str:
	"""Convert forward slashes to backslashes in a path string."""
	return path_str.replace("/", "\\")


def _strip_src(path_str: str) -> str:
	"""Strip the ``src/`` or ``src\\`` prefix from a relative path.

	Returns the remainder (e.g., ``"Core/Logging/Log.h"``).
	"""
	rel = path_str.replace("\\", "/")
	if rel.startswith("src/"):
		return rel[4:]
	elif rel.startswith("src\\"):
		return rel[4:]
	return rel


def _get_filter_for_file(rel_path: str) -> str:
	"""Extract the filter (directory portion) for a file relative to src/.

	Given ``"src/Engine/Renderer/Renderer.h"`` returns ``"Engine\\Renderer"``.
	Given ``"src/Core.h"`` returns ``""`` (no filter).
	"""
	rel = _strip_src(rel_path)
	parts = Path(rel).parts
	if len(parts) <= 1:
		return ""
	# Return all directory parts except the filename, joined with backslashes
	dir_parts = parts[:-1]
	return "\\".join(dir_parts)
