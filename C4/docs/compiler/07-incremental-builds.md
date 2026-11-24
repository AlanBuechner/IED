# 07 - Incremental builds: where an edit's time goes, and the plan (R1, roadmap 5.3)

An incremental build (`--incremental`, Nuke's Debug and Development) already reuses the objects of unchanged partitions
([05](05-partitioned-emission.md)): the object cache is keyed by each partition's bitcode. Everything before the
object cache - reading, preprocessing and parsing every file (Bedrock too), building the model, lowering every body,
emitting every partition's IR and writing its bitcode to hash it - runs again on every build, and C4.exe is single-shot.
This page measures that, weighs the ways to cut it, and says what each reuse must key on so that it stays exact: a
wrong reuse is a miscompile, so every reuse is checked against a cold build's output, byte for byte.

## Measurements

`C4/Benchmarks/incremental/measure.ps1` copies a program's sources to a scratch folder and builds it four times with
the arguments Nuke wrote (`<int>/compile.args`): cold (empty int folder), nothing changed, a statement edit (a string
in a body), a header declaration edit (a constant's value). Each line is one run's wall time and its `-ftime` phases,
on 16 threads, with the dev toolchain of b12fa71a (2026-11-22). Runs spread about 0.1 s; the cold runs depend on the
file cache (the IED server's first cold run read its sources for 2.8 s).

```
powershell -ExecutionPolicy Bypass -File C4/Benchmarks/incremental/measure.ps1 -Compiler toolchains/c4/dev/C4.exe ^
  -ArgsFile build/int/Development-windows-x64/ied-server/compile.args -Root C:/GameDev/IED -Scratch <folder> -Opt O3 ^
  -Statement "src/web/Rpc.cf|unknown method '|unknown method: '" -Header "src/web/Frames.ch|c_ProtocolVersion = 1|c_ProtocolVersion = 2"
```

Claymore's editor: `-Statement "Claymore/Editor/src/Editor/Commands/AuthoringCommands.cf|nothing is hidden|nothing is
hidden now" -Header "Claymore/Editor/src/Editor/Mcp/McpServer.ch|c_CallTimeoutMs = 20000|c_CallTimeoutMs = 20001"`.

| Development (`-O3 -g --incremental --script-host`), s | wall | register | preprocess | parse | model | lower | optimize+codegen | runtime+link |
|---|---|---|---|---|---|---|---|---|
| IED server, cold | 17.9 | 2.83 | 0.17 | 0.19 | 0.24 | 0.68 | 12.5 | 0.49 |
| IED server, nothing changed | 1.57 | 0.06 | 0.10 | 0.11 | 0.18 | 0.62 | 0.20 | 0.01 |
| IED server, a statement | 3.00 | 0.05 | 0.11 | 0.09 | 0.16 | 0.64 | 1.30 | 0.33 |
| IED server, a header constant | 3.04 | 0.05 | 0.10 | 0.09 | 0.16 | 0.58 | 1.38 | 0.33 |
| Claymore editor, cold | 28.8 | 0.88 | 5.74 | 0.27 | 0.44 | 1.25 | 18.3 | 0.61 |
| Claymore editor, nothing changed | 3.22 | 0.13 | 0.22 | 0.28 | 0.47 | 1.24 | 0.37 | 0.01 |
| Claymore editor, a statement | 4.67 | 0.13 | 0.22 | 0.29 | 0.46 | 1.24 | 1.21 | 0.48 |
| Claymore editor, a header constant | 4.71 | 0.13 | 0.22 | 0.28 | 0.43 | 1.22 | 1.36 | 0.48 |
| IED server `-O0`: cold / nothing / statement / header | 3.24 / 1.23 / 1.58 / 1.64 | | | | 0.16 | 0.47-0.50 | 1.24 / 0.10 / 0.16 / 0.21 | 0.47 / 0.01 / 0.30 / 0.30 |

The rest of the wall time is the script host (0.04 / 0.08), its inline bodies (0.02-0.05 after an edit), the interface
(0.04 / 0.10) and the process itself (about 0.2 s).

The `lower` phase is not only lowering: its steps (IED server / Claymore editor, an edit) are the bodies 0.12 / 0.19
(24187 / 42103 bodies, 8.8k / 17.6k model requests), emitting **every** partition's IR 0.13 / 0.23, the reflection
image's description 0.09 / 0.24, the partitions' imports 0.11 / 0.20, nounwind 0.07 / 0.10, thunks 0.04 / 0.10 and the
partitions' finish 0.03 / 0.08. `optimize+codegen` after an edit is writing every partition's bitcode to hash it
(about 0.2 / 0.37, what a build that changed nothing pays) plus optimizing and assembling the partitions the edit changed.

**Partition size** decides most of an `-O3` edit (IED server, same runs):

| `--partitions` | cold | nothing changed | a statement | a header constant |
|---|---|---|---|---|
| 32 | 15.3 | 1.43 | 4.11 | 5.47 |
| 128 (the default until stage 1) | 17.9 / 15.3 | 1.57 / 1.56 | 3.00 / 3.14 | 3.04 / 3.20 |
| 256 (the `--partitions` limit; the default since stage 1) | 15.1 | 1.65 | 2.39 | 2.53 |
| Claymore editor, 128 / 256 | 28.8 / 27.4 | 3.22 / 3.62 | 4.67 / 4.50 | 4.71 / 4.55 |

On Claymore's editor 256 partitions save 0.4 s of the edited partition's optimizer but cost 0.2 s more in `lower`
(emitting, the reflection description and the finish are per partition) and 0.04 s more hashing; a build that changed
nothing, which Nuke does not run, costs 0.4 s more.

So after an edit, at `-O3` the time is the edited partition's optimizer (about 1.1 s at 128), the front end and lowering
(1.1 s on the IED server, 2.4 s on Claymore's editor) and the link (0.3-0.5 s); at `-O0` it is the front end and
lowering and the link. A build that changed nothing still pays everything before the object cache.

## The options

| Option | What it saves (IED server, a statement edit at `-O3`: 3.0 s) | Effort | Risk |
|---|---|---|---|
| 1. Smaller partitions in an incremental build | measured at 256: 0.7 s on the IED server, 0.2 s on Claymore's editor, whose per-partition work in `lower` eats the rest | a constant and the `--partitions` limit | none for correctness (placement is by name); +0.1 s and about 300 MB for a build that changed nothing |
| 2. A build server Nuke keeps alive | estimated: front end 0.42 -> 0.05, lowering 0.64 -> about 0.1, bitcode hashing 0.2 -> 0, the process 0.2 -> 0: about 1.3 s, leaving the edited partition's optimizer and the link | large | exact invalidation of the model and of lowered trees (below) |
| 3. Disk caches (parses, the model, HIR) | the front end is 0.42 s here and 1.1 s on Claymore's editor; reading a serialized model of 60k lines plus Bedrock is estimated at a good part of building it | large: every model and HIR node serializable | the same invalidation as 2, plus versioning |
| 3b. A disk cache of each partition's bitcode keyed by its input (its lowered trees, what they read of the model, the target) | 0.13 (emit) + 0.2 (bitcode) + part of imports: about 0.4 s, without a server | medium | the key must cover everything emission reads from the model: a missed input is a miscompile |
| 4. A faster link | the link is 0.3 s (IED) and 0.5 s (Claymore) of every edit; lld-link has no incremental mode on COFF; `/DEBUG:GHASH` (precomputed type hashes) may cut the PDB merge - unmeasured | small to try | none (same program) |

**The choice.** First option 1 (most value per effort, no risk: it is the next stage). Then the build server (option 2)
in stages, because it is the only option that removes the front end, lowering and hashing together, and because the
model, the lowered trees and the parses are already made from explicit keys (R9, R10, `PriorParses`) - they only need
to outlive one build. Disk caches of the model (3) are not worth building: they cost most of what they save. Option 4
is a measurement to make with option 1.

## The build server

C4.exe gains a server mode Nuke starts once per program (configuration, target and arguments are its identity) and
talks to over a pipe: "build", with the changed files' paths; it answers with the diagnostics and the output, exactly
as a single-shot build would. Nuke falls back to a single-shot build when the server is gone or refuses (a changed
toolchain, changed arguments). The server holds, between builds:

| State | Keyed by | Reused when |
|---|---|---|
| each file's parse | its text, its MacroLayout, its bindings (as `PriorParses` in the LSP; B76 shares it) | its key is unchanged |
| each file's declaration fingerprint | a hash of every token outside the bodies of functions that are not `inline`, not `[compile_time]` and not in a template, plus its include list | - (it decides what the model reuses) |
| the model | the fingerprints of every file, the include closures | every fingerprint and include closure is unchanged (a body-only edit) |
| each body's lowered tree (HIR, MIR) | its function's body tokens, the request keys it asked (`DefinitionKey`, closure owner and lambda, type and operation) and the declarations it read | its body and everything it read are unchanged |
| each partition's IR and bitcode hash | the lowered trees placed in it, its imports, the target | none of them changed |

**Stage 2a (body-only edits, the common case).** An edit that leaves every fingerprint and include closure as it was
re-parses its files, swaps each changed function's body node in its record, re-lowers those bodies (their requests go
through `ModelMaking::Request`, which finds what exists and makes what is new), re-emits only their partitions and the
partitions that import them, and relinks. Any other edit rebuilds the front end inside the server, still reusing the
parses of unchanged files (their keys say so) and the bitcode hashes of unchanged partitions.

### Stage 2a as built

`C4.exe --serve` (requests on stdin) and `--serve-on <name>` (a named pipe, a Unix socket on Linux: how Nuke builds
Debug and Development, its `server` setting, [tools/02](../tools/02-nuke.md)) keep the last build - its `CompilerInstance`, model, `ProgramLowering` and
backend - between builds (`C4/src/Source.cpp` `ServeBuild`). Each build first prepares the sources as they are now in
a new compile that takes the last one's parses (registration, the macro and interop passes, `CompileAll`: a file whose
text, macro layout and bindings are unchanged keeps its tree). Then:

1. **Is it a body edit?** (`Serve/BodySwap` `SwapEdits`, `Serve/BodyEdits` `FindFileEdit`). The same files of the same
   projects are registered; every file parsed again has no parse error, and the same text and tokens outside the bodies
   its own functions alone read (`BodyIsItsOwn`) - and, since stage 2b, outside the bodies of inline functions and
   templates and the initializers of its globals (below). When those tokens kept their lines and
   columns, every declaration keeps its place (only byte offsets after a longer or shorter body move, which nothing
   emitted reads). When a body gained or lost lines (or columns, on its last line), what follows it moved: that is
   served too when nothing but such functions and function declarations follows the first token that moved - their
   bodies are lowered again where they are now (`KeptDefinition::moved`), and a moved body's lambdas keep their
   closures (`ProgramModel::ClosureBodiesMoved`) - and refused when a type, a global, a `[compile_time]` function, an
   annotated namespace or a `[test]` follows it, whose place the model or the test registry keeps
   (the measured choice: lowering the moved bodies again costs milliseconds, a cold build in the server about 1.5 s on
   the IED server). A changed body writes no lambda and declares no local type or function (their model entries are its
   lowering's own), a moved one declares none.
2. **Swap.** The live compile takes the edited files' text, macro layout and parse; the model's functions declared or
   defined in them point at the new definitions (`declarations`, `bodyNode`, `paramDefaults`); the trees they replaced
   stay alive (the model's types and symbols keep their nodes, whose places are unchanged).
3. **Lower again** (`ProgramLowering::Relower`). The body-reference pass of the model (`ModelResolver::
   TypeInstancesNamedIn`) must name the same template instances in the same order in the new body as in the old, and
   make none. Each changed body is lowered in place of its tree, recording what it asks the model for: every request
   records its answer, found or made, for the lowering that asked it outside another request (`ModelMaking::Ask`,
   `Asking`), and each answer a request made notes the phase it was made in (a sharing's start and end are phases). The
   swap stands for a cold build when the model made nothing new; the new body lowers without errors; it needs, beyond
   the functions lowered first (written ones, which every body may name), what the old needed, in order
   (`ReachedBy`); an answer only the new body asks for was made by the old body's phase; an answer only the old one
   asked for was made earlier, or another lowering of that phase asks for it too; and no body the old tree first named
   has diagnostics (they end with where the program first uses it). The program's diagnostics take the new body's in
   the old's place.
4. **Emit again** (`ProgramBackend::EmitAgain`, `PartialEmission`). A served backend keeps each partition's facts from
   the emission that last emitted it (`PartitionFacts`): its functions' import facts, its copies, its part of the
   call graph and the unwind answers it took, the out-of-scope globals it names, its static locals; and the reflection
   image the last whole emission kept apart (`KeptImage`). The emission after an edit emits only the partitions that
   hold a lowered-again body or copied one, into new modules; the other partitions' facts stand in for them in every
   step that reads other partitions (imports, nounwind, named globals). A partition left out whose copies, unwind
   answers or named globals would differ now is stale, and the emission starts again with it; a change of an emitted
   partition's static locals (partition 0 declares them for the GC roots) emits every partition. Partition 0, the
   program's own code, holds no function, so an edit seldom emits it.
5. **Write** (`ProgramOutput` with `ServedOutputs`). A partition not emitted again keeps its object, its inline bodies
   and its script-table candidates (`HostCandidates`) from the build that emitted it; the emitted ones' bitcode is
   written and their objects looked up or assembled; the table, the interface and the link are made as a cold build
   makes them.

Any other edit (stage 2b's below aside) - a declaration, a lambda body, a line added before a type or a global, a parse
error, a new template instance - is a cold build of the prepared sources in the same process, which then becomes the
live build. A build whose kept object is gone emits every partition.

### Stage 2b as built

The declaration edits whose effect on the model is known without re-resolving it are served on the same path; every
other one is a cold build in the server.

- **A global's value** (`Serve/ValueEdits`): the initializer of a variable of a namespace or a static member of a class
  that is no template, folded to a value before (a constant, or a global's starting value) - written in place, the
  rest of its own definition moving with it. Its symbol, and every global's of the edited files, takes the new
  definition (`declNode`, `initializer`); it and every global whose initializer names it, on to the end, are folded
  again as a cold build folds them (`FoldAgain`: folding is order-independent, a constant folding what it names on
  demand) and must fold to a value of the kind they had; the partitions that hold their storage are emitted again
  (`ProgramBackend::StorageChanged`; only the home partition starts a global with its value). Every place a changed
  name is written is found - by word in every file's text, then by the lexer's tokens through the macro layout: in a
  comment or a string it reads nothing; in a function body it makes that body's lowered functions readers, lowered
  again (a template's instances, an inline function, a lambda's owner alike); in an initializer of a global folded
  again, or naming such a global where it is declared, it is the fold's. Anywhere else the edit is a cold build: a
  type's extent, a field's initializer, a parameter's default, an annotation, a macro or a directive, the body of a
  `[compile_time]` function (a constant may run it), a global that starts at run time. A name is matched as a word
  wherever it is declared, so a reader of another of that name is lowered again too, which changes nothing; the
  readers' bodies must name the same template instances with the new values (`TypeInstancesNamedIn`).
- **An inline function's body**: lowered again like an own body; the partitions that copy it are emitted again with
  it (their facts say they copy it). No constant runs it: a `[compile_time]` function calls only its kind.
- **A template's body** (a function template's, a class template's method's): each of its instances the build lowered
  is lowered again from the new body; one nothing reached stays unlowered (what reaches a function is the lowered
  bodies' needs). An instance is lowered after the first pass, where the reach comparison does not look, so it must
  need what it needed (`ProgramLowering::LaterNeedsDiffer`).

What stays a cold build, and why: a signature (the record's types, its name - which places it in a partition - and
the reflection image and invoke thunks, which a whole emission makes), a field or a type (layouts reach every user),
an alias, an include (what is visible), a macro (every file's tokens through it), an annotation (reflection), a
`[compile_time]` function's body (the constants it folds), a function or a global added or removed (the order and
the names of everything after it).

### Measured

`C4/Benchmarks/incremental/serve-check.ps1` builds a copy of a program's sources after each edit twice - one-shot and
through one `C4.exe --serve` kept across the edits - and compares the two outputs: the objects (named by their
bitcode's hash), the `.c4i` and the executable but its link stamps. Both build into the same folder (each moved there
while it builds), since a build's paths are in its output: a binding's file is in the int folder, and where a file's
functions go hangs on its path. Every build below was the same, byte for byte (2026-11-24, 16 threads, the dev
toolchain with this stage; each edit is undone by the next).

| Wall time, s: one-shot / served | IED server `-O3` | IED server `-O0` | Claymore editor `-O3` | Claymore editor `-O0` |
|---|---|---|---|---|
| first build (cold in both) | 17.1-20.2 / 14.3-14.8 | 4.74 / 3.58 | 28.8 / 26.1 | 13.2 / 9.6 |
| a string in a body | 2.21-2.41 / 0.99-1.08 | 1.71-1.73 / 0.70-0.73 | 4.98-5.23 / 2.44-2.61 | 3.34-3.47 / 1.28-1.32 |
| a line added to a body, removed again (moved bodies) | 2.85 / 1.63 | 1.72-1.73 / 0.73-0.74 | 4.33-4.64 / 1.84-2.33 | 3.37 / 1.30-1.33 |
| a header constant (stage 2b: its readers lowered again) | 2.88-2.95 / 1.74-1.79 | 1.74-1.75 / 0.83-0.84 | 4.61-4.66 / 2.22-2.38 | 3.35-3.37 / 1.36-1.37 |
| a header constant before stage 2b (cold in the server) | 2.93-3.66 / 2.67-2.85 | 1.78-1.91 / 1.54-1.64 | 5.35-5.43 / 4.83-4.92 | 3.35-3.38 / 3.01-3.02 |
| a signature: a parameter widened, `int32` to `int64` (cold in the server) | 2.58-2.82 / 2.34-2.43 | 1.93-2.03 / 1.74-1.82 | 5.66-5.89 / 5.19-5.23 | 4.39-4.40 / 3.86-3.89 |

IED server: `-Edits "src/web/Rpc.cf|unknown method '|unknown method: '~~..."` with `-ArgsFile
build/int/Development-windows-x64/ied-server/compile.args` (`-Opt O0` for `-O0`); Claymore's editor:
`Claymore/Editor/src/Editor/Mcp/McpServer.cf|the body is no JSON|the body is not JSON` (`McpServer::Answer`), a line
added after `out.AppendInt(code);` (`Failure`), `McpServer.ch|c_CallTimeoutMs = 20000|c_CallTimeoutMs = 20001`, and
`McpServer.cf|int32 code, Core::StringView message|int64 code, Core::StringView message`; the IED server's header
constant is `src/web/Frames.ch|c_ProtocolVersion = 1|...` (read in two bodies; another global of its name, in
`plugins/mcp`, is lowered again with them) and its signature `src/web/Proxy.cf|UpstreamUrl(int32 port|UpstreamUrl(int64
port`. A served body edit is the front end's parse of the edited file, the bodies, the emission of their partitions (a
few), the edited partition's optimizer and the link; a value edit is that for its readers and the constant's home
partition; a declaration edit served cold saves the process and the unchanged files' parses (0.2-0.7 s). The same edits
with `-fstack-maps` give the same bytes too. Between builds the server holds its last build: the IED server's is 6.0-6.4
GB committed, 3.7-5.2 GB in use at `-O3` (4.7-5.5 / 2.9-4.0 GB at `-O0`), Claymore's editor's 9.1 / 7.9 GB (`-O0`: 7.4 /
6.2 GB).

**The rest of stage 2b: not built.** The plan was: `DeclarationQueries` records which declaration asked for which (the
`Ask` edges); a changed fingerprint re-resolves its file's declarations and, transitively, those that asked for them; a
body re-lowers when a declaration it read changed. Building 2a and the value edits showed what exactness asks of it
beyond those edges, each a piece of its own:

- **Order and names.** The model's records, types and scopes are in registration order (files, then declarations), and
  a request's answer takes its place and its `_<n>` in the phase it is made in. A declaration added, removed or changed
  must be made again at its place, everything after it in every ordered table placed and named as a cold build places
  and names it, and every lowering request made again in the phase a cold build makes it (`ModelMaking::MadeIn` gives
  the phase; nothing can take an answer out of the model's grow-only tables yet).
- **Read sets.** A body reads declarations through every query - a name lookup, an overload resolution (a new overload
  changes what other calls resolve to), a layout - not only through requests: `ModelMaking::Ask` logs requests, not
  reads. Without read sets the coarse rule holds (a body re-lowers when any file of its include closure changed its
  fingerprint), which on a header edit is most of the program.
- **What was made from it.** Template instances of an edited template, or with an edited type among their arguments,
  and everything their making made; constants folded from an edited constant and the layouts they sized.

So any other declaration edit is a cold build in the server today: it keeps the parses of the files the edit did not
touch and pays no process start (measured above). The first case worth building is the commonest declaration edit of a `.cf`
file - a function added - which needs its record registered at its place, its overload set's other callers re-lowered,
and partition 0's declarations emitted again.

## Exact invalidation

What each change must force (each is a test: build A cold, edit to B, build B through the reuse, build B cold, and
compare the partitions' bitcode, the object names and the program, byte for byte):

| Change | Forces |
|---|---|
| a statement in a plain function's body | that body's lowering and its partition (and the partitions that import it when it is small enough to copy) |
| a declaration others use (a field, a signature, an alias) | its file's fingerprint: the declarations that asked for it, the bodies that read it, their partitions; a layout change reaches every user of the type |
| a macro (`#define`, `#if`) | the MacroLayout of every file the macro reaches, so their parses; then as their tokens changed |
| an include added or removed | the file's include closure: what is visible there (R4), so its declarations re-resolve and its bodies re-lower |
| a template's text (its body included) | every instance of it (`DefinitionKey`'s template) and every body that asked for one |
| a constant's value | its file's fingerprint: whatever folded it - an enum, an array extent and the layouts it sizes, the bodies that read it |
| a `[compile_time]` function's body | it is part of its file's fingerprint: every constant that called it, and on from there |
| an `inline` function's body | part of the fingerprint: the partitions that copy it (imports) |
| a reflected declaration (`[reflect]`, an annotation) | its fingerprint; the reflection image is already keyed by its description's hash (`ImageDescription::Key`) and the invoke thunks by name |

Two things the reuse must also keep: **names and order** - a request no live body asks for any more (an instance only
the old body named) must leave the model, or the `_<n>` numbering and record order (`OrderMade`) differ from a cold
build's, so the server keeps each body's requests and drops the answers nobody asks for; and **diagnostics** - a
reused body's warnings are reported again, as a cold build reports them.

## Stages

1. (Done, R1 stage 1) Incremental builds of a large program get 256 partitions (`kMaxPartitions`). More would need
   the per-partition work in `lower` (emit, reflection, finish) to shrink first: Claymore's editor already pays
   nearly what it saves.
2. The build server: 2a body-only edits (done: every edit kind above gives a one-shot build's bytes - C4Testing
   `Serve.*`, ToolsTest `Test-ServedBuilds`, `serve-check.ps1` - and Nuke builds Debug and Development through it),
   then 2b declaration edits (not built: "Stage 2b" above); the LSP's parses join it (B76).
3. The link (`/DEBUG:GHASH`, or fewer, larger debug sections) if option 4's measurement says so.
