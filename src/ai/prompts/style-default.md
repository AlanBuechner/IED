# Code style (owner defaults - project AGENTS.md rules take precedence where they are more specific)

Formatting
- Indent with tabs, never spaces (unless the file already uses spaces: then match it and mention it). Use fix_indentation if a file is mixed.
- Match the surrounding code's naming, structure and idiom.

Functions and types
- Single purpose: a function does one thing, a type models one concept. If its name needs "and", split it.
- One level of abstraction per function: every line in a body works at the same level. A loop with a non-trivial body, or a commented "section" inside a function, is a missing function.
- Honest functions: touch the outside world only through parameters (including `this`). Keep I/O, clocks, globals and environment at the top level (main, handlers, callbacks) and keep the core logic honest and testable.
- No global mutable state; pass what a function needs.
- Signatures are for the caller: group many parameters or mysterious booleans into a small named struct; encode invariants in types; if B requires A first, make A return something B takes.
- Write the small functions you wish existed, then implement them.

Files and structure
- No bloated files: organize by subsystem; when a file grows too large, split it by concern.
- No dead code, no unused parameters or flags, no speculative abstractions (two real uses before a pattern earns an interface).
- Name things for what they mean in this codebase. No magic numbers or strings: use named constants or enums.

Comments
- Minimal. Code explains itself through names and structure. A comment says *why*, in one short line, only when the code cannot.
- No doc-comment blocks on every function. Section dividers are fine for grouping at file level.

Errors
- Explicit and handled where something can be done about them. Never swallow errors, silently default, or abort from library code.

Tests
- Every behavior gets tests, including edge cases and error paths. Tests are isolated (no order dependence, no shared mutable fixtures).
- A failing test is fixed, never deleted, skipped or weakened.

Performance
- Clean code must not cost speed: avoid needless copies of large objects and needless intermediate structures; prefer purpose-built solutions where they matter.

Working rules
- Refactor what you touch when it violates these rules.
- Fix every bug you find, pre-existing or not, and keep docs and skills in sync with actual behavior.
- The build and the full relevant test suite must pass before work is considered done.
