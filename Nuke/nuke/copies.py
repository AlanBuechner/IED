"""What [[project.copy]] wrote into a bin folder, remembered, so the next copy removes what it no longer writes.

Several modules copy into one folder (every package's `Shaders` into `<bin>/Shaders`), so a destination cannot be mirrored
from one source. Instead each program keeps a manifest of the files its last copy wrote (relative to its bin folder), kept in
its int folder, not beside the program: it is not an output to ship. A file in the manifest that this copy does not write
is removed - its source was deleted, renamed, excluded or its copy entry gone. Nothing else in the bin folder is touched:
the program, files a user dropped in, files written at run time. With no manifest (the first build after this existed)
nothing is removed, as what is already there cannot be told from what Nuke put there."""

import json
import os

from . import fsutil

MANIFEST_NAME = "copies.json"
MANIFEST_VERSION = 1


def manifest_path(int_dir):
	return fsutil.join(int_dir, MANIFEST_NAME)


def relative_files(bin_dir, written):
	"""`written` (paths in `bin_dir`) as '/'-separated paths from it, in order, each once. A copy that lands outside the bin
	folder is not recorded: it is never pruned."""
	found, seen = [], set()
	for path in written:
		relative = fsutil.norm(os.path.relpath(path, bin_dir))
		if relative == ".." or relative.startswith("../") or os.path.isabs(relative) or os.path.normcase(relative) in seen:
			continue
		seen.add(os.path.normcase(relative))
		found.append(relative)
	return found


def read_manifest(int_dir):
	"""The files the last copy wrote, or None without a manifest (or one that is unreadable: treated as none)."""
	text = fsutil.read_text(manifest_path(int_dir))
	if text is None:
		return None
	try:
		document = json.loads(text)
		files = document["files"]
		if document.get("version") != MANIFEST_VERSION or not isinstance(files, list) or not all(isinstance(f, str) for f in files):
			return None
		return files
	except (ValueError, KeyError, TypeError):
		return None


def write_manifest(int_dir, files):
	fsutil.write_if_changed(manifest_path(int_dir), json.dumps({"version": MANIFEST_VERSION, "files": sorted(files)}, indent="\t") + "\n")


def prune(bin_dir, previous, current):
	"""Remove the files of `previous` that `current` no longer has, and the folders that leaves empty (never `bin_dir`
	itself, never a folder that holds something else). Returns how many files were removed."""
	wanted = {os.path.normcase(f) for f in current}
	removed = 0
	for relative in previous:
		path = fsutil.join(bin_dir, relative)
		if os.path.normcase(relative) in wanted or not fsutil.is_within(path, bin_dir) or fsutil.same_path(path, bin_dir):
			continue
		if os.path.isfile(path):
			fsutil.remove_file(path)
			removed += 1
			_remove_empty_parents(os.path.dirname(path), bin_dir)
	return removed


def _remove_empty_parents(folder, stop):
	while not fsutil.same_path(folder, stop) and fsutil.is_within(folder, stop) and os.path.isdir(folder) and not os.listdir(folder):
		os.rmdir(folder)
		folder = os.path.dirname(folder)


def record(bin_dir, int_dir, written):
	"""After a copy that wrote `written`: prune what the last one wrote and this did not, and remember this one's files.
	Returns the number of files pruned."""
	current = relative_files(bin_dir, written)
	previous = read_manifest(int_dir)
	removed = prune(bin_dir, previous, current) if previous else 0
	if current or previous:
		write_manifest(int_dir, current)
	return removed
