---
name: browser
description: Rendering and testing web pages with IED's server-side browser - opening the workspace's dev server or files, reading pages as accessibility trees with element refs, clicking and typing, screenshots, console errors and failed requests, evaluating script, and checking a web change works before saying it does.
triggers:
  - "web page"
  - "check the page"
  - "dev server"
  - "screenshot of the site"
  - "console errors"
  - "click the button"
  - "frontend change"
---

# The browser

IED runs web pages in a headless Edge on the server. A page you open has an id (`pg-...`) that the other tools take,
lives until you close it (or the user does), and the user can watch it live in a Browser tab while you work: each of
your clicks and keystrokes is drawn on the page for them.

## Workflow for a web change

1. Make sure the page is served: start the project's dev server as a task or in a terminal, and wait for its "ready"
   line. A plain HTML file of the workspace opens directly (`file:///C:/project/index.html`).
2. `browser_open {url}` - waits for the load event; the result names the page id and any console errors.
3. `browser_snapshot {page}` - the accessibility tree: one element per line, `- button "Save" [ref=e7]`, with values
   (`textbox "Email": "a@b.c"`) and states (`[checked]`, `[disabled]`, `[expanded]`). Prefer it to screenshots for
   reading: it is exact and cheap. `mode: "text"` gives the visible text, `mode: "dom"` the HTML.
4. `browser_act {page, action, ref | selector, text?}` - `click`, `hover`, `type` (into a field), `press` (`Enter`,
   `Tab`, `Escape`, arrows, one character), `select` (an option's value), `scroll` (to an element, or the page: `up`,
   `down`, pixels). Refs come from the latest snapshot; take a new snapshot after the page changes.
5. `browser_console {page}` and `browser_network {page}` - uncaught errors, console warnings, failed requests. Pass
   `since` (the last seq you saw) to get only what is new.
6. `browser_screenshot {page, full_page?}` - for layout and visual checks; the PNG lands in
   `.ied/local/browser/shots/` (models that see images get it).
7. `browser_eval {page, js}` - read state the page does not show (`document.title`, `localStorage`, a component's
   data). It runs in the page with the page's rights; it needs approval in the default permission mode.

A change to a web UI is done when the page loads without console errors, the snapshot shows what was asked for, and
the interaction works when you drive it - not when the code compiles.

## Notes

- Pages of one workspace share cookies and storage; other workspaces' pages do not see them.
- The viewport is 1280x800 unless `browser_open` gets `width`/`height`; a page the user resized keeps their size.
- `alert`, `confirm` and `prompt` dialogs are answered (accepted) automatically, and the console says so.
- `browser_navigate {page, url}` loads another URL in the same page; `browser_close` when you are done.
