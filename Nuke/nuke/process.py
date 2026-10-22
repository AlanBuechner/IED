"""Running child processes with their output streamed line by line to the log."""

import os
import subprocess


def run(argv, log, cwd=None, env=None, shell=False, chatter=None):
	"""Run `argv` (a list, or a string with shell=True), streaming its merged stdout/stderr; lines `chatter` (a compiled
	regex) matches are progress and only shown with --verbose. Returns the exit code."""
	log.detail(f"run: {argv if shell else subprocess.list2cmdline(argv)}" + (f"  (in {cwd})" if cwd else ""))
	merged_env = None
	if env:
		merged_env = dict(os.environ)
		merged_env.update(env)
	try:
		proc = subprocess.Popen(argv, cwd=cwd, env=merged_env, shell=shell, stdout=subprocess.PIPE,
			stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace", bufsize=1)
	except FileNotFoundError as error:
		log.line(f"nuke: cannot start {argv if shell else argv[0]}: {error.strerror}")
		return 127
	with proc:
		for text in proc.stdout:
			if chatter is None or log.verbose or not chatter.match(text):
				log.write(text)
	return proc.returncode


def run_attached(argv, cwd=None, env=None):
	"""Run a program with the console attached (stdin included), for `nuke run`. Returns its exit code."""
	merged_env = None
	if env:
		merged_env = dict(os.environ)
		merged_env.update(env)
	try:
		return subprocess.run(argv, cwd=cwd, env=merged_env).returncode
	except FileNotFoundError:
		return 127
