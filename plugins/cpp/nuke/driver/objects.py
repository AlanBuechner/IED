"""Compiling sources to objects with clang's -cc1 frontend, in parallel, each only when it has to be: no object or
dependency file, other arguments than last time, a newer clang, or a file it read changed or went. A reflecting
project's sources also go through the HeaderTool after they compile (and again when the HeaderTool is newer).

An object is stamped with the time its compile began (to the nanosecond), so a file edited while the compile ran is newer
than it and the next build compiles it again (BUG-346, found in Frost)."""

import concurrent.futures
import os
import subprocess
import time
from dataclasses import dataclass, field

from . import depfile

# clang 22 and the HeaderTool (libclang) crash now and then under a parallel build on Windows, a different file each
# time and never on a rerun: a crash is retried, an error exit never is.
CRASH_EXIT_CODES = {0xC0000005: "access violation", 0xC00000FD: "stack overflow", 0xC0000409: "stack buffer overrun"}
CRASH_RETRIES = 2
ERROR_LIMIT = "10"


@dataclass
class Job:
	source: str
	obj: str
	depfile: str
	argsfile: str
	command: list
	header_tool: list = None     # the HeaderTool's command, for a reflecting project
	compile_needed: bool = False
	reflect_needed: bool = False
	code: int = 0
	log: list = field(default_factory=list)


class Stats:
	"""File modification times (nanoseconds), each read once per build."""

	def __init__(self):
		self._times = {}

	def mtime(self, path):
		if path not in self._times:
			try:
				self._times[path] = os.stat(path).st_mtime_ns
			except OSError:
				self._times[path] = None
		return self._times[path]


def object_name(source):
	return os.path.splitext(os.path.basename(source))[0]


def shared_object_names(sources):
	"""The clashes of sources whose objects would share a name (an object is named after its source's base name)."""
	seen, clashes = {}, []
	for source in sources:
		name = object_name(source).lower()
		if name in seen and seen[name] != source:
			clashes.append(f"{seen[name]} and {source} both compile to {object_name(source)}.obj")
		seen.setdefault(name, source)
	return clashes


def language_args(toolchain, source):
	if source.lower().endswith(".c"):
		return ["-std=" + toolchain.settings.c_standard, "-x", "c"]
	return ["-std=" + toolchain.settings.standard, "-x", "c++"]


def base_args(toolchain, source, includes, system_includes, defines, system_as_user=False):
	"""Language, include folders and defines: what the compile and the HeaderTool share."""
	args = language_args(toolchain, source)
	for folder in includes:
		args += ["-I", folder]
	for folder in system_includes:
		args += ["-I", folder] if system_as_user else ["-internal-isystem", folder]
	args += [f"-D{define}" for define in defines]
	return args


def compile_args(toolchain, configuration, source, includes, system_includes, defines, compiler_args):
	"""clang -cc1's arguments for one source, before its input and outputs: the MSVC-compatible ABI, C++ exceptions,
	CodeView debug information when the configuration keeps it, at most 10 errors in MSVC's format."""
	args = base_args(toolchain, source, includes, system_includes, defines)
	args += [f"-O{configuration.optimize}", "-triple", toolchain.target]
	args += list(compiler_args)
	if configuration.debug_info:
		args += ["-debug-info-kind=standalone", "-gcodeview"]
	args += ["-ferror-limit", ERROR_LIMIT, "-fdiagnostics-format", "msvc", "-fms-volatile", "-fms-extensions", "-fms-compatibility",
		f"-fms-compatibility-version={toolchain.ms_version}", "-fcxx-exceptions", "-fexceptions", "-fexternc-nounwind",
		"-Wno-unused-comparison"]
	return args


def make_job(toolchain, configuration, target, source, includes, system_includes, header_tool):
	name = os.path.join(target.int_dir, object_name(source)).replace("\\", "/")
	obj, dep = name + ".obj", name + ".d"
	command = [toolchain.clang, "-cc1", "-emit-obj"]
	command += compile_args(toolchain, configuration, source, includes, system_includes, target.defines, target.compiler_args)
	command += [source, "-o", obj, "-dependency-file", dep, "-MT", dep]
	job = Job(source, obj, dep, name + ".args", command)
	if header_tool:
		job.header_tool = [header_tool, "--generated-folder", f"{target.int_dir}/generated", "--source-folder", f"{target.dir}/src", source,
			"--", toolchain.clang] + toolchain.sysroot_args() + base_args(toolchain, source, includes, system_includes, target.defines, system_as_user=True) + ["-DHEADER_TOOL"]
	return job


def read_text(path):
	try:
		with open(path, encoding="utf-8", errors="replace") as f:
			return f.read()
	except OSError:
		return None


def command_text(command):
	return " ".join(command)


def decide(job, stats, clang, header_tool):
	"""Whether `job` must compile, and whether its sources must go through the HeaderTool again."""
	built = stats.mtime(job.obj)
	if built is None or stats.mtime(job.depfile) is None or read_text(job.argsfile) != command_text(job.command):
		job.compile_needed = True
	elif (stats.mtime(clang) or 0) > built or not dependencies_older(job.depfile, built, stats):
		job.compile_needed = True
	elif job.header_tool and (stats.mtime(header_tool) or 0) > built:
		job.reflect_needed = True
	job.reflect_needed = job.reflect_needed or (job.compile_needed and job.header_tool is not None)


def dependencies_older(path, built, stats):
	"""Every file the dependency file lists is still there and older than the object."""
	for dependency in depfile.parse(read_text(path) or ""):
		changed = stats.mtime(dependency)
		if changed is None or changed > built:
			return False
	return True


def run_retrying_crashes(command, cwd, log, label):
	for attempt in range(CRASH_RETRIES + 1):
		try:
			result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
				errors="replace", stdin=subprocess.DEVNULL)
		except OSError as error:
			log.append(f"cannot start {command[0]}: {error}")
			return 127
		if result.stdout:
			log.append(result.stdout.rstrip("\n"))
		crash = CRASH_EXIT_CODES.get(result.returncode & 0xFFFFFFFF)
		if crash is None or attempt == CRASH_RETRIES:
			return result.returncode
		log.append(f"warning: {os.path.basename(command[0])} crashed ({crash}) on {label}; retrying ({attempt + 1}/{CRASH_RETRIES})")
	return 1


def discard(job):
	"""A failed compile's object and dependency file go, so the next build compiles the file again instead of trusting
	what a crash left."""
	for path in (job.obj, job.depfile):
		if os.path.exists(path):
			os.remove(path)


def run_job(job, cwd):
	"""Compiles and reflects `job` as decided; its exit code (0: done)."""
	started = time.time_ns()
	if job.compile_needed:
		with open(job.argsfile, "w", encoding="utf-8") as f:
			f.write(command_text(job.command))
		job.code = run_retrying_crashes(job.command, cwd, job.log, os.path.basename(job.source))
		if job.code == 0 and not os.path.isfile(job.obj):
			job.log.append(f"error: clang reported success but wrote no {job.obj}")
			job.code = 1
		if job.code != 0:
			discard(job)
			return job.code
	if job.reflect_needed:
		os.makedirs(job.header_tool[2], exist_ok=True)
		job.code = run_retrying_crashes(job.header_tool, cwd, job.log, os.path.basename(job.source))
		if job.code != 0:
			job.log.append(f"error: the HeaderTool failed on {os.path.basename(job.source)} (exit code {job.code})")
			return job.code
	os.utime(job.obj, ns=(started, started))
	return 0


@dataclass
class Outcome:
	compiled: int = 0
	reflected: int = 0     # went through the HeaderTool again without compiling (a newer HeaderTool)
	failed: int = 0
	current: int = 0

	@property
	def worked(self):
		return self.compiled > 0 or self.reflected > 0 or self.failed > 0


def build_all(jobs, cwd, workers, report):
	"""Runs the jobs that need work, `workers` at a time; `report(job)` is called as each finishes."""
	outcome = Outcome()
	pending = [job for job in jobs if job.compile_needed or job.reflect_needed]
	outcome.current = len(jobs) - len(pending)
	if not pending:
		return outcome
	with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
		futures = {pool.submit(run_job, job, cwd): job for job in pending}
		for future in concurrent.futures.as_completed(futures):
			job = futures[future]
			try:
				future.result()
			except Exception as error:
				job.code = 1
				job.log.append(f"error: compiling {job.source} raised {type(error).__name__}: {error}")
			if job.code == 0 and job.compile_needed:
				outcome.compiled += 1
			elif job.code == 0:
				outcome.reflected += 1
			else:
				outcome.failed += 1
			report(job)
	return outcome
