# Known bugs

Every open bug in the C4 compiler, runtime, Bedrock and tools. Add an entry when a bug is found;
delete it when the fix lands.

Next ID: **BUG-365**

## Rules for this file

- **One entry per bug.** If one root cause has several symptoms, list them in one entry.
- **IDs are permanent.** Take the "Next ID" above and bump it. Never renumber or reuse an ID, so
  commits, tests and code comments can refer to `BUG-012` safely.
- **Remove the entry when it is fixed**, in the same change as the fix. The fix must come with a test
  that would have caught it (see [../tools/05-test-suites.md](../tools/05-test-suites.md)). Also remove
  the matching "Current implementation" note on the feature's page. The commit message keeps the history.
- **Pre-existing is not a reason to keep a bug.** Per `AGENTS.md`, every known bug gets fixed.
- **Status** is `Open` (confirmed by a repro or by reading the code) or `Needs re-test` (reported
  earlier, may already be fixed; re-test, then fix or delete).
- **Severity:**
  - **High** - miscompiles, memory unsafety, compiler crash, or valid programs that cannot be written.
  - **Medium** - wrong behaviour with a workaround, valid code rejected, a language rule not enforced.
  - **Low** - diagnostics quality, performance, tooling polish.
- Design questions that are not bugs go to [roadmap.md](roadmap.md). Unbuilt features go to the
  roadmap backlog, not here.

### Entry template

```text
### BUG-NNN Short title
- **Area:** Parser | Model | HIR | Codegen | Preprocessor | Runtime | Bedrock | Tools | Diagnostics | Tests
- **Severity:** High | Medium | Low
- **Status:** Open | Needs re-test
- **Found:** YYYY-MM-DD
- **Symptom:** what goes wrong, from the user's point of view
- **Repro:** smallest program or command (optional)
- **Where:** files / functions involved
- **Notes:** fix direction, related bugs, constraints (optional)
```

---

## Index

| ID | Title | Area | Severity |
|---|---|---|---|
| BUG-358 | printf and Core::Format take a String as a C string and print garbage | Model | Medium |
| BUG-359 | Core::Format cuts an int64 under `{n}` to 32 bits without a word | Bedrock | Medium |
| BUG-360 | No way to read standard input | Bedrock | High |
| BUG-361 | C++ forms and STL names get diagnostics that do not name the C4 form | Diagnostics | Low |
| BUG-362 | A `const T&` field cannot be bound in its object's create | HIR | Medium |
| BUG-363 | Each struct returned from a call takes its own stack slot, so recursive functions overflow early | Codegen | Medium |
| BUG-364 | HashMap::FindAs has no const form | Bedrock | Low |

---

## Parser, lexer and preprocessor

## Model, templates and semantics

### BUG-358 printf and Core::Format take a String as a C string and print garbage
- **Area:** Model
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** an object (Core::String, Core::StringView, an Array) passed to a C variadic `...` parameter compiles and
  is read as a `const char*`: `printf("%s", name)` and `Core::Format(&b, "{0:s}", name)` with a `Core::String name`
  print garbage. Models trained on C++ (where `std::string` in printf is a compile warning) write it constantly
  (IED's language eval, tools/ai-evals/lang).
- **Repro:** `Core::String name = Core::String("ada"); printf("[%s]", name);` prints bytes of the String object.
- **Notes:** reject (or warn on) a non-trivially-copyable object as a variadic argument, naming `.CStr()`; a type-safe
  Format (variadic templates exist) would remove BUG-359 too.

## HIR and codegen

### BUG-363 Each struct returned from a call takes its own stack slot, so recursive functions overflow early
- **Area:** Codegen
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** every `return f(...)` of a struct goes through an sret temporary of its own that is then copied into
  the caller's return slot, and no `llvm.lifetime` markers are emitted, so temporaries of call sites that never run
  together never share a slot even at -O3: a function returning an 80-byte struct from each of eight switch cases has
  a 704-byte frame (144 with one case). A tree-walking interpreter written in C4 (IED's language eval) overflows the
  1 MiB stack at about 209 nested script calls where the same program in Go reaches 100000.
- **Repro:** `struct Value { int32 kind; int64 integer; Core::String text; int64* a; int64* b; int64* c; }` and a
  recursive `static Value F(int64 n, int32 which)` whose switch has eight `case k: return F(n - 1, which - k);`;
  the distance between `&local` at two depths is the frame size (one case: 144 bytes).
- **Notes:** return into the caller's slot directly (copy elision for `return f(...)`), and emit lifetime markers
  for temporaries and locals so LLVM's stack coloring can share slots.

### BUG-362 A `const T&` field cannot be bound in its object's create
- **Area:** HIR
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** a reference field to const is bound by an assignment in its object's create, as language/04-types.md
  4.4 allows, but the compiler reads the binding as a write through the const reference:
  `error: cannot write through this location: it is reached through a pointer or reference to const`. The same code
  with an `int32&` field builds and runs. A C++ class holding `const std::vector<T>& lines_` (IED's language eval
  ports one) cannot be written the same way in C4.
- **Repro:** `class Reader { [operator("create")] public void Init(const int32& source) { this.value = source; }
  public int32 Get() const { return value; } private const int32& value; }`, then `int32 n = 42; Reader r = Reader(n);`.
- **Where:** `C4/C4Lib/src/Backend/HIR/ReadOnly.cpp` (`ReadOnlyReason`) likely, where the binding assignment is
  classified.

## Interop, runtime and Bedrock

### BUG-364 HashMap::FindAs has no const form
- **Area:** Bedrock
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** `const Core::HashMap<Core::String, int64>& map; map.FindAs(name)` does not compile: `Find` has a const
  twin returning `const V*` and `FindAs` has none, against "Queries are const (D-31)" (library/bedrock.md section 2),
  so a lookup by view in a const method has to drop the const.
- **Repro:** `static bool Has(const Core::HashMap<Core::String, int64>& map, Core::StringView name) { return map.FindAs(name) != null; }`.

### BUG-359 Core::Format cuts an int64 under `{n}` to 32 bits without a word
- **Area:** Bedrock
- **Severity:** Medium
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** `Core::Format(&b, "{0}", big)` with `int64 big = 5000000000` writes 705032704: `{n}` reads an int32 and
  nothing checks the arguments against the placeholders (`{0:D}` is right). Counts in Bedrock are int64, so the
  default placeholder is the wrong one for most numbers a program formats.
- **Notes:** check placeholders against argument types (a variadic-template Format), or make `{n}` format any integer.

### BUG-360 No way to read standard input
- **Area:** Bedrock
- **Severity:** High
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** Bedrock writes standard output and error (`Platform::WriteOutput`, `WriteError`) but has nothing that
  reads standard input, and the prelude's `file_open` cannot open it: a filter (`tool < input`, a pipe) cannot be
  written in C4.
- **Notes:** e.g. `Platform::ReadInput(buffer, capacity)` / `Platform::ReadAllInput()` and an `FS::File` on stdin.

## Tools

## Diagnostics and tests

### BUG-361 C++ forms and STL names get diagnostics that do not name the C4 form
- **Area:** Diagnostics
- **Severity:** Low
- **Status:** Open
- **Found:** 2026-10-22
- **Symptom:** the forms a C++ programmer (or a model trained on C++) writes first are reported without the C4 way:
  `a.push_back(1)`, `s.size()`, `m.insert(k, v)` on Bedrock types say "no matching overload of push_back" (there is
  no such member at all; Push, Count, Insert exist); `auto x = 1;` says "type not found: auto"; `int values[4];` and
  `char buf[16];` say "Unexpected token 'int'"; a C++ constructor `Point(int32 px) { ... }` in a class body gives ten
  "Unexpected token" errors; `std::string` says "type not found: std". IED's language eval (tools/ai-evals/lang)
  shows these as the usual first errors of a local model writing C4.
- **Notes:** "has no member push_back; did you mean Push?" (edit distance over the type's members plus the common STL
  names: push_back->Push, size->Count, empty->IsEmpty, c_str->CStr, substr->Substring, find->Find, insert->Insert);
  "C4 has no auto: write the type"; "array extents belong to the type: int32[4] values"; "constructors are methods
  marked [operator("create")]"; "there is no std: Bedrock's Core::String". Also: inside one function body only
  the first error is reported, so a model fixes one error per build.

