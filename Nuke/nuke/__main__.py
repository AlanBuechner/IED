"""Entry point: `python <IED>/Nuke/nuke ...` (what bin/nuke.cmd runs) or `python -m nuke` with Nuke/ on sys.path."""

import os
import sys

NUKE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if __package__ in (None, ""):
	sys.path = [p for p in sys.path if os.path.normcase(os.path.abspath(p or ".")) != os.path.normcase(os.path.dirname(os.path.abspath(__file__)))]
	sys.path.insert(0, NUKE_DIR)
sys.dont_write_bytecode = True
if sys.version_info < (3, 11):
	sys.exit(f"nuke needs Python 3.11 or newer (this is {sys.version.split()[0]}); run setup.bat in the IED checkout")
for stream in (sys.stdout, sys.stderr):
	stream.reconfigure(errors="replace")

from nuke.cli import main, process_exit_code  # noqa: E402

sys.exit(process_exit_code(main()))
