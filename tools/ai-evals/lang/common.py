"""What the language eval's runner, grader and reports share: where things are, the tasks, copying folders,
running a program and measuring it."""

import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import tomllib
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TASKS = os.path.join(HERE, "tasks")
VARIANTS = os.path.join(HERE, "variants")
RESULTS = os.path.join(REPO, "build", "ai-evals", "lang")
WORK = os.path.join(tempfile.gettempdir(), "ied-langeval")
NUKE = os.path.join(REPO, "bin", "nuke.cmd")
DEV_TOOLCHAIN = os.path.join(REPO, "toolchains", "c4", "dev")
LANGUAGES = ("c4", "go")

# Rates of the owner's model server with one request at a time (prompt tokens and generated tokens a second): what a
# job's model requests would have taken alone, whatever else shared the server. Jobs are budgeted in these seconds.
SOLO_PREFILL_RATE = 900.0
SOLO_GENERATE_RATE = 30.0

sys.path.insert(0, os.path.join(REPO, "tools", "pylib"))


def solo_model_seconds(timings):
	"""One request's cost alone on the server, by the tokens it read (cache reuse left out) and generated."""
	return (timings.get("prompt_n") or 0) / SOLO_PREFILL_RATE + (timings.get("predicted_n") or 0) / SOLO_GENERATE_RATE


def load_task(task_id):
	folder = os.path.join(TASKS, task_id)
	with open(os.path.join(folder, "task.toml"), "rb") as f:
		task = tomllib.load(f)
	task["dir"] = folder
	task.setdefault("id", task_id)
	return task


def load_tasks(selection=None, task_set=None):
	"""The tasks named (comma-separated ids), else every task of `task_set` (tune, holdout), else all."""
	ids = sorted(name for name in os.listdir(TASKS) if os.path.exists(os.path.join(TASKS, name, "task.toml")))
	if selection:
		wanted = selection.split(",")
		unknown = [name for name in wanted if name not in ids]
		if unknown:
			raise SystemExit("no task %s (there are %s)" % (", ".join(unknown), ", ".join(ids)))
		ids = wanted
	tasks = [load_task(name) for name in ids]
	return [t for t in tasks if not task_set or t.get("set") == task_set]


def languages_of(task, wanted):
	return [lang for lang in wanted if lang in task]


def mirror(source, target, ignore=None):
	"""`target` made a copy of `source`: files copied when their size or time differ, files gone removed."""
	os.makedirs(target, exist_ok=True)
	seen = set()
	for root, folders, files in os.walk(source):
		relative = os.path.relpath(root, source)
		if ignore:
			folders[:] = [d for d in folders if not ignore(os.path.join(relative, d))]
		out = os.path.normpath(os.path.join(target, relative))
		os.makedirs(out, exist_ok=True)
		seen.add(os.path.normcase(out))
		for name in files:
			if ignore and ignore(os.path.join(relative, name)):
				continue
			src, dst = os.path.join(root, name), os.path.join(out, name)
			seen.add(os.path.normcase(dst))
			a = os.stat(src)
			b = os.stat(dst) if os.path.exists(dst) else None
			if b is None or a.st_size != b.st_size or int(a.st_mtime) != int(b.st_mtime):
				shutil.copy2(src, dst)
	for root, folders, files in os.walk(target, topdown=False):
		for name in files:
			path = os.path.join(root, name)
			if os.path.normcase(path) not in seen:
				os.remove(path)
		if os.path.normcase(root) not in seen and root != target:
			shutil.rmtree(root, ignore_errors=True)


def overlay(source, target):
	"""Copies every file of `source` over `target` (a variant's changed prompts, skills, docs)."""
	if not os.path.isdir(source):
		return 0
	count = 0
	for root, _, files in os.walk(source):
		for name in files:
			src = os.path.join(root, name)
			dst = os.path.join(target, os.path.relpath(src, source))
			os.makedirs(os.path.dirname(dst), exist_ok=True)
			shutil.copyfile(src, dst)
			count += 1
	return count


def run(argv, cwd, timeout=600, env=None, stdin=None):
	"""(exit code, stdout, stderr, seconds); exit code None on a timeout."""
	started = time.time()
	try:
		done = subprocess.run(argv, cwd=cwd, capture_output=True, timeout=timeout, env=env, stdin=stdin or subprocess.DEVNULL)
		return done.returncode, done.stdout.decode("utf-8", "replace"), done.stderr.decode("utf-8", "replace"), time.time() - started
	except subprocess.TimeoutExpired as expired:
		return None, (expired.stdout or b"").decode("utf-8", "replace"), (expired.stderr or b"").decode("utf-8", "replace"), time.time() - started


def nuke(words, cwd, timeout=900):
	return run(["cmd", "/c", NUKE] + list(words), cwd, timeout)


# --- Measuring a program -------------------------------------------------------------------------------------------

class _MemoryCounters(ctypes.Structure):
	_fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
		("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
		("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
		("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


def _process_usage(handle):
	"""Peak working set and peak private bytes, and CPU seconds (user + kernel), of an ended process."""
	counters = _MemoryCounters()
	counters.cb = ctypes.sizeof(counters)
	ctypes.WinDLL("psapi").GetProcessMemoryInfo(wintypes.HANDLE(int(handle)), ctypes.byref(counters), counters.cb)
	times = [wintypes.FILETIME() for _ in range(4)]
	ctypes.WinDLL("kernel32").GetProcessTimes(wintypes.HANDLE(int(handle)), *[ctypes.byref(t) for t in times])
	ticks = lambda t: (t.dwHighDateTime << 32) | t.dwLowDateTime
	return counters.PeakWorkingSetSize, counters.PeakPagefileUsage, (ticks(times[2]) + ticks(times[3])) / 1e7


def measure(argv, cwd, timeout=300, stdin_path=None):
	"""Runs a program: {exit, stdout, stderr, seconds, cpu, peakWorkingSet, peakPrivate}; exit None on a timeout."""
	stdin = open(stdin_path, "rb") if stdin_path else subprocess.DEVNULL
	try:
		started = time.perf_counter()
		process = subprocess.Popen(argv, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=stdin)
		try:
			out, err = process.communicate(timeout=timeout)
			code = process.returncode
		except subprocess.TimeoutExpired:
			process.kill()
			out, err = process.communicate()
			code = None
		seconds = time.perf_counter() - started
		peak, private, cpu = _process_usage(process._handle)
	finally:
		if stdin_path:
			stdin.close()
	return {"exit": code, "stdout": out.decode("utf-8", "replace"), "stderr": err.decode("utf-8", "replace"), "seconds": seconds,
		"cpu": cpu, "peakWorkingSet": peak, "peakPrivate": private}


# --- Model settings ------------------------------------------------------------------------------------------------

def settings_profile(path, name):
	import jsonc
	with open(path, encoding="utf-8") as f:
		models = jsonc.loads(f.read()).get("ai.models", {})
	if not models:
		raise SystemExit("no ai.models in %s (a Microsoft Store Python reads its own private copy of AppData: run vendor/python/python.exe, or pass --settings)" % path)
	name = name or next(iter(models))
	if name not in models:
		raise SystemExit("no model %s in %s (it has %s)" % (name, path, ", ".join(models)))
	return name, models[name]


def write_json(path, value):
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		json.dump(value, f, indent="\t")


def read_json(path, default=None):
	if not os.path.exists(path):
		return default
	with open(path, encoding="utf-8") as f:
		return json.load(f)


def source_files(folder, extensions):
	"""Source files under `folder`, relative, skipping build output and hidden folders."""
	found = []
	for root, folders, files in os.walk(folder):
		folders[:] = [d for d in folders if not d.startswith(".") and d not in ("build", "bin", "node_modules")]
		found += [os.path.relpath(os.path.join(root, name), folder) for name in files if name.endswith(extensions)]
	return sorted(found)


def strip_c_comments(text):
	return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
