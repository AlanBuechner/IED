# Features: editor (EDT)

Owner: `plugins/editor` unless noted. Design: `docs/05` (overview), `docs/15` (keymap), `docs/17` (C4 LSP quirks the generic client must tolerate).

"Like Visual Studio, not VS Code" means: VS keybindings, VS window names and behaviors, Error List, navigation bar, preview tab, map-mode scrollbar, VS-style context menus, track-changes margin. Monaco is the text surface; the VS feel is everything around it.

## Document model (server-owned buffers)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-1 | Server-side document store: every open document's text + version lives on the server; clients hold replicas | P0 | US-EDT-14 | src/fs (doc store), plugins/editor |
| F-EDT-2 | Unsaved edits persist on the server (hot exit): survive browser close, server restart (journaled to `.ied/local/buffers/`), visible from other devices | P0 | US-EDT-14 | src/fs |
| F-EDT-3 | Edit lease per document: one client edits, others mirror read-only live with *Take over*; lease auto-releases after idle | P1 | US-EDT-15 | src/fs |
| F-EDT-4 | AI edits go through the same document store: if the doc is open with unsaved changes, the AI edit is applied to the buffer (not disk) and shown as an AI change (see F-EDT-90) | P0 | US-EDT-16 | src/fs, src/ai |
| F-EDT-5 | External change detection: clean buffer -> reload silently; dirty buffer -> banner with Compare / Keep mine / Take disk | P0 | US-EDT-16 | src/fs |
| F-EDT-6 | Encoding detection (UTF-8 default, BOM preserved, UTF-16 read), EOL preserved per file, both shown/settable in the status bar | P0 | US-SHL-6 | src/fs |
| F-EDT-7 | Large file mode: > N MB disables LSP/minimap/folding; > M MB read-only paged viewer | P1 | - | plugins/editor |
| F-EDT-8 | Save, Save All (Ctrl+Shift+S), auto-save off by default (VS), save-on-build setting (VS default: save all before build) | P0 | US-BLD-2 | plugins/editor |

## LSP integration (gen)

The host's LSP pool (`src/lsp`) manages servers; this plugin renders. Every feature is gated on server capabilities.

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-10 | Completion (IntelliSense member list), with resolve when supported; commit characters; snippet completions | P0 | US-EDT-1 | plugins/editor, src/lsp |
| F-EDT-11 | Hover (Quick Info), signature help (Parameter Info, Ctrl+Shift+Space) | P0 | US-EDT-1 | plugins/editor |
| F-EDT-12 | Go to definition (F12), declaration, type definition, implementation (Ctrl+F12), peek definition (Alt+F12) | P0 | US-EDT-1, US-EDT-13 | plugins/editor |
| F-EDT-13 | Find All References (Shift+F12) into a *Find Symbol Results* tool window (grouped by file, with preview), plus document highlight | P0 | US-EDT-1 | plugins/editor |
| F-EDT-14 | Rename (Ctrl+R, Ctrl+R) with preview of the WorkspaceEdit across files | P0 | US-EDT-1 | plugins/editor |
| F-EDT-15 | Formatting: document (Ctrl+K, Ctrl+D), selection (Ctrl+K, Ctrl+F), on save / on paste / on type - each a setting, default off except as the language plugin recommends | P0 | US-EDT-1 | plugins/editor |
| F-EDT-16 | Code actions / quick fixes (Ctrl+. lightbulb), filtered client-side to the diagnostics at the cursor (C4LSP returns all actions for the document) | P0 | US-EDT-1 | plugins/editor |
| F-EDT-17 | Semantic tokens -> theme token colors via the language plugin's mapping; refetch after each `publishDiagnostics` when the server lacks `semanticTokens/refresh` (C4LSP) | P0 | US-EDT-1 | plugins/editor |
| F-EDT-18 | Folding ranges (outlining: Ctrl+M, Ctrl+M / Ctrl+M, Ctrl+O / Ctrl+M, Ctrl+L) | P0 | US-EDT-1 | plugins/editor |
| F-EDT-19 | Document symbols -> navigation bar (VS: project / type / member dropdowns above the editor) and Document Outline tool window | P1 | US-EDT-13 | plugins/editor |
| F-EDT-20 | Diagnostics: squiggles, Error List integration (F-BLD-30), scrollbar markers, hover detail with code/source | P0 | US-EDT-1 | plugins/editor |
| F-EDT-21 | Inlay hints, CodeLens, call hierarchy (Ctrl+K, Ctrl+T), type hierarchy, selection range (Shift+Alt+= / -), linked editing - when servers support them | P1 | US-EDT-13 | plugins/editor |
| F-EDT-22 | Workspace symbols -> Go To All (F-SHL-23) | P0 | US-EDT-13 | plugins/editor |
| F-EDT-23 | Custom LSP requests exposed as bindings and AI tools by language plugins (gen) | P1 | US-TOOL-2 | src/lsp |
| F-EDT-24 | Position-encoding negotiation (UTF-16 / UTF-8) with a per-profile override for servers that count bytes (C4LSP today) | P0 | - | src/lsp |

## Editing (VS parity)

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-EDT-30 | Indentation guides on by default; tabs default for new files; visible whitespace toggle (Ctrl+R, Ctrl+W) | P0 | US-EDT-3 |
| F-EDT-31 | Indentation detection per file (respect existing files), status-bar indicator and switch, *Fix indentation* (convert to project default) for file/selection | P0 (detect), P1 (fix) | US-EDT-3, US-EDT-8 |
| F-EDT-32 | Comment / uncomment (Ctrl+K, Ctrl+C / Ctrl+K, Ctrl+U) using the language's comment tokens | P0 | US-EDT-12 |
| F-EDT-33 | Line ops: duplicate (Ctrl+D), move (Alt+Up/Down), delete line (Ctrl+Shift+L), join, sort, transpose | P0 | US-EDT-18 |
| F-EDT-34 | Multi-caret (Ctrl+Alt+click, Shift+Alt+. insert next match), column/box selection (Shift+Alt+arrows, Alt+drag) | P1 | US-EDT-18 |
| F-EDT-35 | Auto-close brackets/quotes, bracket matching, bracket pair colorization (off by default - color carries meaning only), surround with | P0 | - |
| F-EDT-36 | Snippets (contributed + user), Code Snippets Manager equivalent | P2 | US-EDT-18 |
| F-EDT-37 | Clipboard ring (Ctrl+Shift+V cycles) | P2 | US-EDT-18 |
| F-EDT-38 | Word wrap toggle, zoom per editor, sticky scroll (VS 2022), line numbers | P0 | - |
| F-EDT-39 | Undo/redo across server round-trips; undo of an AI edit is one step | P0 | US-INL-1 |

## Margins and scrollbar

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-EDT-40 | Track-changes margin: yellow = unsaved, green = saved since open/vs VCS base (VS semantics), orange = reverted-to-different (VS) | P0 | US-EDT-2 |
| F-EDT-41 | Glyph margin: breakpoints (all kinds), current statement, bookmarks, AI-change markers | P0 | US-DBG-1 |
| F-EDT-42 | Map-mode scrollbar (the plan's "minimap on the scroll bar"): thin code thumbnail in the scrollbar with overview markers for changes, errors, warnings, breakpoints, bookmarks, search hits, caret; hover previews | P1 | US-EDT-5 |
| F-EDT-43 | Git blame annotations (inline end-of-line on the caret line, toggle full gutter blame) | P1 | US-GIT-5 |

## Navigation

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-EDT-50 | Navigate backward/forward (Ctrl+- / Ctrl+Shift+-), across files | P0 | US-EDT-13 |
| F-EDT-51 | Go to line (Ctrl+G), go to matching brace (Ctrl+]) | P0 | US-EDT-13 |
| F-EDT-52 | Bookmarks (Ctrl+K, Ctrl+K toggle; Ctrl+K, Ctrl+N/P), Bookmarks tool window, persisted per workspace | P1 | US-EDT-13 |
| F-EDT-53 | Task List tool window: TODO/HACK/FIXME comment tokens (configurable), from a background scan + LSP | P1 | US-EDT-13 |
| F-EDT-54 | Switch header/source (Ctrl+K, Ctrl+O) using the language plugin's pair rules (gen) | P0 | US-EDT-6 |

## Find and replace

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-60 | Quick Find/Replace in file (Ctrl+F / Ctrl+H): regex, match case, whole word, in selection, preserve case | P0 | US-EDT-4 | plugins/editor |
| F-EDT-61 | Find in Files (Ctrl+Shift+F) / Replace in Files (Ctrl+Shift+H): scope = current document / open documents / current project / solution / federation member / federation / custom folder; include/exclude globs; regex | P0 | US-EDT-4 | plugins/editor, src/fs |
| F-EDT-62 | Find Results tool windows (1 and 2, VS), streaming results with counts, grouped by file, keep results | P0 | US-EDT-4 | plugins/editor |
| F-EDT-63 | Replace with preview: per-file and per-hit checkboxes, diff preview, apply as one undoable transaction (buffers updated, not just disk) | P0 | US-EDT-4 | plugins/editor |
| F-EDT-64 | Search backend: bundled ripgrep (fast, `.gitignore`-aware) driven by the server; C4-native fallback for when it is absent | P0 | US-EDT-4 | src/fs |

## Splits and bound splits

| ID | Feature | Pri | Stories |
|---|---|---|---|
| F-EDT-70 | Split editor (same document in two views: Window > Split), split document well into groups (vertical/horizontal), move tab to group | P0 | US-EDT-6 |
| F-EDT-71 | Bound split: a pair of groups linked by a rule; opening `X.cf` in one opens its pair `X.ch` in the other (pair rules contributed per language; fallback = same stem, configured extension pairs) | P1 | US-EDT-6 |
| F-EDT-72 | Compare (diff) editor: side-by-side and inline, word-level highlights, ignore whitespace, next/prev change (F8 / Shift+F8), editable right side | P0 | US-EDT-17 |
| F-EDT-73 | 3-way merge editor (used by git conflicts) | P1 | US-GIT-3 |

## Spell check (code-aware)

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-80 | Server-side spell service emitting diagnostics (source `spell`) for open documents; English dictionary bundled (SCOWL/Hunspell format) | P1 | US-EDT-7 | src/spell (host, generic) |
| F-EDT-81 | Identifier splitting (camelCase, PascalCase, snake_case, digits, acronyms) and checking of subwords; comments and string literals checked as prose | P1 | US-EDT-7 | src/spell |
| F-EDT-82 | Ignore lists from the language: keywords, semantic-token types/modifiers flagged builtin, symbols defined outside the workspace, words in the project's own identifiers (a word used as an identifier >= N times is accepted) | P1 | US-EDT-7 | src/spell, src/lsp |
| F-EDT-83 | Dictionaries: user (app tier), project `.ied/dictionary.txt` (committed), plugin-contributed (e.g. C4 terms); quick fixes: suggestions, add to project/user dictionary, ignore once | P1 | US-EDT-7 | src/spell |
| F-EDT-84 | AI-assisted mode (optional): a fast local model double-checks flagged words in batches to suppress false positives on jargon; never blocks typing | P2 | US-EDT-7 | src/spell, src/ai |

## Viewers and special editors

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-90 | AI change review in the editor: AI-modified regions highlighted (accent-ai), per-hunk accept/reject, "accept all for this file" | P0 | US-INL-1, US-CHT-12 | plugins/editor |
| F-EDT-91 | Markdown preview (side-by-side or tab): GFM, tables, task lists, code highlighting, mermaid diagrams, relative images/links, scroll sync | P1 | US-EDT-9 | plugins/editor |
| F-EDT-92 | Interactive markdown: clicking a checkbox (list or table cell) edits the source via the document store (undoable); links open in the editor or browser pane | P1 | US-EDT-9 | plugins/editor |
| F-EDT-93 | Browser pane: iframe for same-origin/proxied content (F-SRV-50) with URL bar, reload, devtools-lite console (from the page via proxy-injected script) | P1 | US-EDT-10 | plugins/editor |
| F-EDT-94 | Remote-render browser mode: server-side headless Chromium (Edge via CDP on Windows) screencast into the pane, for sites that refuse framing; shared with the AI browser tools | P2 | US-EDT-10, US-TOOL-5 | src/browser |
| F-EDT-95 | Image viewer (zoom, pixel grid, alpha checkerboard, info: size/format), video and audio players | P1 | US-EDT-11 | plugins/editor |
| F-EDT-96 | Hex viewer for binary files; "open with..." chooser listing all viewers that accept the file | P1 | US-EDT-11 | plugins/editor |
| F-EDT-97 | Custom editors/viewers contributed by plugins for file patterns (profiler traces, `.c4scene`, etc.) (gen) | P1 | US-EDT-11 | plugins/editor |
| F-EDT-98 | SVG preview (source + rendered), JSON/YAML tree view toggle | P2 | US-EDT-11 | plugins/editor |

## Inline AI

| ID | Feature | Pri | Stories | Owner |
|---|---|---|---|---|
| F-EDT-100 | Ctrl+I inline chat at caret/selection: prompt box in the editor, `@` mentions, streams a proposed change as an inline diff (green/red), accept (Tab / check) / reject (Esc / X) / follow-up | P1 | US-INL-1 | plugins/editor, plugins/chat |
| F-EDT-101 | Inline sessions are server-side (survive reload, unlike Echo) and listed in the chat panel's history as small chats | P1 | US-INL-1 | plugins/chat |
| F-EDT-102 | AI context-menu actions on selection/symbol: Explain, Fix, Document, Write tests, Refactor..., Send to chat (opens/focuses a chat with the selection attached) | P1 | US-INL-2 | plugins/editor |
| F-EDT-103 | Lightbulb AI fixes: "Fix with AI" on diagnostics, alongside the LSP's quick fixes | P1 | US-INL-2 | plugins/editor |
| F-EDT-104 | Ghost-text completion from a FIM-capable local model (role `completion` in the model group), debounced, cancellable, accept with Tab, word-by-word with Ctrl+Right | P1 | US-INL-3 | plugins/editor, src/ai |
