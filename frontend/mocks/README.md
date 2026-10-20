# frontend/mocks/ - M0 static review mocks

Static HTML+CSS mocks of the six screens `docs/15` lists for design review, plus a token board. They exist so the owner can sign off the look before the real frontend is built (`docs/18` M0 item 2). Open `index.html` from disk; no server, no framework, no network.

| Page | Screen |
|---|---|
| `00-tokens.html` | every token in `../css/theme.css` with its live value |
| `01-editor.html` | Editor activity: Solution Explorer, two documents, Output + Error List, terminal open (`?syntax=grayscale` for the Grayscale palette) |
| `02-chat.html` | Chat activity mid-run with an approval card and the breakdown panel |
| `03-diff-review.html` | diff review: desktop document; phone flow via `?step=notification`, `?step=changes`, `?step=diff` |
| `04-settings-model-groups.html` | Settings window on AI > Model Groups |
| `05-git.html` | Git activity: refs, graph, commit details |
| `06-profiler.html` | Profiler timeline, `?view=threads` or `?view=tasks` |

Every page is responsive: below 720 px it switches to the phone shell (bottom nav, one pane).

## Rules

- Colors come only from `frontend/css/theme.css` tokens. `build.py lint` rejects raw colors anywhere else, non-ASCII bytes, and space indentation.
- Shared chrome (sidebar, menu bar, standard toolbar, terminal dock, phone nav) is injected by `js/mock.js`; code blocks are written with `` `kind:text` `` token markup that `mock.js` renders (kinds map to `--syntax-*`).
- `css/shell.css` is the seed for the real `frontend/css/layout.css`; `css/code.css` for the editor and diff surfaces.

## Commands

```
python -I frontend/mocks/build.py icons          # regenerate css/codicons.css from third_party/codicons
python -I frontend/mocks/build.py lint           # ASCII, tabs, no raw colors outside theme.css
python -I frontend/mocks/build.py shots [prefix] # Playwright screenshots into screenshots/
```

`icons` needs `third_party/codicons` (`tools/fetch-deps.ps1`); only the icons the mocks reference are embedded, as data-URI masks (Microsoft vscode-codicons, CC BY 4.0). `shots` needs Python Playwright with its Chromium (`pip install playwright` then `python -m playwright install chromium`).
