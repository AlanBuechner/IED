# IsolatedTest - one program per feature or regression

Each folder under a category is a self-contained program (one or a few `.cf`/`.ch` files, plus any
`.h`/`.c`/`.cpp` for interop) that compiles and runs through the real `C4.exe` and proves one language
feature or guards one bug. `RunTests.ps1` builds and runs every folder; it is a gate at `-O0`, `-O2` and
`-O2 -g` (see [../../docs/tools/05-test-suites.md](../../docs/tools/05-test-suites.md)). Programs that must not
compile live in `assets/NegativeTest`; the large mixed suite is `assets/TestCode`.

## Layout

```
Asm/               inline assembly: operands, register classes, options, clobber_abi calls, file-scope asm
Basics/            core constructs, conversions, promotions, multi-file programs and includes
Bedrock/           the default library through `include "bedrock.ch"`
Calls/             calls, function pointers, variadics, references returned by calls
ControlFlow/       loops, switch, range-for, goto
Diagnostics/       programs whose compiler output is checked (warnings of a build that succeeds)
Exceptions/        try / catch / finally, rethrow, uncaught exceptions
Functions/         declarations, definitions, returns, default arguments
Interop/           C and C++ through CBind, COM, Win32, DX12, the C ABI at the boundary
Lambdas/           captures, func, closures
Lifecycle/         create / copy / delete, relocation, temporaries, static storage
Macros/            the preprocessor
MemoryGC/          allocation, collection, destructors, liveness, collection across threads
Namespaces/        namespaced functions, types and templates
OOP/               classes, inheritance, virtual dispatch, out-of-line methods
Operators/         operator methods, built-in operators, constant operands, the conditional
Pointers/          pointers and references
Platform/          Bedrock's Platform namespace where a test needs its own program (page rights that fault)
Reflection/        the runtime type registry
Scripting/         the runtime's support for scripts: per-script exits, emulated thread storage
Simd/              SIMD intrinsics per family, packed casts and broadcasts, wide vectors, the -march target
Templates/         function, class, alias and member templates, specializations, packs
TestingFramework/  `[test]` functions and the `-test` harness
Threads/           the runtime's threads: start, join, detach, waiting on an address
Types/             layout, unions, swizzles, enums, arrays, casts, sizes
```

## Running

`main` returns the number of failed checks: 0 is a pass.

```powershell
.\assets\IsolatedTest\RunTests.ps1                     # every folder at -O0
.\assets\IsolatedTest\RunTests.ps1 -Filter Templates -O2
```

The runner reads four manifests beside it:

- `ExpectedExit.json`: programs that must end with another exit code (an uncaught exception), with output,
  flags or a JSON file to check.
- `ExpectedIR.json`: programs whose emitted IR must contain given text (volatile accesses, thread_local).
- `NegativeTests.json`: the few folders here that must fail to compile with a given message.
- `SkipTests.json`: folders that need something the machine may lack (a GPU).

Interop folders that link SDK headers (`Interop/Dx12DeviceInit`, `Interop/Win32WindowCallback`) need that
Windows SDK. C4Testing's `IsolatedCorpusModelTest` also builds a program model from every `.cf` here.

## Conventions

- One folder, one thing, named for the feature or the bug it guards (`Pointers/ReferenceReturn`).
- A regression test names its bug id in its first comment line (`// ... (BUG-216)`).
- Only sources belong in a folder; build outputs go under `assets/build-output`.
- A folder sits in the category that owns its feature, never at the suite root.
