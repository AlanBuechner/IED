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

**Stage 2b (declaration edits).** `DeclarationQueries` records which declaration asked for which (the `Ask` edges);
a changed fingerprint re-resolves its file's declarations and, transitively, those that asked for them; a body
re-lowers when a declaration it read changed. Until those read sets are recorded, the coarse rule is exact: a body
re-lowers when any file in its include closure changed its fingerprint.

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
2. The build server: 2a body-only edits, then 2b declaration edits, behind a flag until every invalidation test above
   gives a cold build's bytes; the LSP's parses join it (B76).
3. The link (`/DEBUG:GHASH`, or fewer, larger debug sections) if option 4's measurement says so.
