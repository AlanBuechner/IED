"""The scale test: writes a program of about 54k lines in 401 files (generate.py) under assets/build-output/scale,
compiles it at -O0 and -O2 with the repository's C4.exe and reports each compile's time, then runs each build on
several threads for a while; every run's result must match the single-threaded one.

    python assets/ScaleTest/run.py [--seconds 30] [--threads 4] [--modules 200] [--jobs 3] [--levels O0,O2]

Exit code: 0 when every build compiled and every run matched.
"""
import argparse
import os
import subprocess
import sys
import time

import generate

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
COMPILER = os.path.join(os.path.dirname(REPO), 'build', 'bin', 'Development-windows-x64', 'C4', 'C4.exe')
WORK = os.path.join(REPO, 'assets', 'build-output', 'scale')


def compile_program(source, level, jobs):
    exe = os.path.join(WORK, f'scale-{level}.exe')
    start = time.perf_counter()
    result = subprocess.run([COMPILER, source, f'-{level}', '-j', str(jobs), '-o', exe],
                            capture_output=True, text=True)
    seconds = time.perf_counter() - start
    if result.returncode != 0:
        print(result.stdout[-4000:] + result.stderr[-4000:])
    return exe if result.returncode == 0 else None, seconds


def run_program(exe, seconds, threads):
    env = dict(os.environ, SCALE_SECONDS=str(seconds), SCALE_THREADS=str(threads))
    result = subprocess.run([exe], capture_output=True, text=True, env=env, timeout=seconds * 4 + 600)
    print('  ' + (result.stdout.strip() or result.stderr.strip()))
    return result.returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--threads', type=int, default=4)
    parser.add_argument('--modules', type=int, default=generate.DEFAULT_MODULES)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--levels', default='O0,O2')
    args = parser.parse_args()
    if not os.path.exists(COMPILER):
        sys.exit(f'no compiler at {COMPILER}: build it first')
    source = os.path.join(WORK, 'src')
    lines = generate.generate(source, args.modules)
    print(f'scale test: {lines} lines in {2 * args.modules + 1} files')
    failures = 0
    for level in args.levels.split(','):
        exe, seconds = compile_program(source, level, args.jobs)
        print(f'-{level}: compiled in {seconds:.1f} s' if exe else f'-{level}: FAILED to compile ({seconds:.1f} s)')
        if not exe:
            failures += 1
            continue
        code = run_program(exe, args.seconds, args.threads)
        if code != 0:
            print(f'-{level}: FAILED (exit {code})')
            failures += 1
    print('PASSED' if failures == 0 else f'FAILED: {failures}')
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
