# 28. Developer mode: the AI fixes IED and C4 while it works

**Status:** built 2026-10-21 (owner's request: "a developer mode in the settings when we are building from source so the
AI can fix issues with the C4 compiler or IED itself as it is working on stuff"): `plugins/developer`, the chat's
`ai.roots` routing (`plugins/chat/code/roots.cf`), `IED::OpenFolder`; server checks `test_developer.py`. Rebuild and
relaunch built 2026-10-21 (owner's request: "in the settings we should add a re build and relaunch to make this easy for
me to keep the desktop build up to date"). Not yet: see "Where it is not yet" below.

## Why

IED, the C4 compiler, Bedrock and Nuke live in one repository (`docs/99` #18), and the rule for anyone working in it is
that a bug in the compiler or the IDE is fixed at the source, never worked around (`AGENTS.md`). A chat working on a game
in IED meets those bugs too - a miscompile, a missing Bedrock function, a tool that misbehaves - and today it can only
work around them, because it sees only its own workspace. When IED runs from its source checkout, the owner wants the
chat to be able to go and fix the bug where it lives, the way the sessions that build IED do.

## When it is on

- `developer.enabled` (on by default, any tier) has an effect only when the server is built from a source checkout:
  either its home (the folder it found `shared/host-api` in, `IED::HomeFolder`) is one - it holds `.git`,
  `C4/C4Lib/src` and `src/nuke.toml` - or it is an install whose `install.json` names one as its `source` and that
  folder still is one. `nuke install-ied` writes that record (`docs/21` "Install shape"), so the desktop app the owner
  installed from the checkout gets developer mode too; an install from elsewhere, or with no record, offers nothing
  whatever the setting says.
- Settings > Environment > Developer holds the setting and the desktop build (below). The status bar shows `DEV` while
  it is on (the source folder in its tooltip; a click opens the page); it reads `DEV: building` while a rebuild runs,
  `DEV: build failed` in red after one fails, and its tooltip says how many commits the checkout is ahead of the
  install.
- Only the owner's chats get it; a chat in an untrusted workspace does not (it cannot run builds anyway).

## What a chat gets

1. **The source as a second root.** File paths that start with `ied:/` name files in the source checkout
   (`ied:/C4/C4Lib/src/Backend/HIR/ExprLowerer.cpp`): `fs_read`, `fs_list`, `fs_glob`, `search_text`, `fs_edit`,
   `fs_write`, `image_view` and `image_info` take them. Under the hood the source is opened as a workspace of its own
   (a folder workspace, not added to the recent projects) the first time a path names it, so edits go through its
   document store like any other - an open editor of that file shows the change. `shell_run` takes `cwd: "ied:/"`.
2. **Permissions that always ask.** Writes into the source have the subject `fs.write(ied:/<path>)` and commands run
   there `shell(ied:/<command>)`; every one asks, whatever the chat's permission mode and its rules (a deny rule still
   refuses). The IDE's own source is never written silently.
3. **A system message section** (`docs/14` item 5, "IED itself"): this IED is built from source at `<path>`; when a
   failure is in the compiler, Bedrock, Nuke or IED rather than in the workspace, fix it there - read
   `ied:/AGENTS.md` and `ied:/C4/AGENTS.md` first, record a C4 bug in `ied:/C4/docs/status/known-bugs.md`, add a test
   that would have caught it, run `nuke build` and `nuke gates` in `ied:/`, and say in the chat what was fixed. About
   150 tokens; only while developer mode is on.
4. **Tools** (a deferred group `ied`, loaded with `tool_search`, `docs/13`):
   - `ied_rebuild()`: `nuke build` in the source, the end of its output; a compiler build refreshes the `dev`
     toolchain, so the workspace's next build uses the fix at once when it pins `dev`.
   - `ied_gates()`: `nuke gates`, the C4 test suites, with a summary of what failed.
   - `ied_known_bugs()`: the open entries of `known-bugs.md`, so a chat checks whether a bug is known before filing it.

## Rebuild and relaunch

The settings page keeps the desktop app current with the checkout without a terminal:

- **What it shows:** the source checkout; the install - the commit it was made from, when, and whether the checkout had
  uncommitted changes then (from `install.json`); the checkout's HEAD and how many commits it is ahead of the install
  (`git rev-list --count <installed>..HEAD`). An IED that runs from the checkout itself says so and has no install to
  compare.
- **Rebuild** runs `nuke.bat install-ied` in the checkout on the server (`ied.developer/rebuild`): Nuke builds the
  Development configuration and installs it (`docs/21`; a running IED keeps running, its programs renamed aside). The
  page shows the log as it comes (the last 400 lines are kept), the time it has run, and the end: `Built in 3:12`, or
  `Failed (exit 1) after 0:42` in red. **Cancel** (`ied.developer/cancel`) ends the build and every process it started
  (Bedrock's `Process::Child` runs it in a job); a build longer than an hour is ended the same way. One build at a time:
  a second `rebuild` is refused with `conflict`.
- **Rebuild and relaunch** (the desktop app only) does the same, then, when the build succeeds, calls
  `window.iedDesktop.relaunch()` (`docs/27`): the app records its windows and the workspace each shows, stops the
  server, starts the new install and opens the windows again. Every chat, build and terminal resumes from disk
  (`docs/12` restart rules). **Relaunch** alone is offered after a build that succeeded.
- **In a browser,** or a desktop app from before relaunch, the page says "Restart IED to use the new build: `ied stop`,
  then open IED" after a build instead.
- **The server owns the build** (`AGENTS.md` hard rule 2): the state lives in the plugin and the topic
  `ied.developer/state` carries it - the snapshot `{source, install: {commit, dirty, installedMs} | null, head, behind,
  build: {state, startedMs, endedMs, exitCode, lines}}`, the event `changed` (a new snapshot) and `build.lines`
  (`{lines}`, the lines added). A window closed during a build finds it, its log and its end when it opens again;
  `ied.developer/refresh` reads HEAD and `install.json` again (the page asks when it opens).
- Relaunching is the owner's click, never automatic from a chat: the chat tool `ied_rebuild` builds (`nuke build`) but
  installs nothing.

## What it does not do

- It does not move the running server onto a new build by itself. The owner's Rebuild and relaunch does (above); a
  chat that fixed IED tells the owner, who rebuilds when it suits them.
- It does not commit or push. The owner reviews the change (the chat's Modified files list shows `ied:/` files too) and
  commits it.
- It never touches another checkout: `ied:/` is the source this server was built from, nothing else.

## Where it lives (the host stays generic)

Nothing here is in the host. Developer mode is a default plugin, `ied.developer` (`plugins/developer`), built on the
plugin API like the others, and the chat plugin learns of the source through a binding, not a dependency
(`AGENTS.md` hard rules 1 and 7):

- **`IED::OpenFolder(path, ws, problem)`** (new in `src/api/workspace.ch`, permission `workspace.open`): a plugin opens a
  folder as a workspace, or finds the open one rooted there - the same as the owner opening it, so its watcher, document
  store and plugins run. Generic: any plugin with the permission may use it.
- **`ai.roots`** (a binding the chat plugin consumes, optional; `kind: function`): `{wsId}` -> `{roots: [{alias, wsId,
  title, confirm}]}`, other workspaces a chat of `wsId` may reach. A path `<alias>:/<path>` in a file tool or a
  `cwd` names `<path>` in that workspace; `confirm` makes every write and command there ask (the subjects
  `fs.write(<alias>:/<path>)`, `shell(<alias>:/: <command>)`). The developer plugin provides it with one root,
  `{alias: "ied", title: "IED source", confirm: true}`, while developer mode is on; nothing is reachable otherwise.
- **The system message section and the tools** are entries the plugin's code contributes when it activates
  (`PluginContext::Contribute`), only when the server runs from source, each with `when: config.developer.enabled`; the
  tools are deferred (`tool_search` loads them, group `ied.developer`).
- **The setting** is the plugin's `developer.enabled` (on by default; it has no effect without the source); its
  status item shows `DEV` while the source is there and the setting holds, from the plugin's topic
  `ied.developer/state` and the workspace's settings.
- **The settings page** is the plugin's `settingsPages` entry `ied.developer` (Environment > Developer, view
  `DeveloperSettings`, `plugins/developer/ui/buildpage.js`); it draws `developer.enabled` with the settings window's own
  row (`settings.row(key)`, `docs/07`) and the build below it. The build runs in `code/builds.cf`; the checkout and the
  record are found in `code/source.cf`.
- **Where it is not yet:** the chat's Modified files list keeps to the chat's workspace (an `ied:/` write shows in its
  tool card).

## Several chats at once

Two chats may meet the same compiler bug. Before fixing one, a chat reads `known-bugs.md` (`ied_known_bugs`) and the
other chats' recent work (`chat_list`, `chat_read`, `docs/04` "Chats talk to each other"); a chat that is fixing a bug
says so in `known-bugs.md` (its entry names the chat), so the other one waits for it (`chat_wait`) or works on.

## Order of work

1. The setting, detection, the Settings page and the `DEV` status.
2. `ied:/` paths in the file tools through a second workspace, the default `confirm` rules.
3. The system message section.
4. `ied_rebuild`, `ied_gates`, `ied_known_bugs`.
5. Rebuild and relaunch in the settings page (with the desktop app's `relaunch`, `docs/27`).
