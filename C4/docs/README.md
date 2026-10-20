# C4 Documentation

C4 is a C++-like compiled language for game development. It keeps C++'s model of the machine (value
types, pointers, templates, zero-cost abstractions) and changes the parts that slow game teams down:
how sources are compiled, how constructors and operators are declared, and what ships with the
language (reflection, a garbage collector, built-in unit tests).

These docs describe **what C4 is and what it should be**. They are the reference to check new
features, bug fixes and library code against.

Start with [01-what-is-c4.md](01-what-is-c4.md).

---

## How to read these docs

Each language or runtime page has two layers:

1. **Normative text** - how the feature is meant to work. This is the design to build toward.
2. **A status line** on every section, plus a "Current implementation" note wherever the compiler
   differs from the design today.

| Status | Meaning |
|---|---|
| **Implemented** | Works end to end and is covered by a test suite. |
| **Partial** | Some of it works; the gaps are listed next to the status. |
| **Planned** | Part of the intended language/toolchain, not built yet. |
| **Open question** | The design is not settled. The question is stated so it can be decided. |

When the normative text and the implementation disagree, it is either a bug (listed in
[status/known-bugs.md](status/known-bugs.md)) or an open question (listed in
[status/roadmap.md](status/roadmap.md)). Nothing in these docs should claim a limitation that has not
been checked against the compiler.

---

## Map

### Overview
| Page | Contents |
|---|---|
| [01-what-is-c4.md](01-what-is-c4.md) | Identity, goals, principles, C4 vs C++ at a glance, a short tour |

### Language reference - `language/`
| Page | Contents |
|---|---|
| [01-program-model.md](language/01-program-model.md) | `.cf` / `.ch` files, `include`, whole-program compilation, visibility, entry point, namespaces |
| [02-preprocessor.md](language/02-preprocessor.md) | `#define`, function-like and variadic macros, `#if` family, macro state through includes |
| [03-lexical-structure.md](language/03-lexical-structure.md) | Comments, keywords, operators, literals and escapes |
| [04-types.md](language/04-types.md) | Primitives, type-side declarators, const, pointers, references, arrays, `funcptr`, `alias`, enums, conversions |
| [05-objects.md](language/05-objects.md) | `class` / `struct` / `interface` / `union`, layout, visibility, inheritance, virtuals, extension methods |
| [06-lifecycle-and-operators.md](language/06-lifecycle-and-operators.md) | `[operator(...)]`: constructors, destructors, operator overloading, custom operators, iteration |
| [07-functions-and-lambdas.md](language/07-functions-and-lambdas.md) | Functions, overloading, default arguments, variadics, function pointers, lambdas |
| [08-templates.md](language/08-templates.md) | Class, function and alias templates, instantiation rules |
| [09-statements-and-control-flow.md](language/09-statements-and-control-flow.md) | Control flow, range-for, `switch` / `fallthrough`, `goto`, `defer`, exceptions |
| [10-expressions.md](language/10-expressions.md) | Operator precedence, expression forms, casts, `sizeof`, `new` / `delete`, initializers |
| [11-vectors-and-swizzling.md](language/11-vectors-and-swizzling.md) | `packed` / `unpacked` SIMD lane views and HLSL-style swizzles |
| [12-annotations.md](language/12-annotations.md) | `[name(args)]` syntax and the annotations the compiler acts on |
| [13-interop.md](language/13-interop.md) | `extern "c"` / `extern "c++"`, CBind, `[link]`, COM |
| [14-builtins.md](language/14-builtins.md) | Functions available without an include |
| [15-projects.md](language/15-projects.md) | Several projects in one build: their defines, includes and boundaries |

### Runtime - `runtime/`
| Page | Contents |
|---|---|
| [01-memory-and-gc.md](runtime/01-memory-and-gc.md) | Memory model, the garbage collector, engine hooks |
| [02-reflection.md](runtime/02-reflection.md) | The reflection registry and the Bedrock `Reflection` API |
| [03-testing.md](runtime/03-testing.md) | `[test]` unit testing, assertions, the generated test harness |
| [04-scripting.md](runtime/04-scripting.md) | Scripts: compiling C4 in a running program against its interface, linking with ORCv2, reload, crash isolation |

### Default library - `library/`
| Page | Contents |
|---|---|
| [bedrock.md](library/bedrock.md) | Bedrock: containers, strings, formatting, file IO, serialization, DOM |

### Tools - `tools/`
| Page | Contents |
|---|---|
| [01-compiler-cli.md](tools/01-compiler-cli.md) | `C4.exe` command line |
| [02-nuke.md](tools/02-nuke.md) | Building C4 projects with Nuke: the C4.exe command line, rebuild rules, tests, the editor bridge, versioned toolchains |
| [03-editor-lsp-dap.md](tools/03-editor-lsp-dap.md) | Editor integration (IED), C4LSP, C4DAP |
| [04-building-the-compiler.md](tools/04-building-the-compiler.md) | Building C4 itself: `setup.bat`, `nuke build` or Frost, toolchains from the build, build traps |
| [05-test-suites.md](tools/05-test-suites.md) | Every test suite, how to run it (`nuke gates` runs them all), how to add tests |

### Compiler internals - `compiler/`
| Page | Contents |
|---|---|
| [README.md](compiler/README.md) | Architecture rules, source tree map, legacy doc-number references |
| [01-pipeline.md](compiler/01-pipeline.md) | End-to-end compilation pipeline |
| [02-program-model.md](compiler/02-program-model.md) | The ProgramModel and its query API |
| [03-hir.md](compiler/03-hir.md) | HIR and body lowering |
| [04-codegen-and-abi.md](compiler/04-codegen-and-abi.md) | LLVM code generation, Windows x64 ABI, exceptions, GC emission |

### Status - `status/`
| Page | Contents |
|---|---|
| [implementation-status.md](status/implementation-status.md) | Feature-by-feature status, code-health items, stale documents elsewhere in the repo |
| [known-bugs.md](status/known-bugs.md) | Every open bug, one entry each, added when found and removed when fixed |
| [roadmap.md](status/roadmap.md) | Long-term plan and the record of decided design questions |
| [plan.md](status/plan.md) | The implementation plan: every design decision, and every work package with steps, files and tests |

---

## Suggested reading paths

- **New to C4:** [01-what-is-c4](01-what-is-c4.md) -> [program model](language/01-program-model.md) ->
  [types](language/04-types.md) -> [objects](language/05-objects.md) ->
  [lifecycle and operators](language/06-lifecycle-and-operators.md) -> [Bedrock](library/bedrock.md).
- **Building a C4 project:** [Nuke](tools/02-nuke.md), [testing](runtime/03-testing.md),
  [editor](tools/03-editor-lsp-dap.md).
- **Working on the compiler:** [compiler/README](compiler/README.md),
  [building the compiler](tools/04-building-the-compiler.md), [test suites](tools/05-test-suites.md),
  [implementation status](status/implementation-status.md), [known bugs](status/known-bugs.md).
- **Deciding what to build next:** [plan](status/plan.md), then [roadmap](status/roadmap.md).

---

## Keeping the docs current

- A change to the language, runtime, Bedrock or tools updates the page that owns that topic in the
  same change, including its status line.
- A newly found bug gets an entry in [status/known-bugs.md](status/known-bugs.md). Fixing a bug or a
  limitation removes its entry there and the
  "Current implementation" note on the feature's page.
- Deciding an open question moves it out of [status/roadmap.md](status/roadmap.md) and into the
  normative text.
- Examples use only syntax the compiler accepts today, unless the section is marked **Planned**.
- ASCII only (no em dashes, curly quotes or arrows), like the rest of the repo.
- Code comments that point at docs use the page paths above. The old numbered architecture docs
  ("doc 05", "doc 09") are mapped in [compiler/README.md](compiler/README.md).

## Other documents in the repo

| File | Role |
|---|---|
| `AGENTS.md` | Standards for anyone (human or AI) changing the code: stability mandate, code cleanliness, comments |
| `WORK.md` | Tracker for active work |
| `LongTermPlan.txt` | The original long-term plan; its items are tracked in [status/roadmap.md](status/roadmap.md) |
| `skills/*/SKILL.md` | Short agent-facing summaries of the language, shipped in every toolchain; these docs are the full reference |
| `Frost/docs/` | Documentation for Frost (the build system for the C4 compiler and its C++ support libraries) |
