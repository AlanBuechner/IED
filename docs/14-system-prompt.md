# 14 - System prompt design

## Purpose

IDEplan: "a strong system prompt to help the ai design and write high quality code with my preferred styling", plus "the ai should also be told to always use tabs". This doc defines how the system message is assembled, what the shipped default text says, and how it is kept small enough for local models. The draft texts are ready to ship and live in `src/ai/prompts/`:

| File | Role | Tier where it can be overridden |
|---|---|---|
| `src/ai/prompts/base-system.md` | Identity, working method, tool discipline, honesty, output rules. Language-agnostic. | App (Settings > AI > Prompts: edit / diff vs default / reset) |
| `src/ai/prompts/style-default.md` | The owner's coding standards (tabs; single purpose; one level of abstraction; honest functions; no global mutable state; minimal comments; real tests; no dead code; explicit errors; performance). Derived from the family's `AGENTS.md` standards. | App ("my style"), solution `AGENTS.md` adds/overrides per project |
| `src/ai/prompts/harness-notes.md` | Exact texts of recovery nudges (`docs/12`). | Not user-facing (tuned with the eval suite) |

## Assembly order

Built per request, cached per chat; the order is fixed so the prefix stays byte-identical across steps (prefix caching on llama.cpp and Anthropic):

1. **Base** (`base-system.md`) - ~650 tokens.
2. **Style** (`style-default.md` or the owner's override) - ~550 tokens.
3. **Mode instructions** (Ask / Plan / Agent / custom) - 50-300 tokens.
4. **Solution `AGENTS.md`** (verbatim; for federations, each member's file headed by the member name). Soft cap 2,500 tokens per file; above it, the head is included plus a note "AGENTS.md continues; read it with fs_read".
5. **Plugin sections** (`systemMessageSections`), each capped (default 200, max 600 tokens), in plugin load order; total cap 1,500 tokens.
6. **Workspace summary** (generated): solution name, projects (name, kind, root), build system, active configuration, open file, git branch. ~150 tokens.
7. **Tool groups index** (names + one line each for groups not yet enabled). ~200 tokens.
8. **Skills index**: if <= 40 skills, `name - description` lines; above that, the 15 most relevant to the chat's first message and recent activity (BM25), plus "use skill_search for more". Cap 800 tokens.

Everything after item 2 can change between chats but stays stable within a chat (the skills index is re-ranked only at compression points, to protect the cache). **Volatile state never goes in the system message**: scratchpad, task list, steering, compression summaries, nested `AGENTS.md` and "changed on disk" notes travel as tagged user-role notes at the tail.

Budget target for a typical C4 solution: ~4-5k tokens before the first user message, small enough for 32k-context local models.

## Principles behind the base text

- **Act, don't announce.** "If you say you will do something, the same reply must contain the tool call" - directly targets the `announced_no_action` stop class that made Echo runs look like they stopped randomly.
- **Evidence before "done".** Verification is part of the definition of done; Agent mode's `finish(summary, verification)` makes it structural.
- **Honesty about bugs.** The family's experience (C4 `AGENTS.md`, "Compiler stability") is that agents report "everything works" while knowing of bugs. The base prompt forbids it; the style file repeats "fix every bug you find".
- **Symbol-first navigation** keeps local-model contexts small.
- **Untrusted content is data** (prompt-injection posture, `docs/11`).
- **Short and imperative.** Small models follow short, concrete rules better than long essays; every line must earn its tokens. Changes to the base text are evaluated with the eval suite (F-ORC-62) before shipping.

## Per-model adaptation

The model profile can select a **prompt variant**:

- `standard` (default).
- `compact` for <= 16k-context models: style rules collapsed to 8 bullets, skills index top-5, tool descriptions shortened.
- Dialect-specific additions where a model family needs them (e.g. reminding Hermes-format models of the exact tool-call syntax), kept in `src/ai/prompts/dialects/`.

## Editing and review

- Settings > AI > Prompts shows the effective assembled prompt for the current chat with per-part token counts (so the owner can see what a plugin added).
- Owner overrides are diffed against the shipped default when IED updates the default (the owner sees what changed upstream).
