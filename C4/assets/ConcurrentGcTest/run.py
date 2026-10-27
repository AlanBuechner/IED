"""The collector's concurrent marking (D-79) under the programs that allocate the most: Bedrock's tests, the GC
isolated tests and the GC benchmark's check, with every automatic collection a concurrent cycle verified against a
fresh mark (C4_GC_VERIFY) and a 64 KiB trigger, so cycles run often. A wrong result or a missed object fails.

    python assets/ConcurrentGcTest/run.py

Runs after the bedrock gate step, whose build of the Bedrock tests it reuses."""
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
ENVIRONMENT = dict(os.environ, C4_GC_CONCURRENT='always', C4_GC_VERIFY='1', C4_GC_MIN_TRIGGER='65536')
POWERSHELL = ['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']

CHECKS = [
    ('Bedrock tests', [os.path.join(REPO, 'assets', 'build-output', 'bedrock', 'BedrockTests.exe')]),
    ('IsolatedTest MemoryGC', POWERSHELL + [os.path.join(REPO, 'assets', 'IsolatedTest', 'RunTests.ps1'), '-Filter', 'MemoryGC']),
    ('GC benchmark check', POWERSHELL + [os.path.join(REPO, 'Benchmarks', 'gc', 'run-benchmark.ps1'), '-Check']),
]


def run(name, command):
    result = subprocess.run(command, cwd=REPO, env=ENVIRONMENT, capture_output=True, text=True)
    status = 'PASS' if result.returncode == 0 else f'FAIL (exit {result.returncode})'
    print(f'  {status}  {name}')
    if result.returncode != 0:
        output = result.stdout + result.stderr
        failures = [line for line in output.splitlines() if 'FAIL' in line]
        if failures:  # a long run's failing case is far above its tail
            print('\n'.join(failures[:40]))
        print(output[-4000:])
    return result.returncode == 0


def main():
    passed = sum(run(name, command) for name, command in CHECKS)
    failed = len(CHECKS) - passed
    print(f'PASSED: {passed}   FAILED: {failed}   TOTAL: {len(CHECKS)}')
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
