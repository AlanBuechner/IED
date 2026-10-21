"""The web client end to end (docs/19 "Frontend unit" and "E2E"): the in-browser unit tests, then every scenario at its
viewports against the mock server and, with --server, the real ied-server. Screenshots go to build/e2e/.

	python -I tests/e2e/run_e2e.py [--server build/bin/Development-windows-x64/ied-server/ied-server.exe] [-k name] [--headed]

Exit code 0 when everything passed.
"""

import argparse
import importlib.util
import os
import subprocess
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from harness import Run  # noqa: E402
from scenarios import SCENARIOS  # noqa: E402
from targets import REPO, MockTarget, RealTarget  # noqa: E402

SHOTS = os.path.join(REPO, "build", "e2e")


def unit_tests(browser, target):
	page = browser.new_page()
	errors = []
	page.on("pageerror", lambda e: errors.append(str(e)))
	page.goto(target.url + "/tests/index.html")
	page.wait_for_function("document.documentElement.dataset.done === '1'", timeout=60000)
	results = page.evaluate("window.ied_test_results")
	page.close()
	failures = ["unit: %s: %s" % (f["name"], f["message"]) for f in results["failures"]] + ["unit: page error: %s" % e for e in errors]
	print("unit tests: %d passed, %d failed" % (results["passed"], results["failed"]))
	return failures


def run_scenarios(browser, target, pattern):
	failures = []
	for name, fn, viewports, targets in SCENARIOS:
		if target.kind not in targets or (pattern and pattern not in name):
			continue
		for viewport in viewports:
			run = Run(target, browser, viewport, SHOTS, name)
			label = "%s %s %s" % (target.kind, viewport, name)
			started = time.monotonic()
			try:
				fn(run)
				unexpected = [e for e in run.errors if "Failed to load resource" not in e or "404" in e]
				if unexpected:
					raise AssertionError("console errors: " + "; ".join(unexpected[:5]))
				print("ok   %s (%.1f s)" % (label, time.monotonic() - started))
			except Exception as e:
				failures.append("%s: %s" % (label, e))
				print("FAIL %s: %s" % (label, e))
				if not isinstance(e, AssertionError):
					traceback.print_exc()
				try:
					run.shot("failure")
				except Exception:
					pass
			finally:
				run.close()
	return failures


INSTALL_HINT = "tests/e2e needs Playwright for Python: python -m pip install playwright && python -m playwright install chromium"


def playwright_python():
	"""None when this interpreter has Playwright, else another python on PATH that has it (Nuke runs its bundled Python,
	which has none, and Windows finds that one first for a bare `python`)."""
	if importlib.util.find_spec("playwright"):
		return None
	here = os.path.normcase(os.path.dirname(os.path.abspath(sys.executable)))
	for folder in os.environ.get("PATH", "").split(os.pathsep):
		if not folder or os.path.normcase(os.path.abspath(folder)) == here:
			continue
		candidate = os.path.join(folder, "python.exe" if os.name == "nt" else "python3")
		if os.path.isfile(candidate) and subprocess.run([candidate, "-I", "-c", "import playwright"], capture_output=True).returncode == 0:
			return candidate
	raise SystemExit(INSTALL_HINT)


def main(argv):
	other = playwright_python()
	if other:
		return subprocess.run([other, "-I", os.path.abspath(__file__)] + argv).returncode
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--server", help="the ied-server executable; without it only the mock runs")
	parser.add_argument("-k", dest="pattern", help="only scenarios whose name contains this (\"unit\": the unit tests only)")
	parser.add_argument("--headed", action="store_true")
	args = parser.parse_args(argv)
	from playwright.sync_api import sync_playwright
	failures = []
	with sync_playwright() as p:
		browser = p.chromium.launch(headless=not args.headed)
		targets = [MockTarget()] + ([RealTarget(args.server)] if args.server else [])
		for target in targets:
			target.start()
			try:
				print("== %s at %s" % (target.kind, target.url))
				if target.kind == "mock" and (not args.pattern or args.pattern == "unit"):
					failures += unit_tests(browser, target)
				failures += run_scenarios(browser, target, args.pattern)
			finally:
				target.stop()
		browser.close()
	print("\n%d failure(s); screenshots in %s" % (len(failures), SHOTS))
	for failure in failures:
		print("  " + failure)
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
