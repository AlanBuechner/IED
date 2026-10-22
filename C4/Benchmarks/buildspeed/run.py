"""Build speed against Go and Rust: the scale test's program (assets/ScaleTest/generate.py, about 60k lines in 200
modules) and the same program in Go (golang.py: a package per module) and Rust (rust.py: a module per module in one
crate), each built cold, again with nothing changed, after a constant in one function of the middle module changes,
and after it changes back. A language whose toolchain is not installed is skipped.

    python Benchmarks/buildspeed/run.py [--modules 200] [--only c4,go,rust] [--jobs n]

C4 builds as Nuke's configurations do: Debug (-O0 -g --incremental), Development (-O3 -g --incremental) and Dist
(-O3); Go with `go build`; Rust with `cargo build` (dev) and `cargo build --release`.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'assets', 'ScaleTest'))
import generate  # noqa: E402  (the scale test's C4 program)
import golang  # noqa: E402
import rust  # noqa: E402

COMPILER = os.path.join(os.path.dirname(REPO), 'build', 'bin', 'Development-windows-x64', 'C4', 'C4.exe')
WORK = os.path.join(REPO, 'assets', 'build-output', 'buildspeed')


def edited_constant(modules):
    """The text of one step's constant in the middle module, and what it changes to."""
    i, k = modules // 2, 5
    add = 1442695040888963407 + i * 977 + k * 131
    return i, str(add), str(add + 1)


def timed(command, cwd, env=None):
    start = time.perf_counter()
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, errors='replace')
    seconds = time.perf_counter() - start
    if result.returncode != 0:
        print((result.stdout + result.stderr)[-3000:])
    return seconds if result.returncode == 0 else None


def replace_in(path, old, new):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    if text.count(old) != 1:
        sys.exit(f'{path}: expected one {old}')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text.replace(old, new))


def scenario(name, build, edited_file, modules):
    """Cold, unchanged, an edit and the edit undone: each build's seconds."""
    _, before, after = edited_constant(modules)
    times = [build()]
    times.append(build())
    replace_in(edited_file, before, after)
    times.append(build())
    replace_in(edited_file, after, before)
    times.append(build())
    cells = ' '.join(f'{t:8.2f}' if t is not None else '  failed' for t in times)
    print(f'{name:34s} {cells}', flush=True)


def fresh(folder):
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    return folder


def c4(modules, jobs):
    if not os.path.exists(COMPILER):
        print(f'C4: no compiler at {COMPILER}')
        return
    source = os.path.join(WORK, 'c4', 'src')
    lines = generate.generate(source, modules)
    print(f'C4: {lines} lines')
    middle = os.path.join(source, f'm{modules // 2}.cf')
    for label, flags in (('C4 Debug (-O0 -g, incremental)', ['-O0', '-g', '--incremental']),
                         ('C4 Development (-O3 -g, incremental)', ['-O3', '-g', '--incremental']),
                         ('C4 Dist (-O3)', ['-O3'])):
        out = fresh(os.path.join(WORK, 'c4', 'out-' + flags[0] + str(len(flags))))
        command = [COMPILER, source, *flags, '-o', os.path.join(out, 'scale.exe'), '--int-dir', out]
        if jobs:
            command += ['-j', str(jobs)]
        scenario(label, lambda: timed(command, out), middle, modules)


def go(modules, jobs):
    tool = shutil.which('go') or r'C:\Program Files\Go\bin\go.exe'
    if not os.path.exists(tool):
        print('Go: not installed')
        return
    folder = os.path.join(WORK, 'go')
    shutil.rmtree(folder, ignore_errors=True)
    lines = golang.generate(folder, modules)
    print(f'Go: {lines} lines')
    env = dict(os.environ, GOCACHE=fresh(os.path.join(WORK, 'go-cache')), GOFLAGS='-mod=mod')
    command = [tool, 'build', '-o', 'scale.exe', '.']
    if jobs:
        command[2:2] = ['-p', str(jobs)]
    middle = os.path.join(folder, f'm{modules // 2}', f'm{modules // 2}.go')
    scenario('Go (go build)', lambda: timed(command, folder, env), middle, modules)


def rust_builds(modules, jobs):
    tool = shutil.which('cargo')
    if not tool:
        print('Rust: not installed')
        return
    folder = os.path.join(WORK, 'rust')
    shutil.rmtree(folder, ignore_errors=True)
    lines = rust.generate(folder, modules)
    print(f'Rust: {lines} lines')
    middle = os.path.join(folder, 'src', f'm{modules // 2}.rs')
    for label, flags in (('Rust dev (cargo build)', []), ('Rust release (cargo build --release)', ['--release'])):
        env = dict(os.environ, CARGO_TARGET_DIR=fresh(os.path.join(WORK, 'rust-target' + ''.join(flags))))
        command = [tool, 'build', '--offline', *flags] + (['-j', str(jobs)] if jobs else [])
        scenario(label, lambda: timed(command, folder, env), middle, modules)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--modules', type=int, default=generate.DEFAULT_MODULES)
    parser.add_argument('--only', default='c4,go,rust')
    parser.add_argument('--jobs', type=int, default=0)
    args = parser.parse_args()
    os.makedirs(WORK, exist_ok=True)
    print(f'{"build (seconds)":34s} {"cold":>8s} {"same":>8s} {"edit":>8s} {"undo":>8s}')
    chosen = args.only.split(',')
    if 'c4' in chosen:
        c4(args.modules, args.jobs)
    if 'go' in chosen:
        go(args.modules, args.jobs)
    if 'rust' in chosen:
        rust_builds(args.modules, args.jobs)


if __name__ == '__main__':
    main()
