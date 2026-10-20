# Built-in unit testing

C4 has unit testing built into the language: annotate functions with `[test("group.path")]` and build
with `-test`; the compiler generates an entry point that runs every test and returns the number of
failed assertions as the exit code. No framework include is needed.

Sources: test collection in `TestRegistry` (`C4Lib/src/Model/TestRegistry.cpp`), harness generation in
`C4Lib/src/Backend/CodeGen/TestHarnessEmitter.cpp`, assertions and reporting in `C4Runtime/TestRuntimeC.c`. End-to-end example: `assets/IsolatedTest/TestingFramework/TestAnnotationFramework/TestFlagTest.cf`.

---

## 1. Writing tests

```c4
int32 g_globalSetup = 0;
int32 g_counterValue = 0;

[test_setup]
void GlobalSetup() { g_globalSetup++; }

[test_teardown]
void GlobalTeardown() { assert_eq(1, g_globalSetup, "setup ran once"); }

[test("math.mul")]
void TestMul() {
	assert_eq(6, 2 * 3, "2 * 3 == 6");
}

struct Counter {
	[test_setup]
	public static void Setup() { g_counterValue = 100; }

	[test("counter.read")]
	public static void TestRead() {
		assert_eq(100, g_counterValue, "class setup ran first");
	}
}
```

| Rule | Status |
|---|---|
| `[test("group.path")]` on a free function at file scope marks it as a test. Dots are separators used for sorting and display. A bare `[test]` gets an empty group. | **Implemented** |
| `[test("...")]` on a method of a class/struct makes it a class test. Test methods (and class fixtures) must be `static`: the harness calls them without an object (`test methods must be static: the harness calls 'Setup' without an object; write 'public static'`). | **Implemented** |
| `[test("...")]` on a static extension method registers it under the extended type. | **Implemented** |
| Global fixtures: a free function tagged `[test_setup]` / `[test_teardown]` runs once before / after everything. A program has one of each (`'B' is a second [test_setup] here: 'A' is already one`), and so does each class. | **Implemented** |
| Class fixtures: a `static` method tagged `[test_setup]` / `[test_teardown]` brackets only that class's tests. | **Implemented** |
| Tests and fixtures take no parameters and return nothing (`a test takes no parameters, but 'F' takes 1`). | **Implemented** |
| Group paths compose across the owning type: `[test("math.vector")] class VectorTests { [test("addition")] static void TestAddition(); }` is the test `math.vector.addition`; a bare `[test]` method in that class is `math.vector.TestAddition`. A bare free `[test]` is named by its function, a bare method test outside a grouped class `Type.Function`. | **Implemented** |

Keep shared state in file-scope globals that the fixtures set up; tests should not depend on each
other's side effects.

---

## 2. Assertions

Assertions are compiler builtins (no include); pointer parameters are declared `void*`, so a string
literal binds directly. Each returns an `int32` status that you normally ignore;
the runtime counts passes and failures.

| Builtin | Meaning |
|---|---|
| `assert_true(int32 cond, void* msg)` | condition is non-zero |
| `assert_eq(int64 expected, int64 actual, void* msg)` | integer equality (narrower ints and bools convert) |
| `assert_feq(double expected, double actual, double tol, void* msg)` | float equality within `tol` |
| `assert_se(void* expected, void* actual, void* msg)` | C string equality |

A failure prints `FAIL: <file>:<line>: <msg>` with the assertion's own file and line (the compiler passes
the call site to the runtime's `c4_assert_*_at` forms); `assert_eq`, `assert_feq` and `assert_se` append
`(expected X, got Y)`.

Note: the test runtime is linked into **every** executable, so the assertions also link in normal
(non `-test`) builds. Older skills that say they only link under `-test` are wrong.

---

## 3. The `-test` build

`C4.exe <sources> -o tests.exe -test` changes two things:

1. The macro `TEST` is predefined (value 1), so tests and test-only helpers can be wrapped in
   `#ifdef TEST` and strip out of shipping builds.
2. A synthetic `main` replaces the program's own entry point (only when the program has at least one
   `[test]`; with none, the normal entry point is required). The program does not need a `main`;
   if it has one, it is used for normal builds and ignored under `-test`.

The generated harness runs, in order:

1. the global `[test_setup]`;
2. free tests, sorted by name;
3. for each class with tests, in alphabetical order of class name: the class `[test_setup]`, the
   class's tests sorted by name, the class `[test_teardown]`;
4. the global `[test_teardown]`;
5. a summary, then `return` the number of **failed assertions** (0 = all pass).

Each case prints `[TEST] <name>`, then `ok <name> (0.012 ms)` or `FAILED <name> (...)` with its wall-clock
time. A failed assertion prints `FAIL: <file>:<line>: <message>`, where file and line are the assertion's.
Each case runs inside a catch-all: an exception that leaves it prints `FAIL: <name>: an exception left the
test`, counts as a failed assertion, and the run goes on with the next case.

- `-test-json <path>` (a compiler option) makes the run write a JSON summary: the case and assertion
  counts and a `cases` array of `{ name, file, line, passed, milliseconds }`.
- `--filter <glob>` (an argument of the test executable) runs only the cases whose name matches: `*` is any
  run of characters, `?` one character (`tests.exe --filter "math.*"`). The fixtures still run.

C4.exe does not run the executable; you (or Nuke) run it and read the exit code.

---

## 4. Running tests with Nuke

A project carries its tests in an in-source `src/tests/` folder. A project with at least one `.cf` or
`.ch` under `src/tests/` (or `tests = true` in its `nuke.toml`) is test-bearing:

- `nuke build` / `nuke run` never pass `-test`: the files under `src/tests/` are part of the program and
  its `[test]` functions are inert code, so the program runs its own `main`.
- `nuke test [config] [--filter GLOB]` compiles each test-bearing project separately with
  `-test -test-json <int>/tests/results.json` into `build/bin/<Config>-windows-x64/<Project>/<Project>-tests.exe`
  (intermediates in `<int>/tests/`), runs it from the
  project folder (passing `--filter` on), runs every harness even after one fails, and exits 1 if any failed.

See [tools/02-nuke.md](../tools/02-nuke.md#3-tests).

---

## 5. Gaps

| Gap | Status |
|---|---|
| Parameterized tests, benchmarks. | **Open question** (ideas from old docs, not confirmed) |

---

## 6. Testing culture

The project's rule (AGENTS.md, LongTermPlan item 2): every behaviour gets tests, including edge cases,
error paths and feature interactions, with no duplicate tests; a failing test is fixed, never deleted
or weakened. See [tools/05-test-suites.md](../tools/05-test-suites.md) for the compiler's own suites.
