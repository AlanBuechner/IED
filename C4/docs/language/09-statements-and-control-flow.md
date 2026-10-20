# 09 - Statements and control flow

C4 statements are C++ statements, plus `defer`, an explicit `fallthrough;`, and a `finally`
clause on `try`. Anything not listed here as a C4 change behaves as in C++.

See also: [10 - Expressions](10-expressions.md),
[06 - Lifecycle and operators](06-lifecycle-and-operators.md) (scope-exit destruction and the
iterator operators), [compiler/03-hir.md](../compiler/03-hir.md) (how statements lower).

---

## 1. Blocks and local declarations

**Status:** **Implemented**

- `{ ... }` opens a scope. Locals are visible from their declaration to the end of the block.
  Using a local outside its block, a braceless `if`/`for` body, or after a `for` header is an
  error (`no variable named x`).
- Redeclaring a name in the same block is an error
  (`Variable 'x' is already declared in this block`). Shadowing in an inner block is allowed.
- `;` on its own is an empty statement.
- Function bodies may declare local types (`struct`, `class`, `enum`, `union`), local aliases and
  local functions. Inside a function body, declarations register in order, so a local type must
  be declared before use (unlike file scope - see [01 - Program model](01-program-model.md)).

```c4
void F() {
	struct Pair { int32 a, b; };
	alias Count = int32;
	Pair p = { 1, 2 };
	Count n = p.a + p.b;
}
```

Local functions are lowered as ordinary functions; they do not capture the enclosing function's
locals. **Partial**: covered by compiler unit tests, no end-to-end program yet.

---

## 2. Conditionals and loops

**Status:** **Implemented**

```c4
if (x > 0) { y = 1; } else if (x < 0) { y = -1; } else y = 0;

while (i < 10) { i++; }

do { i--; } while (i > 0);

for (int32 i = 0; i < 10; i++) { sum += i; }
for (;;) { break; }
for (; lo < hi; ++lo, --hi) { ... }      // a step of several expressions
```

- A condition (of `if`, a loop, `?:`, or an operand of `&&` / `||`) is a `bool`, a number, an enum or a
  pointer: non-zero / non-null is true. A reference is its referent. Anything else is an error that names
  the type (`a 'Point' has no truth value: a condition is a bool, a number, an enum or a pointer`); a
  `packed bool[N]` mask has N answers, so test one lane. An object with `[operator("logical_and")]` or
  `[operator("logical_or")]` is an operand of `&&` / `||` through that operator.
- Each `for` clause is optional. A variable declared in the init clause is scoped to the loop.
- The step clause is a list of expressions separated by commas, run left to right after each iteration,
  each a full expression of its own (D-40). There is no comma operator anywhere else.
- `break` exits the innermost loop or `switch`; `continue` jumps to the next iteration (running
  the `for` step). `break`/`continue` outside a loop is an error
  (`'break' with no enclosing loop or switch`). There are no labeled `break`/`continue`.

There is no `loop` statement; `loop` is an ordinary identifier (D-04). Write `while (true)` or
`for (;;)` for an endless loop.

---

## 3. Range-based for

**Status:** **Implemented** (arrays, init lists, iterator protocol, reference loop variables)

```c4
int32[5] arr;
int32 sum = 0;
for (int32 v : arr) { sum += v; }
```

The loop variable must be written with its type (C4 has no `auto`). A value loop variable is a copy of
the element each iteration, so assigning to it does not modify the range. A reference loop variable (D-54)
is the element itself:

```c4
for (int32& v : arr) v = v * 10;        // writes each element
for (const int32& v : arr) sum += v;    // reads without a copy; works over a read-only range
```

- The element must be a location: an array or init-list element, or what an iterator's `dereference` names
  when it returns `T&`. A by-value `dereference` gives copies, so `T& v` over it is an error that says so.
- `T&` names elements of type `T` (`'v' is a 'int64&', but the elements are 'int32'`), and a read-only range
  needs `const T&` (`the elements are read-only: declare 'const int32& v'`).

Ranges that work:

| Range expression | How it iterates |
|---|---|
| Fixed-size array `T[N]` | index `0 .. N-1` |
| `__init_list<T>` | its data pointer and size |
| Any type with the iterator protocol | see below |
| A plain pointer | error: `cannot range-for over this pointer` (no length) |

### 3.1 Iterator protocol

A type is iterable when it provides `[operator("begin")]` and `[operator("end")]` returning an
iterator type, and the iterator provides `notequal`, `dereference` and `increment`. These are
ordinary [operator annotations](06-lifecycle-and-operators.md); there is no `begin()`/`end()`
naming convention as in C++.

```c4
struct Collection { int32 count; };
struct CollectionIterator { int32 index; };

[operator("begin")] CollectionIterator Begin() extends Collection {
	CollectionIterator it; it.index = 0; return it;
}
[operator("end")] CollectionIterator End() extends Collection {
	CollectionIterator it; it.index = this.count; return it;
}
[operator("notequal")] int32 NotEqual(const CollectionIterator& other) extends CollectionIterator {
	return (this.index != other.index) ? 1 : 0;
}
[operator("dereference")] int32 Dereference() extends CollectionIterator {
	return g_data[this.index];
}
[operator("increment")] CollectionIterator Increment() extends CollectionIterator {
	CollectionIterator it; it.index = this.index + 1; return it;
}

// for (int32 v : col) { ... }
```

The loop desugars to:

```text
it = begin(range); end = end(range);
while (it notequal end) { v = dereference(it); body; it = increment(it); }
```

`continue` runs the increment step; `break` skips it. Note that the result of `increment` is
assigned back to the iterator, so `increment` may either return a new iterator or mutate `this`
and return it. A missing operator body is reported as `iterator protocol: 'begin' has no body`.

Planned: `for (T& v : range)` reference loop variables that alias the element (**Planned** - not
covered by tests today).

---

## 4. switch

**Status:** **Implemented**

```c4
switch (x) {
case 1:
	r = 10;
	fallthrough;      // explicit: continue into the next case
case 2:
	r = r + 20;
	break;
case Color::Red:      // scoped enum labels are written qualified
	r = 5;
	break;
default:
	r = 0;
	break;
}
```

- The switch value is an integer, a char, a bool or an enum (`a switch selects on an integer, a char or an
  enum, not a 'float'`).
- Case labels are compile-time constants (literals, const variables, enum members) that the switch value's
  type holds exactly: `case 300:` on a `uint8` and `case -1:` on a `uint32` are errors (`case value 300 does
  not fit in the switch's 'uint8'`), and so is a floating-point label. Each value is a case once, and a switch
  has one `default`.
- A switch on an enum class takes that enum's members; an integer label is an error, and so is an enum
  class member on a switch of another type (D-32).
- A case may not run into the next one by accident (D-39): a case with statements whose end can be
  reached is the error `this case falls into the next one: end it with 'break;' or 'fallthrough;'`. A case
  ends with `break`, `return`, `throw`, `continue`, `goto`, `fallthrough;`, or a statement that cannot
  complete (an `if` whose branches both end, an endless loop with no `break`, ...). The last case may run
  off its end; that leaves the switch.
- Stacked labels with no statements of their own share the body after them: `case 1: case 2: ...`.
- `fallthrough;` jumps into the next case. It must be the last statement of its case, the last case has no
  case to continue into, and it is an error outside a case
  (`'fallthrough' is only valid inside a switch case`).

---

## 5. goto and labels

**Status:** **Implemented**

```c4
	goto skip;
	value = 1;
skip:
	value = 2;
```

Labels are function-scoped and must be unique in the function. The rules (C++ rules, made strict
because every C4 declaration initializes):

- A `goto` may target a label in its own block or in any enclosing block. Jumping into a nested block
  is an error: `goto 'x' jumps into a nested block`.
- A forward jump may not cross a declaration or a `defer` of the label's block:
  `jump to label 'x' crosses the declaration of 'y'` / `... crosses a defer`. Static locals do not count.
- A backward jump, or any jump out of a block, runs the defers and destructors of everything it leaves,
  exactly like `break`.
- Falling through into a label continues at the label.
- A `goto` to a label that does not exist is an error, and so is a `goto` or a label inside a `defer` or
  `finally` body.

---

## 6. return

**Status:** **Implemented**

- `return;` in a `void` function, `return expr;` otherwise; `return { ... };` returns a braced
  aggregate of the declared return type. A `void` function may `return F();` when `F` returns void too; any
  other value is an error (`'Log' returns void, so 'return' cannot give it a value of type 'int32'`).
- A missing return value or a value of the wrong type is an error (`function returns 'S'`).
- Before control leaves the function, pending `defer` statements and scope-exit destructors run
  (innermost first), and enclosing `finally` blocks run.
- `main` may return `void` (exit code 0) or `int32`.

---

## 7. defer

**Status:** **Implemented**

`defer stmt;` or `defer { ... }` schedules a statement to run when the enclosing scope exits.

```c4
int32 DeferWithReturn(int32 cond) {
	defer DeferBump();
	if (cond) return 42;   // DeferBump runs before the return
	return 7;              // ...and here
}

void LifoScope() {
	defer DeferTag(1);
	defer DeferTag(2);
	defer DeferTag(3);     // runs first: 3, 2, 1
}
```

Rules (C++ RAII semantics):

- Runs on every exit edge of its scope: falling off the end, `return`, `break`, `continue`,
  `fallthrough`, `goto` out of the scope, and an exception propagating through the scope (including one
  thrown by a callee).
- A `defer` that has not been reached yet when the scope exits does not run.
- LIFO within a scope; inner scopes' defers run before outer scopes'. A `throw` caught inside the same
  function runs only the defers of the scopes it actually leaves.
- Scope-exit destructors of value locals (`[operator("delete")]`, see
  [06 - Lifecycle and operators](06-lifecycle-and-operators.md)) use the same mechanism and
  interleave with `defer` in reverse declaration order. A local declared in a `for` init clause lives
  until the loop ends.
- The deferred statement sees the variables in scope at the `defer` point, evaluated when it runs
  (not when it is scheduled). A `return` value is computed before the defers run.
- Control may not leave a defer body: `return`, `goto`, labels, and a `break` / `continue` whose loop is
  outside the body are errors. A `throw` is allowed; one that escapes a defer while an exception is
  already unwinding terminates the program, as a throwing destructor does in C++.

---

## 8. Exceptions

**Status:** **Implemented**

```c4
void DoThrow(int32 v) { throw v; }

int32 F() {
	int32 r = -1;
	try {
		DoThrow(21);
	} catch (int32 e) {
		r = e;
	} catch (float f) {
		r = 0;
	} finally {
		Cleanup();
	}
	return r;
}
```

| Form | Meaning |
|---|---|
| `throw expr;` | throw a value: a temporary moves into the exception object, a variable is copied into it |
| `throw;` | rethrow the exception currently being handled (only inside a `catch`) |
| `try { } catch (T name) { }` | catch a copy (the type's copy operator) of a `T` or of the `T` sub-object of anything deriving from it, destroyed when the handler ends; the name is optional. A type that cannot be copied must be caught by reference |
| `catch (T& name)`, `catch (const T& name)` | the exception object itself (or its `T` sub-object): changes are visible to an outer handler after `throw;` |
| `catch (T* name)` | a thrown pointer to `T` or to anything deriving from it, adjusted to the `T` sub-object; `catch (void* p)` takes any thrown pointer |
| `catch (...)` | any exception, of any type, from C4 or C++ |
| multiple `catch` clauses | tried in order, first match wins |
| `finally { }` | runs however control leaves the try statement: normal completion, after a handler, `return`, `break`, `continue`, `goto`, and while an exception unwinds out of it |

Behavior:

- Nested `try` blocks work; an exception not matched by an inner handler propagates outward.
  `throw` inside a `catch` or `finally` propagates to the enclosing handler.
- Pending `defer` statements and value-local destructors run while unwinding (section 7).
- `finally` is exactly a `defer` that covers the try statement and its handlers:
  `try { A } catch (E e) { B } finally { F }` behaves as `{ defer { F } try { A } catch (E e) { B } }`.
  The defer-body rules apply to `F` (no `return`, no jumps out of it).
- **Uncaught exceptions** terminate the process: the runtime prints
  `C4: fatal: an exception was thrown and not caught` and exits with code 1. No code after the
  throw runs. Under a debugger, Windows hands the exception to the debugger instead (it stops there,
  with the throw's stack), and the program then ends with the exception's code.
- The exception object is destroyed (its `delete`) once the last handler is done with it, also when
  the handler leaves by `return`, `break` or another throw.
- **`noexcept`** (`void F() noexcept { }`, D-13): an exception that would leave the function stops the
  program instead - the runtime prints `C4: fatal: an exception left a noexcept function` and exits with
  code 1 - and calls to a `noexcept` function need no unwind path.
- The implementation uses native Windows x64 C++ exception handling (funclets,
  `__CxxFrameHandler3`); see [compiler/04-codegen-and-abi.md](../compiler/04-codegen-and-abi.md).

- Matching follows C++: a clause matches the thrown type itself or any unambiguous base or interface,
  for objects and for object pointers. The type descriptors carry MSVC decorated names
  ([compiler/04](../compiler/04-codegen-and-abi.md#exceptions)), so a C++ exception is caught by the C4
  clause for the same type (`catch (int32 e)` catches a C++ `throw 7;`, a CBind-bound `CppError` catches
  a C++ `throw CppError{...}`), and a C++ handler catches a C4 throw the same way.

Current limits:

| Item | Status |
|---|---|
| Template instance types in a C++ catch clause (MSVC back-referenced names are not reproduced) | **Partial** - C4 to C4 matching is exact |
| Linux / non-MSVC unwinding | **Planned** with Linux support |

---

## 9. Expression statements

Any expression followed by `;` is a statement. An assignment statement parses exactly like an
initializer: `x = a && b;` assigns `a && b`
([10 - Expressions](10-expressions.md#1-precedence-normative)).
