# shared/ — code shared between host and plugins

## host-api/

The contract. Versioned definitions both sides code against:

- IPC protocol (message framing, request/response + event topics) — resolves open question #2 in `../docs/99`.
- Plugin manifest schema v1 (final form of the sketch in `../docs/02`) — open question #3.
- `.ied.sln` / `.ied.proj` schemas incl. toolbar command-template grammar — open questions #4.
- Contribution interfaces: sidebar icons, panels, toolbar items, output tabs, LSP profiles, DAP providers, skills, tool calls, bindings, settings schemas, system-message sections.

Until the compiler exists this folder holds schema drafts (JSON Schema / markdown) only.

## ui-kit/

Shared web components used by the shell and every plugin's UI:

- tree view (project/file trees), tab strip (chat tabs, editor tabs), ANSI console renderer (output window + terminal), icon set (VS-style distinct file-type icons), context menu, dropdown/button widgets for toolbars.
