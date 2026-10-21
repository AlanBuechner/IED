---
name: c4-testing
description: C4 built-in unit testing - [test("group")] annotations on static functions, global and per-class [test_setup]/[test_teardown] fixtures, the assert_true/assert_eq/assert_feq/assert_se builtins (linked into every build), how nuke test detects src/tests/ folders and builds a separate -test harness that runs every test, ordering rules, exit codes, and known limits.
triggers:
  - "C4 unit tests"
  - "[test] annotation group path"
  - "assert_eq assert_true assertions"
  - "src/tests folder nuke test"
  - "test_setup test_teardown fixtures"
  - "generated harness main exit code"
---

# C4 testing (deep dive)

C4 has built-in unit testing: tag a static function with `[test("group.path")]` and the compiler can generate an entry point that runs every tagged function. No framework include is needed; assertions are builtins. Full reference: `docs/runtime/03-testing.md` in the toolchain.

## Writing tests
    [test("math.vector.addition")]
    public static void TestAddition() {
        assert_eq(2, 1 + 1, "addition works");   // (expected, actual, message)
    }

- A test function must be `static` - free functions at file scope or static methods of a class. The group is the string in `[test("...")]`; dots are just separators used for sorting/display, e.g. `"math.vector.addition"`.
- **Group paths:** dots are separators used for sorting/display, e.g. `"math.vector.addition"`. A `[test("group")]` annotation on a class prefixes every test method inside it (`[test("math.vector")]` + member `[test("addition")]` = `math.vector.addition`; a bare `[test]` member is `math.vector.<Function>`).
- Fixtures are plain functions with their own annotations; they take no arguments and return nothing:
    - Global fixtures (run once, around everything): free `static` functions tagged `[test_setup]` / `[test_teardown]`, or the same tags on a class's static methods.
    - Per-class fixture: tag a static method of the test class; it brackets only that class's tests.

## Assertions
Builtins (C4Runtime/TestRuntimeC.c). The test runtime is linked into **every** executable, so the assertions also link in normal (non-`-test`) builds:
- `assert_true(cond, msg)` - boolean/int condition.
- `assert_eq(expected, actual, msg)` - integer comparison (int64; narrower ints/bools convert).
- `assert_feq(expected, actual, tolerance, msg)` - float/double with explicit tolerance.
- `assert_se(expected, actual, msg)` - string (char*) comparison.

Each returns an int32 status you normally ignore; the harness counts failures itself. String messages are literals that bind to the universal pointer parameter.

## How Nuke runs them
Tests live in an **in-source** `src/tests/` folder of a project (no separate test projects). A project with at least one `.cf`/`.ch` under `src/tests/` is "test-bearing":
- `nuke build` / `nuke run` (never `-test`), or a compile without `-test`: the `[test]` functions are inert code; the project builds as usual.
- `nuke test [config] [--filter "math.*"]`: Nuke compiles each test-bearing project separately with `-test -test-json` into `build/bin/<Config>-windows-x64/<Project>/<Project>-tests.exe`, runs every harness (even after one fails) and exits 1 if any failed.

Under `-test`, a generated harness replaces the entry point: it calls global setup, then free tests sorted by group path, then each class's tests (classes alphabetically, methods by group path) bracketed by that class's static fixtures, then global teardown, prints a summary via the test runtime, and returns the **failed-assert count as the process exit code** (0 = all pass). If a project has no `main` of its own it compiles purely to this harness; if it also defines `int32 main()` that one is used for normal builds while `-test` still swaps in the harness.

## Notes and limits
- Test annotations are read on function declarations (and, for group prefixing, on the owning class). Test methods and fixtures must be `static`, take no parameters and return `void` (each is a clear error otherwise); a program (and each class) has one setup and one teardown.
- Output: `[TEST] name`, then `ok name (0.012 ms)` / `FAILED name`; a failed assertion prints `FAIL: file.cf:12: message`. `-test-json <path>` (compiler) writes a JSON summary; `tests.exe --filter "math.*"` runs only matching cases.
- Keep test files ordinary `.cf`/`.ch`; there is no special file naming requirement beyond living under `src/tests/`.
- For manual runs: compile with `C4.exe -o out.exe -test <sources...>` and run the exe; exit code 0 means green.
