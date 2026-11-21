# 05 - Partitioned emission (D-81, D-90)

Every build that links a program emits it straight into its partitions, each a module of its own in an LLVM context of
its own, the partitions on as many threads as `-j` allows. What differs is the optimization after (D-90, one emission
path):

- **An incremental build** (`--incremental`, D-76: Nuke's Debug and Development) optimizes each partition alone; a
  partition copies the small functions it calls from the others so they still inline (Imports, below).
- **A whole-program build** (Nuke's Dist) merges the partitions into one module first (`Compiler/PartitionMerge`), with
  the reflection image and a script host's table, optimizes that module whole, and splits it again for code generation
  on the output's threads (`SplitModuleToBitcode`, [01-pipeline.md](01-pipeline.md)). Its partitions copy nothing.
- `-S`, `-bc`, `-c` and a script compile emit one partition, the program's module, and write it as text, bitcode or one
  object (a script: the JIT takes it).

## The whole-program build's merge (D-90)

`ProgramOutput::WriteWholeProgram` writes each partition's bitcode on the output's threads (`PartitionBitcode` with
`PartitionOptimization::Whole`: no linkonce definition made weak), takes a script host's inline bodies from the inline
body cache with it as an incremental build does, then `MergePartitions` reads the partitions back into one context in
partition order and links them (LLVM's `Linker`), then the reflection image's and the script table's modules
(`KeyedModule::Builder`s, built straight into that context). Every local the emission made a hidden `.local` global so
another partition could name it (a file-static function, a static local, a once guard, an invoke thunk) is local again
under the name the program gave it (`ProgramName`), so the optimizer sees what it saw when the program was emitted as one
module: it may inline a static function into its one caller and drop it, promote its arguments, and change its
convention. What each partition keeps a copy of (a vtable, an RTTI locator, a string, a census record) is linked in once
per partition, the copies local, as two C++ files' would be. The merged module is verified, optimized at `-O<n>` and split
for the object cache and parallel code generation as before. A program of one partition is its module already: no merge.

A whole-program build emits at most 16 partitions (`c_MergedPartitions`, `C4/src/Source.cpp`; `--partitions` still
sets the number): each partition carries every type the program lays out, so linking the IED server's 128 took 2.8 s
where 16 take 0.6 s (with reading them back 0.5 s and the reflection image 0.5 s), and 16 still emit on every thread.
The number is fixed, not `-j`, so the merged module and the objects split from it are the same on any machine. On the
IED server (`-O3 --script-host`, 16 threads) the work before the optimizer is 2.0 s against the single module's 2.4 s,
a cold build about 57 s against 56 s (the runs spread 2 s), a one-line edit 52.7 s against 54.1 s, and the program 0.2%
larger (the partitions' local copies of vtables, strings and census records stay apart); the benchmarks run at the
single module's speed.

Chosen over a ThinLTO-style summary and import, which keeps each partition's own optimizer: that is the incremental
build's rule (copies of the small callees), and it costs run-time speed where a hot callee is too large to copy (the
math benchmarks' `batch_pointers`, 60% slower); a shipping build keeps whole-program optimization.

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

`ProgramBackend` (`SplitInto(count, onlyAlwaysInline)`, `LowerOn(threads)`) lowers first and emits after, and orders
everything, so the partitions' bitcode, and with it the object cache, does not depend on `-j`:

- **Lowering decides, before any emission.** `ProgramLowering` lowers what the program needs on `-j` threads, the
  model changes (template instances, closures, lowering scopes) made between its rounds in one order
  ([03-hir.md](03-hir.md#what-a-program-lowers-programlowering)); its `LoweredProgram` holds every tree in the order it
  was lowered.
- **Emission.** `PartitionSet::Emit` places each tree in its partition (`ProgramPlacement`) and the partitions emit
  their trees at once, each in the program's order; then each defines its own census counters.
- **The model is read-only while partitions emit.** Object layouts, computed on their first query, are settled before
  the partitions emit (`SettleLayouts`); the function a function-pointer global starts at was resolved by lowering
  (`LoweredProgram::functionPointerStarts`) and a census counter's signature made with it, so emission changes nothing
  in the model and needs no lock.
- **The program's own code and data** come after the fixed point, in partition 0: global initialization, the
  reflection image's registration, the GC roots (which declare every other partition's static locals), the entry
  point or the test harness, the file-scope assembly. The reflection image itself is a module of its own (below).
- **Invoke thunks** are lowered with the rest and emitted like any tree, an own thunk in its target's partition and a
  shared one where its name places it, so an edit that adds a reflected method changes its own partition only; the
  `ReflectionEmitter` then makes each a local of the program and records its entry in the partition that defines the
  target.
- **Objects.** Each partition's bitcode is written on the output's threads, and a partition whose object the cache
  does not keep is assembled on the same thread right after (`WriteCachedObjects` with a bitcode callback), so an
  edit's partitions start while the others' bitcode is written; partitions with the same code (empty ones) claim
  their shared object once (BUG-366).

## Placement (`Backend/CodeGen/ProgramPlacement`)

A function's partition is decided before its code exists, from its record: the file it is defined in (a template
instance's record names its template's), over that file's share of partitions 1 to n-1 by name, one partition per 48
functions the file defines rounded down to a power of two; a function the compiler wrote by its name. A global goes
by the file that declares it. How many partitions: `--partitions` (up to 256), else one per 24 functions the program
defines, rounded down to a power of two, 1 to 128, so the count changes only when the program doubles or halves. An
edit reassembles the partitions it touches, so their size is most of an edit's code generation: 128 keeps the IED
server's near 10k instructions each (64 left them at 32k, 0.8 s more per edit at `-O3`), for about 0.15 s more on a
build that changed nothing and 100 MB more at the peak; 256 saves no more and costs 300 MB more.

## Symbols

| What | One partition | Several |
|---|---|---|
| a function with a body | defined | defined in its partition, declared where another calls it |
| a file-static function, a static global, a static local, a once guard | internal | hidden external `<name>.local` (`ModuleContext::MakeLocal`), defined in its partition: the GC roots and a script host's table name them |
| a global's storage | defined where code first names it | defined in its declaring file's partition, its initial data (`StartAsData`) there; declared elsewhere |
| a constant with no symbol (an unscoped enum's member) | no storage | no storage |
| vtables and their thunks, RTTI locators, EH tables, strings, GUIDs, census records and counters, `c4.ipow.*`, finalizers, `func` thunks | internal or private | internal or private in each partition that uses them: dispatch and casts work through any copy (type identity is the RTTI type descriptor, `linkonce_odr`), and the runtime merges census records by name |
| invoke thunks | internal | hidden external `<name>.local`, which the image's module names: an own thunk in its target's partition, a shared one where its name places it, emitted at once with the round's others |
| the reflection image and its rows and strings | in partition 0: external, private | in the image's own module (D-82): `__c4_reflect_image` external, the rest private; partition 0 declares the image |

## The reflection image (D-82)

The image is the program's largest piece of data (13 MB in the IED server) and was built on the coordinator after
the fixed point. It is made in two steps: `DescribeReflectionImage` reads the model, on the coordinator, into an
`ImageDescription` (rows of numbers, texts, references to tables and the names of the functions and globals it
points at), folding annotation arguments (the model checked them all as it was built, `AnnotationCheck`, and made the
template instances they name, so this fold finds every type made already); `ReflectionTables::Build`
makes the LLVM constants from it and reads no model. One partition builds it in place, sharing the module's strings.
A split program keeps the description (`ReflectionEmitter::KeepImageApart`) with its key, a 128-bit hash of every value
in it and of the target (`ImageDescription::Key`), and partition 0 declares the image. The output keeps the image's
object in the object cache by that key (`ObjectCache::ObjectKeyed`, a `KeyedModule` after the partitions): when an
object of the key is kept, the image is neither built nor written as bitcode; otherwise an assembly thread builds it
first, in a module and LLVM context of its own (`ProgramBackend::BuildReflectionModule`), declaring what it points at
by name (a function as `void()`, a global as a byte, a program local hidden), and assembles it from memory. An edit
that changes no reflected declaration builds no image, and an edit of partition 0 reassembles no image with it. In
the IED server the image is a 6.4 MB constant whose bitcode alone took 0.1 s of every build.

## Imports (`Backend/CodeGen/ImportEmitter`, `ImportPlanner`)

Only an incremental build's partitions copy (`PartitionCopies::Small`, `Inline` at -O0; a whole-program build's merge
needs none, `None`). Each partition gathers, on its thread, the facts of the functions it defines (size of their unoptimized IR, direct
callees, `inline`, `noinline`, `minsize`); the coordinator picks each partition's copies by D-76's rule (a direct
callee of at most 100 instructions, 70% of that a level further, three levels; an `inline` one whatever its size;
at `-O0` only those); each partition emits its copies from their HIR trees as `available_externally`
(`FunctionEmitter::EmitImport`), declaring their static storage and guards (`ModuleContext::SetImporting`) and
their nested functions, which their own partition defines. Each partition then defines the census counters its
copies call (lowering made every counter the program's code needs).

## A script host

Thread-local accessors per partition; one symbol list across the partitions, in partition order (`HostEntries`),
partition 0's own locals among it (a host's shared vtables are such: they become hidden `.local` globals there),
leaving out another partition's own locals (its helpers); the table in an object of its own (`HostTableModule`, which
declares what it lists), kept by a key of what it lists (`HostTableKey`), so no partition changes with what another holds, and an edit
that adds a function makes that small object again, not partition 0 (the largest: 1.2 s at `-O3` in the IED
server); inline
bodies extracted per partition, on its thread, up to four chunks each, against one set of the table's names
(`HostNames`, made once), and kept between builds by the hash of the partition's bitcode and of the table's names
(`InlineBodyCache`, in `<int>/<name>.inline/`): a body names only what its own partition lists and what every
table lists (the runtime's entry points, the prelude's functions, the foreign objects' definitions), so the key needs
no more, and an edit extracts only its partitions'; the interface and its build id once the
partitions are assembled, the build id an object of its own (`<int>/<name>.build-id.obj`). A script knows a host's
local (a static member, a static local) by the name the program gave it, so the interface, the table, the
thread-local accessors and the inline bodies use that name (`ProgramName`: the `.local` suffix off), and the table
finds each under its partition name.

## Tests

C4Testing `Semantic.SplitEmission` (each function and global defined in one partition, locals other partitions name
hidden globals, the program's own code in partition 0, one partition keeping locals local, no storage for nameless
constants, the reflection image in a module of its own that declares only what the partitions define); `Lowering.Program`
(a census counter lowered once and defined in each partition that allocates the type; the lowering order the same on
one thread and on eight; codegen refusing to name what lowering did not lower); `Semantic.PartitionCopies` (a small
callee copied `available_externally` into its callers' partitions, a large one declared, copies reaching through small
callees, only `inline` ones at -O0, none when the partitions are merged, every partition verifying); `Compiler.PartitionMerge`
(each definition once in the merged module, the locals local again under their program names, the reflection image in
it with its invoke thunks local, one partition as the program, an unreadable partition an error); `Compiler.ObjectCache`
(the split of an optimized module, linkonce definitions kept weak only for a partition optimized alone); TestCode
with `-Incremental` (e2e-incremental) builds TestCode split and optimized alone, the other e2e runs build it
partitioned and merged; the IED server's own tests build it split.
