# 05 - Partitioned emission (D-81)

An incremental build (`--incremental`, D-76: Nuke's Debug and Development) emits the program straight into its
partitions, each a module of its own in an LLVM context of its own, the partitions on as many threads as `-j` allows.
A whole-program build (`Dist`), `-S`, `-bc`, `-c` and a script compile emit one partition, the program's module, and
do what they always did (a whole-program build optimizes it whole and splits it for parallel code generation,
`SplitModuleToBitcode`, [01-pipeline.md](01-pipeline.md)).

## Why

After an edit an incremental build assembles only the partitions whose code changed, so the serial work around code
generation was what was left. Before this, on the IED server (about 60k lines of its own plus the Bedrock it uses,
`--script-host`, `-g`, 16 threads), an edit at `-O0` took 3.3 s of which 0.1 s was code generation: the split (1.4 s:
the whole module written as bitcode on one thread, then a lazy copy of it read by every thread, 3.1 GB at the peak),
IR emission on one thread (0.4 s) and the script host's inline bodies (0.8 s, before B69's first step) were most of it.

| IED server, 16 threads (s) | before (one module, split) | partitioned |
|---|---|---|
| `-O0` cold / nothing changed / a statement / a header constant | 4.05 / 3.23 / 3.31 / 3.42 | 2.66 / 1.83 / 1.85 / 1.90 |
| `-O3` cold / nothing changed / a statement / a header constant | 12.5 / 3.48 / 4.46 / 5.18 | 8.38 / 2.12 / 3.25 / 3.44 |
| peak memory, `-O0` edit | 3.1 GB | 1.6 GB |

## The coordinator

`CodeGenDriver` (`SplitInto(count, threads, onlyAlwaysInline)`) keeps what must be serial and orders everything, so
the partitions' bitcode, and with it the object cache, does not depend on `-j`:

- **HIR lowering stays on one thread.** It changes the model (template instances, closures, lowering scopes).
- **Rounds.** `EmitReachable` lowers a batch, places each tree in its partition (`ProgramPlacement`), and the
  partitions emit their trees at once, each in the batch's order (`EmitPlaced`); what each partition's code
  referenced for the first time is the next batch, merged in partition order.
- **The model is read-only while partitions emit.** Object layouts, computed on their first query, are settled before
  each round (`SettleLayouts`); the two things emission asks of the model that can change it (a census counter's
  signature, the function a function-pointer global starts at) go through a lock the partitions share
  (`ModuleContext::ShareModel`).
- **The program's own code and data** come after the fixed point, in partition 0: global initialization, invoke
  thunks, the reflection image, the GC roots (which declare every other partition's static locals), the entry point
  or the test harness, the file-scope assembly.

## Placement (`Backend/CodeGen/ProgramPlacement`)

A function's partition is decided before its code exists, from its record: the file it is defined in (a template
instance's record names its template's), over that file's share of partitions 1 to n-1 by name, one partition per 48
functions the file defines rounded down to a power of two; a function the compiler wrote by its name. A global goes
by the file that declares it. How many partitions: `--partitions`, else one per 24 functions the program defines,
rounded down to a power of two, 1 to 64, so the count changes only when the program doubles or halves.

## Symbols

| What | One partition | Several |
|---|---|---|
| a function with a body | defined | defined in its partition, declared where another calls it |
| a file-static function, a static global, a static local, a once guard | internal | hidden external `<name>.local` (`ModuleContext::MakeLocal`), defined in its partition: the GC roots and a script host's table name them |
| a global's storage | defined where code first names it | defined in its declaring file's partition, its initial data (`StartAsData`) there; declared elsewhere |
| a constant with no symbol (an unscoped enum's member) | no storage | no storage |
| vtables and their thunks, RTTI locators, EH tables, strings, GUIDs, census records and counters, `c4.ipow.*`, finalizers, `func` thunks | internal or private | internal or private in each partition that uses them: dispatch and casts work through any copy (type identity is the RTTI type descriptor, `linkonce_odr`), and the runtime merges census records by name |
| invoke thunks, reflection rows | internal, private | in partition 0, with the image |

## Imports (`Backend/CodeGen/ImportPlanner`)

Each partition gathers, on its thread, the facts of the functions it defines (size of their unoptimized IR, direct
callees, `inline`, `noinline`, `minsize`); the coordinator picks each partition's copies by D-76's rule (a direct
callee of at most 100 instructions, 70% of that a level further, three levels; an `inline` one whatever its size;
at `-O0` only those); each partition emits its copies from their HIR trees as `available_externally`
(`FunctionEmitter::EmitImport`), declaring their static storage and guards (`ModuleContext::SetImporting`) and
their nested functions, which their own partition defines. The census counters the copies need are lowered and
emitted after.

## A script host

Thread-local accessors per partition; one symbol list across the partitions, in partition order (`HostSymbols`),
which partition 0, where the table is, declares, leaving out another partition's own locals (its helpers); inline
bodies extracted per partition, on its thread, up to four chunks each; the interface and its build id once the
partitions are assembled, the build id an object of its own (`<int>/<name>.build-id.obj`). A script knows a host's
local (a static member, a static local) by the name the program gave it, so the interface, the table, the
thread-local accessors and the inline bodies use that name (`ProgramName`: the `.local` suffix off), and the table
finds each under its partition name.

## Tests

C4Testing `Semantic.SplitEmission` (each function and global defined in one partition, locals other partitions name
hidden globals, the program's own code in partition 0, one partition keeping locals local, no storage for nameless
constants); TestCode with `-Incremental` (e2e-incremental) builds TestCode split; the IED server's own tests build it
split.
