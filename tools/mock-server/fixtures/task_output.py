"""Scripted task runs: ANSI build/test/run output with timings, and a flood generator.

A task is a list of steps; a step yields (delay_seconds, source, text) lines and ends with an exit code.
Diagnostics are parsed from 'path(line,col): severity CODE: message' lines by the task service.
"""

ESC = "\x1b["
BOLD, RESET, RED, GREEN, YELLOW, MAGENTA, CYAN = ESC + "1m", ESC + "0m", ESC + "31m", ESC + "32m", ESC + "33m", ESC + "35m", ESC + "36m"
EXE = "build/bin/Development-windows-x64/demo/demo.exe"
COMPILE_UNITS = ["src/engine/math.h", "src/engine/render.c", "src/game/player.c", "src/main.c", "tests/test_math.c"]


def _compile_lines(error):
	total = len(COMPILE_UNITS) + 1
	yield 0.05, "stdout", "%s==> Build demo | Development | x64%s\r\n" % (BOLD, RESET)
	for i, unit in enumerate(COMPILE_UNITS, 1):
		yield 0.25, "stdout", "[%d/%d] %scompile%s %s\r\n" % (i, total, CYAN, RESET, unit)
		if unit == "src/engine/render.c":
			yield 0.05, "stderr", "%ssrc/engine/render.c(9,8): warning W1001: unused variable 'scale'%s\r\n" % (YELLOW, RESET)
		if error and unit == "src/game/player.c":
			yield 0.05, "stderr", "%ssrc/game/player.c(11,61): error E2001: 'vec2_scale' expects (Vec2, float), got (Vec2, double)%s\r\n" % (RED, RESET)
	if error:
		yield 0.1, "stdout", "%sBuild FAILED%s: 1 error, 1 warning (1.42 s)\r\n" % (RED, RESET)
		return 1
	yield 0.3, "stdout", "[%d/%d] %slink%s %s\r\n" % (total, total, MAGENTA, RESET, EXE)
	yield 0.1, "stdout", "%sBuild succeeded%s: 0 errors, 1 warning (2.31 s)\r\n" % (GREEN, RESET)
	return 0


def build_step():
	return "Build demo", "nuke build dev", _compile_lines(False)


def failing_build_step():
	return "Build demo", "nuke build dev", _compile_lines(True)


def _test_lines():
	yield 0.1, "stdout", "running 3 tests\r\n"
	yield 0.2, "stdout", "%sPASS%s test_vec2_add (0.1 ms)\r\n" % (GREEN, RESET)
	yield 0.2, "stdout", "%sPASS%s test_vec2_scale (0.1 ms)\r\n" % (GREEN, RESET)
	yield 0.2, "stderr", "%sFAIL%s test_player_update: expected position.x == 1.0, got 0.0167\r\n" % (RED, RESET)
	yield 0.05, "stderr", "%stests/test_player.c(14,5): error T0001: assertion failed: p.position.x == 1.0f%s\r\n" % (RED, RESET)
	yield 0.1, "stdout", "2 passed, %s1 failed%s\r\n" % (RED, RESET)
	return 1


def test_step():
	return "Run tests", "nuke test dev", _test_lines()


def _run_lines():
	yield 0.2, "stdout", "demo 0.1 (Development x64)\r\n"
	for frame in range(0, 601, 100):
		yield 0.3, "stdout", "frame %d  player.position = (%.2f, 0.00)\r\n" % (frame, frame / 60.0)
	yield 0.1, "stdout", "%sexited with code 0%s\r\n" % (BOLD, RESET)
	return 0


def run_step():
	return "Run demo", EXE, _run_lines()


def _flood_lines(count):
	for i in range(1, count + 1):
		colour = YELLOW if i % 100 == 0 else ""
		yield 0.0, "stdout", "%sflood line %06d lorem ipsum dolor sit amet, consectetur adipiscing elit%s\r\n" % (colour, i, RESET if colour else "")
	return 0


def flood_step(count):
	return "Flood output", "flood --lines %d" % count, _flood_lines(count)


TASKS = {
	"build": {"label": "Build", "slot": "build", "steps": lambda params: [build_step()]},
	"build-broken": {"label": "Build (with an error)", "slot": None, "steps": lambda params: [failing_build_step()]},
	"test": {"label": "Test", "slot": "test", "steps": lambda params: [test_step()]},
	"run": {"label": "Run", "slot": "run", "steps": lambda params: [build_step(), run_step()]},
	"flood": {"label": "Flood (50k lines)", "slot": None, "steps": lambda params: [flood_step(params.get("lines", 50000))]},
}

OUTPUT_SOURCE = {"build": "build", "build-broken": "build", "test": "test", "run": "run", "flood": "build"}
