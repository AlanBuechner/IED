"""
BuildInfo — Tracks per-file and per-project build metrics for analysing optimisation opportunities.

Records timing for each step of the compilation pipeline:
  - Compilation (clang -cc1)
  - Reflection collection (HeaderTool)
  - Reflection generation (CodeGen tool + compiling generated/generated.cpp)
  - Linking

Also records whether each step was actually needed (i.e. the file/project was up-to-date).
"""

import os
import json
import time
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class FileBuildInfo:
	"""Metrics for a single source file compilation."""
	# Source path relative to the project folder
	sourcePath: str = ""

	# Compilation step
	compileNeeded: bool = False
	compileTime: float = 0.0          # seconds
	compileStartTime: float = 0.0     # time.time() wall-clock start
	compileEndTime: float = 0.0       # time.time() wall-clock end

	# Reflection collection (HeaderTool) step
	reflectionCollectNeeded: bool = False
	reflectionCollectTime: float = 0.0  # seconds
	reflectionCollectStartTime: float = 0.0  # time.time() wall-clock start
	reflectionCollectEndTime: float = 0.0    # time.time() wall-clock end

	# Build result
	buildSucceeded: bool = True


@dataclass
class ProjectBuildInfo:
	"""Metrics for a single project build."""
	projectName: str = ""
	configuration: str = ""
	architecture: str = ""
	timestamp: str = ""               # ISO-8601

	# Overall timing
	totalBuildTime: float = 0.0       # seconds — wall clock for the whole project
	buildStartTime: float = 0.0       # time.time() wall-clock start of project build
	buildEndTime: float = 0.0         # time.time() wall-clock end of project build

	 # Compilation timing (sum of all file compile times)
	compileTime: float = 0.0          # sum of all file compile times

	# Reflection generation timing (CodeGen tool)
	reflectionGenTime: float = 0.0      # seconds — CodeGen tool invocation time
	reflectionGenStartTime: float = 0.0   # time.time() wall-clock start
	reflectionGenEndTime: float = 0.0     # time.time() wall-clock end

	# Reflection compilation timing (compiling generated/generated.cpp)
	reflectionCompileTime: float = 0.0   # seconds — compiling generated/generated.cpp
	reflectionCompileStartTime: float = 0.0  # time.time() wall-clock start
	reflectionCompileEndTime: float = 0.0    # time.time() wall-clock end

	# Linking timing
	linkTime: float = 0.0             # seconds — linker invocation time
	linkStartTime: float = 0.0        # time.time() wall-clock start
	linkEndTime: float = 0.0          # time.time() wall-clock end

	# Custom code generation timing
	customCodeGenTime: float = 0.0    # seconds — aggregate custom code gen execution time

	# Flags — was the step actually performed?
	compileNeeded: bool = False
	reflectionGenNeeded: bool = False
	linkNeeded: bool = False
	customCodeGenNeeded: bool = False

	# Counts
	numFilesCompiled: int = 0         # files that were compiled this run
	numFilesSkipped: int = 0          # files up-to-date, skipped
	numFilesFailed: int = 0           # files whose compilation returned non-zero

	# Per-file breakdown
	files: list[FileBuildInfo] = field(default_factory=list)

	def to_dict(self) -> dict:
		"""Serialise to a JSON-friendly dict."""
		return {
			"projectName": self.projectName,
			"configuration": self.configuration,
			"architecture": self.architecture,
			"timestamp": self.timestamp,
			"totalBuildTime": round(self.totalBuildTime, 4),
			"buildStartTime": round(self.buildStartTime, 4),
			"buildEndTime": round(self.buildEndTime, 4),
			"compileTime": round(self.compileTime, 4),
			"reflectionGenTime": round(self.reflectionGenTime, 4),
			"reflectionGenStartTime": round(self.reflectionGenStartTime, 4),
			"reflectionGenEndTime": round(self.reflectionGenEndTime, 4),
			"reflectionCompileTime": round(self.reflectionCompileTime, 4),
			"reflectionCompileStartTime": round(self.reflectionCompileStartTime, 4),
			"reflectionCompileEndTime": round(self.reflectionCompileEndTime, 4),
			"linkTime": round(self.linkTime, 4),
			"linkStartTime": round(self.linkStartTime, 4),
			"linkEndTime": round(self.linkEndTime, 4),
			"compileNeeded": self.compileNeeded,
			"reflectionGenNeeded": self.reflectionGenNeeded,
			"linkNeeded": self.linkNeeded,
			"customCodeGenTime": round(self.customCodeGenTime, 4),
			"customCodeGenNeeded": self.customCodeGenNeeded,
			"numFilesCompiled": self.numFilesCompiled,
			"numFilesSkipped": self.numFilesSkipped,
			"numFilesFailed": self.numFilesFailed,
			"files": [f.__dict__ for f in self.files],
		}


# ---------------------------------------------------------------------------
# Global build session — accumulates all project infos across a single Build.py run
# ---------------------------------------------------------------------------

_build_session: dict[str, ProjectBuildInfo] = {}


def GetBuildSession() -> dict[str, ProjectBuildInfo]:
	"""Return the current build session dictionary (projectName -> ProjectBuildInfo)."""
	return _build_session


def ClearBuildSession():
	"""Reset the global build session state."""
	_build_session.clear()


def CreateProjectInfo(project_name: str, configuration: str, architecture: str) -> ProjectBuildInfo:
	"""Create and register a new ProjectBuildInfo for *project_name*."""
	info = ProjectBuildInfo()
	info.projectName = project_name
	info.configuration = configuration
	info.architecture = architecture
	info.timestamp = time.strftime("%Y-%m-%dT%H:%M:%S%z")
	_build_session[project_name] = info
	return info


def GetProjectInfo(project_name: str) -> Optional[ProjectBuildInfo]:
	"""Retrieve the ProjectBuildInfo for *project_name*, or None."""
	return _build_session.get(project_name, None)


def RecordFile(file_info: FileBuildInfo):
	"""Attach a completed FileBuildInfo to the most recently active project.

	The caller (ObjectBuilder) sets the sourcePath and timing fields before calling.
	This is called from ProjectBuilder via the build actions list after BuildObjects returns.
	"""
	# Files are recorded through the project info directly; see RegisterFile below.
	pass


def RegisterFile(project_name: str, file_info: FileBuildInfo):
	"""Register a single file's metrics under *project_name*."""
	info = _build_session.get(project_name)
	if info is None:
		return
	info.files.append(file_info)
	info.compileTime += file_info.compileTime
	if file_info.compileNeeded:
		info.compileNeeded = True
		info.numFilesCompiled += 1
	else:
		info.numFilesSkipped += 1
	if not file_info.buildSucceeded:
		info.numFilesFailed += 1


def SaveBuildInfo(output_path: str):
	"""Write the current build session to a JSON file at *output_path*.

	Creates parent directories if needed.  Previous files are overwritten.
	"""
	os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)

	data = {
		"buildTime": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
		"projects": {name: info.to_dict() for name, info in _build_session.items()},
	}

	with open(output_path, "w", encoding="utf-8") as f:
		json.dump(data, f, indent=2)


def LoadBuildInfo(input_path: str) -> Optional[dict]:
	"""Load a previous build-info JSON file.  Returns None on any error."""
	if not os.path.isfile(input_path):
		return None
	try:
		with open(input_path, "r", encoding="utf-8") as f:
			return json.load(f)
	except (json.JSONDecodeError, OSError):
		return None


# ---------------------------------------------------------------------------
# Chrome Tracing JSON export
# ---------------------------------------------------------------------------

def SaveChromeTrace(output_path: str):
	"""Write the current build session to a Chrome tracing JSON file.

	The output can be loaded in chrome://tracing or ui.perfetto.dev for
	interactive timeline visualisation of the build pipeline.

	Each project contributes a set of named events (compile, reflection gen,
	link) and each compiled source file is emitted as an individual task on
	its thread slice.
	"""
	# Chrome tracing uses microseconds since an arbitrary epoch.  We use the
	# earliest buildStartTime across all projects as T=0 so that all timestamps
	# are non-negative.
	all_starts: list[float] = []
	for info in _build_session.values():
		if info.buildStartTime > 0:
			all_starts.append(info.buildStartTime)

	epoch = min(all_starts) if all_starts else 0.0

	def ts(wall_time: float) -> int:
		"""Convert a time.time() wall-clock value to microseconds since *epoch*."""
		return int((wall_time - epoch) * 1_000_000)

	def dur(start_wall: float, end_wall: float) -> int:
		"""Duration in microseconds from two wall-clock values."""
		return int((end_wall - start_wall) * 1_000_000)

	trace_events: list[dict] = []

	# --- Metadata ---
	trace_events.append({
		"name": "trace_event_enabled",
		"ph": "M",
		"ts": ts(epoch),
		"args": {"state": 1},
	})

	for project_name, info in _build_session.items():
		proj = f"Build:{project_name}"

		# Overall project build span (complete task)
		if info.buildStartTime > 0 and info.buildEndTime > 0:
			trace_events.append({
				"name": "Project Build",
				"ph": "X",
				"ts": ts(info.buildStartTime),
				"dur": dur(info.buildStartTime, info.buildEndTime),
				"cat": "build",
				"pid": proj,
				"tid": proj,
				"args": {
					"configuration": info.configuration,
					"architecture": info.architecture,
					"filesCompiled": info.numFilesCompiled,
					"filesSkipped": info.numFilesSkipped,
					"filesFailed": info.numFilesFailed,
				},
			})

		# Per-file compilation tasks — each file on its own "thread" so they
		# can overlap visually when built in parallel.
		for f_info in info.files:
			if not f_info.compileNeeded or f_info.compileStartTime == 0:
				continue

			tid = f_info.sourcePath
			trace_events.append({
				"name": "Compile",
				"ph": "X",
				"ts": ts(f_info.compileStartTime),
				"dur": dur(f_info.compileStartTime, f_info.compileEndTime),
				"cat": "compile",
				"pid": proj,
				"tid": tid,
				"args": {
					"file": f_info.sourcePath,
					"time": round(f_info.compileTime, 4),
				},
			})

			# Reflection collection sub-task on same thread
			if f_info.reflectionCollectNeeded and f_info.reflectionCollectStartTime > 0:
				trace_events.append({
					"name": "Reflection Collect",
					"ph": "X",
					"ts": ts(f_info.reflectionCollectStartTime),
					"dur": dur(f_info.reflectionCollectStartTime, f_info.reflectionCollectEndTime),
					"cat": "reflection",
					"pid": proj,
					"tid": tid,
					"args": {
						"file": f_info.sourcePath,
						"time": round(f_info.reflectionCollectTime, 4),
					},
				})

		# Reflection generation (CodeGen tool) — project-level task
		if info.reflectionGenNeeded and info.reflectionGenStartTime > 0:
			trace_events.append({
				"name": "Reflection Generate",
				"ph": "X",
				"ts": ts(info.reflectionGenStartTime),
				"dur": dur(info.reflectionGenStartTime, info.reflectionGenEndTime),
				"cat": "reflection",
				"pid": proj,
				"tid": proj,
				"args": {
					"time": round(info.reflectionGenTime, 4),
				},
			})

		# Reflection compilation (generated/generated.cpp) — project-level task
		if info.reflectionCompileStartTime > 0:
			trace_events.append({
				"name": "Reflection Compile",
				"ph": "X",
				"ts": ts(info.reflectionCompileStartTime),
				"dur": dur(info.reflectionCompileStartTime, info.reflectionCompileEndTime),
				"cat": "reflection",
				"pid": proj,
				"tid": proj,
				"args": {
					"time": round(info.reflectionCompileTime, 4),
				},
			})

		# Linking — project-level task
		if info.linkNeeded and info.linkStartTime > 0:
			trace_events.append({
				"name": "Link",
				"ph": "X",
				"ts": ts(info.linkStartTime),
				"dur": dur(info.linkStartTime, info.linkEndTime),
				"cat": "link",
				"pid": proj,
				"tid": proj,
				"args": {
					"time": round(info.linkTime, 4),
				},
			})

	output = {
		"displayTimeUnit": "ns",
		"traceEvents": trace_events,
	}

	os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else ".", exist_ok=True)

	with open(output_path, "w", encoding="utf-8") as f:
		json.dump(output, f, indent=2)
