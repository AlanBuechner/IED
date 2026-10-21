"""File helpers: normalized paths, globbing, newest/oldest mtimes and copy-if-newer mirroring."""

import fnmatch
import glob
import os
import shutil

# NTFS keeps 1-2 s mtime granularity on some volumes: a copy is stamped older than its source so a
# later edit of the source is always seen as newer.
COPY_MTIME_NUDGE_SECONDS = 2


def norm(path):
	return os.path.normpath(path).replace("\\", "/")


def join(*parts):
	return norm(os.path.join(*parts))


def same_path(a, b):
	return os.path.normcase(os.path.normpath(a)) == os.path.normcase(os.path.normpath(b))


def is_within(path, folder):
	path = os.path.normcase(os.path.abspath(path))
	folder = os.path.normcase(os.path.abspath(folder))
	return path == folder or path.startswith(folder.rstrip("\\/") + os.sep)


def mtime(path):
	try:
		return os.path.getmtime(path)
	except OSError:
		return 0.0


def ensure_dir(path):
	os.makedirs(path, exist_ok=True)


def files_under(folder, extensions=None):
	"""Every file below `folder` (sorted), optionally only those whose extension is in `extensions`."""
	if not os.path.isdir(folder):
		return []
	found = []
	for base, dirs, files in os.walk(folder):
		dirs.sort()
		for name in sorted(files):
			if extensions is None or os.path.splitext(name)[1].lower() in extensions:
				found.append(join(base, name))
	return found


def expand_globs(patterns, base):
	"""Files matching `patterns` (relative to `base` unless absolute; `**` recurses). Plain folders expand
	to every file below them; plain files are kept even when missing so callers can report them."""
	found = []
	for pattern in patterns:
		full = pattern if os.path.isabs(pattern) else os.path.join(base, pattern)
		if glob.has_magic(full):
			found.extend(norm(p) for p in glob.glob(full, recursive=True) if os.path.isfile(p))
		elif os.path.isdir(full):
			found.extend(files_under(full))
		else:
			found.append(norm(full))
	return sorted(set(found))


def matches_any(relative_path, patterns):
	relative_path = relative_path.replace("\\", "/")
	return any(fnmatch.fnmatch(relative_path, p) or fnmatch.fnmatch(relative_path + "/", p) for p in patterns)


def write_if_changed(path, text):
	"""Write `text` only when it differs, so mtimes stay stable for incremental checks. True if written."""
	if os.path.isfile(path):
		with open(path, encoding="utf-8") as f:
			if f.read() == text:
				return False
	ensure_dir(os.path.dirname(path))
	with open(path, "w", encoding="utf-8", newline="\n") as f:
		f.write(text)
	return True


def read_text(path):
	try:
		with open(path, encoding="utf-8") as f:
			return f.read()
	except OSError:
		return None


def copy_if_newer(source, dest):
	"""Copy one file when `dest` is missing or `source` changed since it was copied. True if copied."""
	if os.path.exists(dest) and _copy_of(source, dest):
		return False
	ensure_dir(os.path.dirname(dest))
	if os.path.exists(dest):
		os.remove(dest)
	shutil.copy2(source, dest)
	stamp = mtime(source) - COPY_MTIME_NUDGE_SECONDS
	os.utime(dest, (stamp, stamp))
	return True


def _copy_of(source, dest):
	"""Whether `dest` is copy_if_newer's copy of `source` as it is now: the stamp the copy was given and the same size."""
	return abs(mtime(dest) - (mtime(source) - COPY_MTIME_NUDGE_SECONDS)) < 0.01 and os.path.getsize(dest) == os.path.getsize(source)


def copy_tree_if_newer(source, dest):
	"""Copy every file of `source` into `dest` when newer. Returns the number of files copied."""
	copied = 0
	for path in files_under(source):
		copied += copy_if_newer(path, join(dest, os.path.relpath(path, source)))
	return copied


def mirror_tree(source, dest):
	"""Make `dest` an exact copy of `source`: newer files copied, files missing from `source` removed."""
	copied = copy_tree_if_newer(source, dest)
	wanted = {os.path.normcase(os.path.relpath(p, source)) for p in files_under(source)}
	for path in files_under(dest):
		if os.path.normcase(os.path.relpath(path, dest)) not in wanted:
			os.remove(path)
	remove_empty_dirs(dest)
	return copied


def remove_empty_dirs(folder):
	for base, dirs, files in os.walk(folder, topdown=False):
		if base != folder and not os.listdir(base):
			os.rmdir(base)


def remove_tree(path):
	if os.path.isdir(path):
		shutil.rmtree(path)
		return True
	return False
