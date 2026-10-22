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
- **The program's own code and data** come after the fixed point, in partition 0: global initialization, the
  reflection image's registration, the GC roots (which declare every other partition's static locals), the entry
  point or the test harness, the file-scope assembly. The reflection image itself is a module of its own (below).
- **Invoke thunks** are lowered on the coordinator and emitted like any round's trees, an own thunk in its target's
  partition and a shared one where its name places it, so an edit that adds a reflected method changes its own
  partition only.
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
points at), folding annotation arguments and reporting those that do not fold as before; `ReflectionTables::Build`
makes the LLVM constants from it and reads no model. One partition builds it in place, sharing the module's strings.
A split program keeps the description (`CodeGenDriver::KeepImageApart`) with its key, a 128-bit hash of every value
in it and of the target (`ImageDescription::Key`), and partition 0 declares the image. The output keeps the image's
object in the object cache by that key (`ObjectCache::ObjectKeyed`, a `KeyedModule` after the partitions): when an
object of the key is kept, the image is neither built nor written as bitcode; otherwise an assembly thread builds it
first, in a module and LLVM context of its own (`CodeGenDriver::BuildReflectionModule`), declaring what it points at
by name (a function as `void()`, a global as a byte, a program local hidden), and assembles it from memory. An edit
that changes no reflected declaration builds no image, and an edit of partition 0 reassembles no image with it. In
the IED server the image is a 6.4 MB constant whose bitcode alone took 0.1 s of every build.

## Imports (`Backend/CodeGen/ImportPlanner`)

Each partition gathers, on its thread, the facts of the functions it defines (size of their unoptimized IR, direct
callees, `inline`, `noinline`, `minsize`); the coordinator picks each partition's copies by D-76's rule (a direct
callee of at most 100 instructions, 70% of that a level further, three levels; an `inline` one whatever its size;
at `-O0` only those); each partition emits its copies from their HIR trees as `available_externally`
(`FunctionEmitter::EmitImport`), declaring their static storage and guards (`ModuleContext::SetImporting`) and
their nested functions, which their own partition defines. The census counters the copies need are lowered and
emitted after.

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
constants, the reflection image in a module of its own that declares only what the partitions define); TestCode with `-Incremental` (e2e-incremental) builds TestCode split; the IED server's own tests build it
split.
