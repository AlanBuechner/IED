# DifferentialTest - C4 against C on random programs

`run.py` generates random programs, writes each one twice - as C4 and as C - compiles the C with the vendored
clang and the C4 at every `-O` level asked for, runs them all and compares their output line by line. A program
prints every global, array element and struct field (and the result of one call of each function) after each of
its procedures runs, so the first line that differs points at the state that went wrong.

```powershell
python assets/DifferentialTest/run.py                                # the gate: seeds 1..150 at -O0 and -O2
python assets/DifferentialTest/run.py --start 1000 --count 5000 --size 1.5 --jobs 6
python assets/DifferentialTest/run.py --seed 4242 --keep             # one program, its sources kept
```

A failing seed keeps its folder in `assets/build-output/differential/<seed>/`: `c4/main.cf`, `main.c`, the
outputs (`c.txt`, `c4_O0.txt`, ...) and the executables. A seed always generates the same program.

## What a program contains

- Every primitive type (8- to 64-bit integers, `float`, `double`, `bool`) in globals, arrays, struct fields,
  parameters and locals, with C's usual arithmetic conversions between them.
- Arithmetic, bitwise, shift, comparison and logical operators, casts between every pair of types, `?:`, compound
  assignment, subscripts, members, pointers to locals.
- `if`, `for`, `while`, `do`/`while`, `switch`, `break`, `continue`.
- Functions with scalar and struct parameters, returning scalars or structs (the calling convention both ways),
  and fields of returned structs (`f(x).a`).

## Rules that keep the C program defined

Any difference must be a C4 bug, so the generator never writes code whose C meaning is undefined or
implementation-specific beyond what clang and C4 agree on (two's complement, arithmetic right shift):

- Signed `+`, `-`, `*` run on the unsigned type of the same width (no signed overflow).
- `/` and `%` go through `div_T` / `mod_T`, which return the dividend for a zero divisor or `MIN / -1`.
- Shift amounts are masked to the promoted width; signed left shifts run unsigned.
- A float reaches an integer through `f2i_T`, which clamps out-of-range values and maps NaN to 0.
- Loops are counted; functions used inside expressions write only their own locals, so C's unspecified order of
  evaluating operands cannot show; the checksum maps every NaN to one bit pattern.

## Layout

| File | Contents |
|---|---|
| `run.py` | the runner: seeds, compiles, runs, compares, keeps failures |
| `difftest/types.py` | the primitive types and C's conversion rules |
| `difftest/nodes.py` | expression and statement trees, rendered as C4 or C |
| `difftest/generate.py` | the random program generator |
| `difftest/helpers.py` | the helper functions every program carries |
| `difftest/render.py` | a whole program as source: declarations, functions, `main` |
