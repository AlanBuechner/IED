# C4

## What is C4
C4 is a mostly c++ like language that has some syntax changes and extra features. beyond the simple syntax changes this language should work just like c++

### Changes

1. the language follows a program model system where all source files are built together rather then separately then linked together. this comes withe some changes to keep in mind like
    - Includes are no longer part of the pre processor though includes do still carry pre processor information from the header for use in what ever file includes it and so on just like in c/c++.
    - Because includes are not part of the pre processor each file that includes a header will have the exact same header as all the other files so defining something before including a header will NOT affect how that header is parsed.
    - Forward definitions for types are not needed unless you are in a function body then they will get registered in order so forward declarations may be needed but out side of a function they are not.
    - Because each file including the headers are source files them selfs you don't need to forward declare a static variable then define it in a source file. when you create a static variable in a header the variable is already created in the header because the header IS a source file.
    - functions will still need forward declarations if they have there implementations in a source file. that way when a file includes the header it will know that that function exists even if the implementation is elsewhere.
    - templated functions don't need there implementations in the header anymore they can be moved into a source file just like with all the other functions.
2. arrays and other type specifiers are part of the type no the variable. some examples of this are
    - const int arr[10];(c/c++) -> const int[10] arr;(c4) // this is an array of const int's
    - const int const*[5] arr; // this is an array of const pointers to const int's
    - int const* i; // this is a const pointer to a mutable int.
    - const int* i; // this is a mutable pointer to a const int.
    - int& i; // this is a reference to a mutable int just like in c++
3. probably the largest syntax change is takeing constructors/destructors and operators and making them a single concept using reflection annotations on normal functions
    - class Type { [operator("create")] void Init(); [operator("delete")] void Destroy(); } // this is a default constructor and a destructor
    - class Type { [operator("add")] void Add(const Type& other); } // add operator
    - class Type { [operator("+")] void Add(const Type& other); } // also an add operator as we can use either the symbols or the names like "add_assign" or "+="
    - you can have multiple operators on a single function like [operator("=", "copy")] as long as they have matching signatures.
    - "negate" and "subtract" are strict in what they are but "-" can be either depending on the signatures
    - you can call operators generically without needing to know the name of the function they are attached to with v.operator_add(v2); or v->operator_add() if v is a pointer
    - we support custom operators like [operator("hash")] v.operator_hash(); this is not a built in operator but it is useful so libraries like the default library can interact with custom objects easily without needing to enforce a naming convention
    - "begin" and "end" operators for custom iterators also use this operator syntax rather then the forced naming convention that c++ uses
4. function pointers use a syntax of funcptr<ReturnType(ParamType, ParamType2)> this is just a cleaner syntax for function pointers then what c/c++ has.
5. lambdas need to list there return type like this [](int i) -> int {} the extra -> is not needed if there is no return type
6. we use a single inheritance multiple interface model. you can define an interface just like a normal class with interface SomeInterface { virtual void Func(); }
7. in classes we support both c/c++ style visibility modifiers like public: private: and protected: but we also have more c# style with public void Func(); the inline modifier does not override the c/c++ modifier instead the c/c++ modifier just adds the requirement that all content in its scope has the correct c# style modifier. this is so you know the visibility of something without needing to hunt down the tag(this is a common issue with objects that are very large) but you can still get the visual grouping of the c/c++ tags as well as force the person extending a class to think more about the organization of where they are putting there modification.

beyond the changes listed and probably some small other differences this language work just like c++

### Additions
this language adds some things that are not in c/c++ such as.

1. built int unit testing with [test("test.group")] annotations
    - [test("math.vector")] void TestAdd();
    - set up and teardown functions in classes [test_setup] and [test_teardown]
    - all test functions in objects need to be static
    - test names on objects that are inherited by the test functions in the type like [test("math.vector")] class VectorTests{ [test("addition")] static void TestAddition(); } the test group for the TestAddition function would be "math.vector.addition"
2. reflection. we have a built in runtime reflection system. to much to document here you can just look at a skill or the abstractions in the default library
3. garbage collection. this uses a go style garbage collection.

## Comments

**Keep comments minimal.** Code should be self-documenting through clear names and structure. Only add a comment when the code itself cannot convey the reason or intent.

- No paragraph-long explanations for what a function does — the signature should make it clear.
- No `///` doc-comment blocks on every method unless there's actual ambiguity to resolve.
- Section dividers (`// ---`, `// --- section name --- //`)(make the --- longer in actual code so it stands out) and groupings are allowed to help organize the code.
- If something needs explanation, one short line is enough. If it takes more than two lines of comment, the code probably needs restructuring.


## Compiler stability
if you know of or find a bug you fix it. this has been a constant issue where you will find a bug and say to yourself that it is pre existing then ignore it even when i tell you to fix all the issues you find.
it does not mater if something was pre existing or pre documented if it is a bug it needs to get fixed. you keep incorrectly telling me that everything works even though there are bugs that you know of that you are just ignoring.
this gives me the impression that the compiler is working just fine even though it is not. which is extremely infuriating to have you lie to me and constantly ignore issues.
ALWAYS fix issues that you find or know of regardless of if it was pre existing or not.
when you are done with the work that you are given the compiler and everything else should be in a 100% working condition without fail.
always keep the documentation and skills up to date. if one of them says that there is a limitation then test that limitation fix it and update the docs/skill to remove that limitation/bug
keep everything extremely thoroughly tested to validate so we know when something brakes.

### Known bugs list
`docs/status/known-bugs.md` is the single list of open bugs. Follow its rules:
- When you find a bug, add an entry right away using the template in that file and the next free `BUG-NNN` id (then bump "Next ID"). Do this even if you plan to fix it in the same session, and even if it looks pre-existing.
- When you fix a bug, delete its entry in the same change as the fix, and add a test that would have caught it. Also remove the matching "Current implementation" note on the feature's docs page.
- Never reuse or renumber ids. Reference the id in commit messages and test names (e.g. "Fix BUG-012 ...").
- An entry marked "Needs re-test" must be re-tested, then either fixed or deleted if it no longer reproduces.

## Code cleanliness

Code should be easy to read, locally reason about, and extend without touching unrelated code. These apply to all new and touched code. The standard here follows "How to write the perfect function" (Logan Smith, https://www.youtube.com/watch?v=2OMRWPOSw9s), which references Tony Vineyard's talk — worth watching both.

**Honest functions.** A function is honest if it accesses the outside world only through its signature: it never reads or writes anything that isn't reachable through one of its parameters (an implicit `this` counts as a parameter). Honest functions give local reasoning (you can understand and verify them from their body alone, without knowing what else ran earlier) and testability (predictable behavior regardless of program state). Dishonest functions — I/O, global state, clocks, environment — are infectious: any function that calls a dishonest one becomes dishonest itself. So build the core business logic out of honest functions at the leaves of the call tree, and inject dishonesty only at the topmost possible level: `main()`, file/console/network I/O, event handlers, framework callbacks.

**Framework hooks stay tiny.** Functions called by an external framework (`main`, per-frame `update`, callbacks) exist only to glue your code to the framework. Keep their bodies minimal and jump back into honest functions as soon as possible — that gluing is their single responsibility.

**Signatures should be nice to the caller.** The signature is used first by a human; design it for them.
- Many parameters or a mysterious boolean → group them into a small parameter struct named for what's being passed in. (This is distinct from the anti-pattern of demanding an entire object when you only need two fields — don't make the caller hand over their whole wallet when you just need the card.)
- Don't demand stronger guarantees than you actually need: if you only iterate, take a view/iterator; if you need contiguity or ownership, ask for it explicitly. But when you genuinely do need strong guarantees, be upfront about asking.
- Encode invariants in types instead of comments: if a parameter must satisfy an invariant (e.g. a normalized vector), use a wrapper type that can only be constructed satisfying it, with cheap/implicit conversion back to the plain type. The compiler then enforces the contract at every call site.
- If B logically requires A to have happened, express that in the signature (A returns a handle/receipt that B takes) instead of a comment saying "call A first."

**Golden rule: one level of abstraction per function.** Every line in a body should exist at the same level of abstraction — your function stacks existing bricks; if you find yourself zooming into a brick, fiddling with it, and zooming back out, that is a missing function call. Loops with non-trivial bodies get extracted (or use standard library algorithms instead of hand-rolling binary search). Section-divider comments labeling steps in a body are a smell that levels are mixed — extract each section into a named helper.

**No global mutable state.** Don't conjure global (the classic case is a global PRNG): pass what you need as a parameter. Passing it in keeps the option open to inject deterministic, testable values later; baking the global into the function makes that flexibility permanently unrecoverable.

**Write the functions you want to see in the world.** Before implementing, sketch out the small single-purpose functions you know you'll need (Tony Vineyard's advice). Functions abstract not just spatially but temporally — it's fine to call a function now and implement it later; that keeps your current thought going without a context switch.

**Single purpose.** A function does one thing; a struct/class models one concept. If the name needs "and" (ParseAndValidate) or the body has two unrelated chunks, split it. Multi-purpose structs are where coupling and accidental mutation live; prefer small focused types over one big bag of fields.

**No bloated files.** Files are organized by subsystem, not by "everything added today." When a file grows past a manageable size, divide it into smaller files per concern (the parser is split this way: Expressions/, Declarations/, Statements/). A new feature lands in the file that owns its subsystem; if none exists, create one instead of appending to an existing grab-bag.

**Tests with real coverage.**
- Every behavior gets tests, including all edge cases: empty input, boundary values, error paths, malformed/unexpected input. Happy-path-only is not done.
- Honest functions are what make isolated testing possible: each test sets up its own state, exercises one behavior, asserts on it — no reliance on test execution order, shared mutable fixtures, or global state left behind by earlier tests. If a function can't be tested in isolation, that's usually a dishonesty to fix, not a test problem.
- Test files are organized to mirror the source subsystems (C4Testing/src/Tests/<Subsystem>/) and named after what they cover, not generic names like MoreTests or Test2.
- A failing test is fixed, never deleted, commented out, or weakened to pass.

**Performance stays a first-class concern.** Clean code must not come at the cost of speed:
- Don't copy large objects around when a pointer/reference will do — pass by reference where the copy cost is real.
- Don't materialize intermediate data structures you can avoid (e.g. prefer a for-each/streaming callback over building a huge vector of results when memory matters).
- Purpose built solutions when applicable.

**Other standards for clean, maintainable, extensible code.**
- Delete dead code instead of leaving it in place; unused functions, parameters, and flags are bugs waiting to happen.
- No speculative abstractions: don't add interfaces, factories, or options for hypothetical future needs. Two real uses is when a pattern earns its keep.
- Name things for what they mean in this codebase, not after the implementation detail. Consistent naming across a subsystem matters more than clever names.
- No magic numbers/strings in logic — named constants or enums whose name states the reason.
- Errors are explicit and handled at the level that can act on them; don't swallow them, don't silently default, and never `exit()`/abort from library code when a result type will do.
- Encapsulate ad-hoc data structures: if several functions in a class mirror the same low-level bookkeeping (same lowercasing, same insertion search), that's an un-named data structure — extract it into its own type and let the callers use the brick.
- Refactor as you go: when touching code that violates these rules while fixing something else, clean it up in the same change rather than leaving debt behind.
- Includes name a file from the project's source folder (Bedrock: its root, `include "Text/string.ch"`), or plainly when it sits beside the includer; never with `../` (an include resolves beside the includer first, then in the project's folders: docs/language/15-projects.md rule 3).
- The build and the full test suite must pass before work is considered done.

## Additional notes
keep documentation up to date with the language design.