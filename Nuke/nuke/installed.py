"""The Nuke an IED install carries (IED docs/21 "As built: nuke install-ied"): its home is the checkout the install
was made from (install.json's `source`), and in a folder of a checkout that holds a Nuke of its own, that Nuke runs."""

import json
import os
import subprocess
import sys

from . import fsutil

RECORD = "install.json"
ENTRY = os.path.join("Nuke", "nuke", "__main__.py")


def is_install(folder):
	"""Whether `folder` (the one holding Nuke/) is an IED install: nuke install-ied writes install.json there last."""
	return os.path.isfile(os.path.join(folder, RECORD))


def holds_nuke(folder):
	return os.path.isfile(os.path.join(folder, ENTRY))


def source_checkout(folder):
	"""The checkout the IED install at `folder` was made from; None when `folder` is no install, or its record names
	no folder that still holds Nuke."""
	try:
		with open(os.path.join(folder, RECORD), encoding="utf-8") as f:
			source = json.load(f).get("source")
	except (OSError, ValueError, AttributeError):
		return None
	return fsutil.norm(source) if isinstance(source, str) and source and holds_nuke(source) else None


def home_of(folder):
	"""The home of the Nuke in `folder`/Nuke: the checkout an install there was made from, else `folder` itself."""
	return source_checkout(folder) or fsutil.norm(folder)


def carrying_checkout(start):
	"""The nearest folder at or above `start` that holds a Nuke (Nuke/nuke); None when there is none."""
	folder = os.path.abspath(start)
	while True:
		if holds_nuke(folder):
			return fsutil.norm(folder)
		parent = os.path.dirname(folder)
		if parent == folder:
			return None
		folder = parent


def run_nuke(checkout, argv):
	"""Runs the Nuke `checkout` holds with `argv`, on the checkout's private Python when it has one; its exit code.
	Ctrl+C reaches that Nuke too, so this waits for it to stop."""
	python = os.path.join(checkout, "vendor", "python", "python.exe")
	program = [python if os.path.isfile(python) else sys.executable, os.path.join(checkout, "Nuke", "nuke")] + list(argv)
	process = subprocess.Popen(program)
	while True:
		try:
			return process.wait()
		except KeyboardInterrupt:
			continue
