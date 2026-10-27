"""What lands beside a program besides itself: the [[project.copy]] entries of the program and of the libraries it
reaches (a file is copied when it changed, a folder mirrored), and an `assets` link to the project's assets folder."""

import fnmatch
import os
import shutil
import subprocess

# NTFS keeps 1-2 s mtime granularity on some volumes: a copy is stamped older than its source, so a later edit of the
# source is always newer than the copy (Nuke's fsutil does the same).
COPY_MTIME_NUDGE_SECONDS = 2


def _is_copy_of(source, dest):
	try:
		return abs(os.path.getmtime(dest) - (os.path.getmtime(source) - COPY_MTIME_NUDGE_SECONDS)) < 0.01 and \
			os.path.getsize(dest) == os.path.getsize(source)
	except OSError:
		return False


def copy_file(source, dest, log):
	"""Copies one file when `dest` is not its copy as it is now; True if it did. A file held open by a running program
	is left for the next build, with a warning."""
	if _is_copy_of(source, dest):
		return False
	try:
		os.makedirs(os.path.dirname(dest), exist_ok=True)
		if os.path.exists(dest):
			os.remove(dest)
		shutil.copy2(source, dest)
	except OSError as error:
		log.line(f"warning: could not update {dest} ({error}); it is in use, the next build copies it")
		return False
	stamp = os.path.getmtime(source) - COPY_MTIME_NUDGE_SECONDS
	os.utime(dest, (stamp, stamp))
	return True


def excluded(relative, patterns):
	"""Whether a file at `relative` in a copied folder is one `patterns` leave out: a pattern matches the path ('/'-separated)
	or the file's name, with fnmatch's globs (as Nuke's fsutil.excluded)."""
	relative = relative.replace("\\", "/")
	return any(fnmatch.fnmatch(relative, p) or fnmatch.fnmatch(relative.rsplit("/", 1)[-1], p) for p in patterns)


def mirror_folder(source, dest, log, exclude=()):
	"""Makes `dest` a copy of `source`: changed files copied, files `source` no longer has - and those `exclude` leaves
	out - removed. The number copied."""
	copied, wanted = 0, set()
	for base, dirs, files in os.walk(source):
		for name in files:
			path = os.path.join(base, name)
			relative = os.path.relpath(path, source)
			if excluded(relative, exclude):
				continue
			wanted.add(os.path.normcase(relative))
			copied += copy_file(path, os.path.join(dest, relative), log)
	for base, dirs, files in os.walk(dest, topdown=False):
		for name in files:
			path = os.path.join(base, name)
			if os.path.normcase(os.path.relpath(path, dest)) not in wanted:
				try:
					os.remove(path)
				except OSError as error:
					log.line(f"warning: could not remove {path} ({error})")
		if base != dest and not os.listdir(base):
			os.rmdir(base)
	return copied


def copy_all(copies, bin_dir, log):
	"""Every (source, destination, exclude) of `copies` into `bin_dir`; the number of files copied."""
	copied = 0
	for source, dest, exclude in copies:
		target = os.path.join(bin_dir, dest)
		if os.path.isdir(source):
			copied += mirror_folder(source, target, log, exclude)
		elif os.path.isfile(source):
			copied += copy_file(source, target, log)
		else:
			raise ValueError(f"copy source {source} does not exist")
	return copied


def link_assets(project_dir, bin_dir):
	"""<bin>/assets -> <project>/assets for a program whose project has one (a junction when symbolic links need
	rights the user does not have)."""
	assets, link = os.path.join(project_dir, "assets"), os.path.join(bin_dir, "assets")
	if not os.path.isdir(assets):
		return
	if os.path.lexists(link) and not os.path.exists(link):
		os.unlink(link)
	if os.path.lexists(link):
		return
	try:
		os.symlink(assets, link, target_is_directory=True)
	except OSError:
		subprocess.run(["cmd", "/c", "mklink", "/J", link.replace("/", "\\"), assets.replace("/", "\\")], check=False,
			stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
