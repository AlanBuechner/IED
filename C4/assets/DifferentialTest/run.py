"""Differential test: random programs compiled by C4 (at each -O level) and by clang as C must print the same lines.

	python assets/DifferentialTest/run.py                    # the gate: seeds 1..150 at -O0 and -O2
	python assets/DifferentialTest/run.py --start 1000 --count 5000 --size 1.5
	python assets/DifferentialTest/run.py --seed 4242 --keep # one program, sources kept

A failing seed keeps its folder under assets/build-output/differential/<seed> (main.cf, main.c, the outputs).
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
C4_ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)

from difftest import generate, render, helpers  # noqa: E402

COMPILER = os.path.join(C4_ROOT, 'bin', 'Development-windows-x64', 'C4', 'C4.exe')
CLANG = os.path.join(C4_ROOT, 'Frost', 'vendor', 'Compiler', 'bin', 'clang.exe')
OUTPUT = os.path.join(C4_ROOT, 'assets', 'build-output', 'differential')
RUN_SECONDS = 20
COMPILE_SECONDS = 300


def run(argv, cwd, seconds):
	try:
		done = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=seconds)
		return done.returncode, done.stdout, done.stderr
	except subprocess.TimeoutExpired:
		return -1, '', 'timed out after %d s' % seconds


def first_difference(expected, actual):
	a, b = expected.splitlines(), actual.splitlines()
	for i in range(max(len(a), len(b))):
		left = a[i] if i < len(a) else '<missing>'
		right = b[i] if i < len(b) else '<missing>'
		if left != right:
			return 'line %d: C printed %r, C4 printed %r' % (i, left, right)
	return ''


def write_sources(folder, seed, size):
	gen = generate.Generator(seed, size)
	program = gen.program()
	calls = gen.calls(program)
	os.makedirs(os.path.join(folder, 'c4'), exist_ok=True)
	with open(os.path.join(folder, 'c4', 'main.cf'), 'w', newline='\n') as f:
		f.write(render.source(helpers.C4, program, calls))
	with open(os.path.join(folder, 'main.c'), 'w', newline='\n') as f:
		f.write(render.source(helpers.C, program, calls))


def check_seed(seed, size, levels, keep):
	"""Returns '' when every build agrees with C, else what went wrong."""
	folder = os.path.join(OUTPUT, str(seed))
	shutil.rmtree(folder, ignore_errors=True)
	write_sources(folder, seed, size)
	code, out, err = run([CLANG, '-O0', '-w', 'main.c', '-o', 'c.exe'], folder, COMPILE_SECONDS)
	if code != 0:
		return 'the C version did not compile (a generator bug): ' + (err or out).strip().splitlines()[0]
	code, expected, err = run([os.path.join(folder, 'c.exe')], folder, RUN_SECONDS)
	if code != 0:
		return 'the C version failed (exit %d, a generator bug): %s' % (code, err.strip()[:200])
	problems = []
	for level in levels:
		exe = 'c4_O%d.exe' % level
		code, out, err = run([COMPILER, 'c4', '-O%d' % level, '-w', '-o', exe], folder, COMPILE_SECONDS)
		if code != 0:
			text = '\n'.join(l for l in (out + err).splitlines() if 'error' in l)[:400]
			problems.append('-O%d does not compile: %s' % (level, text or (out + err)[-400:]))
			continue
		code, actual, err = run([os.path.join(folder, exe)], folder, RUN_SECONDS)
		with open(os.path.join(folder, 'c4_O%d.txt' % level), 'w') as f:
			f.write(actual)
		if code != 0:
			problems.append('-O%d exits %d: %s' % (level, code, err.strip()[:200]))
		elif actual != expected:
			problems.append('-O%d differs from C at %s' % (level, first_difference(expected, actual)))
	if problems:
		with open(os.path.join(folder, 'c.txt'), 'w') as f:
			f.write(expected)
	elif not keep:
		shutil.rmtree(folder, ignore_errors=True)
	return '; '.join(problems)


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument('--start', type=int, default=1)
	parser.add_argument('--count', type=int, default=150)
	parser.add_argument('--seed', type=int, help='check one seed')
	parser.add_argument('--size', type=float, default=1.0, help='scales globals, functions, statements and depth')
	parser.add_argument('--levels', default='0,2', help='C4 optimization levels to compare, comma-separated')
	parser.add_argument('--jobs', type=int, default=4)
	parser.add_argument('--keep', action='store_true', help='keep the sources of passing seeds too')
	args = parser.parse_args()

	seeds = [args.seed] if args.seed is not None else list(range(args.start, args.start + args.count))
	levels = [int(x) for x in args.levels.split(',')]
	failures = 0
	with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
		results = pool.map(lambda s: (s, check_seed(s, args.size, levels, args.keep)), seeds)
		for seed, problem in results:
			if problem:
				failures += 1
				print('FAIL seed %d: %s' % (seed, problem), flush=True)
	print('PASSED: %d   FAILED: %d   TOTAL: %d' % (len(seeds) - failures, failures, len(seeds)))
	return 1 if failures else 0


if __name__ == '__main__':
	sys.exit(main())
