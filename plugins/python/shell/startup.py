"""IED's Python terminal (docs/05 "Terminal profiles"): runs first in `python -i startup.py <packages>`, then the
prompt is the interactive interpreter. IED's own Python is the embeddable CPython in vendor/python, whose python312._pth
fixes sys.path (PYTHONPATH and the other PYTHON* variables are ignored, the script's folder is not added) and turns on
`site`. So this script gives the terminal what an ordinary `python` prompt has, and a place for packages:

It puts the current folder (the workspace's) first on sys.path, as the prompt of an ordinary Python has it, and
<packages> (IED's data folder, plugins/ied.python/packages) as a site folder: what pip() installs goes there, never into
IED's install, and other Pythons on the machine never see it. pip("install requests") runs the pip setup put in
vendor/pip with --target <packages>; pip("list"), pip("show requests") and pip("uninstall -y requests") work too.
Typing `pip install requests` at the prompt is Python, not a shell command: a SyntaxError, under which the prompt says
to call pip() instead.

Everything it defines lives inside _ied_start, so the prompt starts with only `pip` beside Python's own names.
"""


def _ied_start():
	import importlib
	import os
	import site
	import subprocess
	import sys

	packages = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(os.path.expanduser("~"), ".ied-python-packages")
	pip_folder = os.path.normpath(os.path.join(os.path.dirname(sys.executable), os.pardir, "pip"))
	boot = "import runpy, sys; sys.path[:0] = [sys.argv.pop(1), sys.argv.pop(1)]; sys.argv[0] = 'pip'; runpy.run_module('pip', run_name='__main__', alter_sys=True)"

	class Pip:
		"""pip for this terminal's packages folder: pip("install requests"), pip("list"), pip("uninstall -y requests")."""

		def __repr__(self):
			return 'pip("install <name>") installs into %s; pip("list"), pip("show <name>"), pip("uninstall -y <name>")' % packages

		def __call__(self, command="help"):
			args = command.split() if isinstance(command, str) else [str(a) for a in command]
			if not os.path.isfile(os.path.join(pip_folder, "pip", "__main__.py")):
				print("pip is not there (%s): IED's setup puts it there (setup.bat in its source; an install copies it)." % pip_folder)
				return 1
			if args and args[0] == "install" and not any(a == "-t" or a.startswith("--target") for a in args):
				args[1:1] = ["--target", packages]
			elif args and args[0] in ("list", "freeze") and "--path" not in args:
				args[1:1] = ["--path", packages]
			code = subprocess.run([sys.executable, "-I", "-c", boot, pip_folder, packages] + args).returncode
			importlib.invalidate_caches()
			site.addsitedir(packages)
			return code

	def hint_for_pip_lines(original):
		def hook(kind, value, trace):
			original(kind, value, trace)
			text = (getattr(value, "text", None) or "").strip()
			if issubclass(kind, SyntaxError) and (text.startswith("pip ") or text.startswith("pip3 ")):
				print('This is Python, not a shell: call pip("%s") to run it.' % text.split(None, 1)[1].replace('"', "'"))
		return hook

	os.makedirs(packages, exist_ok=True)
	site.addsitedir(packages)
	if "" not in sys.path:
		sys.path.insert(0, "")
	sys.argv = [""]
	sys.excepthook = hint_for_pip_lines(sys.excepthook)
	print("Python %s - IED's own Python (vendor\\python). exit() ends it." % sys.version.split()[0])
	print('pip("install <name>") installs into %s' % packages)
	return Pip()


pip = _ied_start()
del _ied_start
globals().pop("__file__", None)
globals().pop("__cached__", None)
__doc__ = None
