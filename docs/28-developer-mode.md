# 28. Developer mode: the AI fixes IED and C4 while it works

**Status:** built 2026-10-21 (owner's request: "a developer mode in the settings when we are building from source so the
AI can fix issues with the C4 compiler or IED itself as it is working on stuff"): `plugins/developer`, the chat's
`ai.roots` routing (`plugins/chat/code/roots.cf`), `IED::OpenFolder`; server checks `test_developer.py`. Rebuild and
relaunch built 2026-10-21 (owner's request: "in the settings we should add a re build and relaunch to make this easy for
me to keep the desktop build up to date"). Autonomy built 2026-10-23 (owner: "the point of the developer mode that IED and
C4 edits are gated behind is so the AI can fix language bugs and iterate in IED and automatically rebuild and relaunch
IED and start its self back up to test the changes"; "developer mode should be disabled by default and only available to
enable if you are building from source"): off by default, writes and commands in `ied:/` follow the chat's permission
mode, `ied_relaunch`, the host's relaunch (`src/host/relaunch.ch`, `docs/27` "Relaunch") and the chat going on after it
(`docs/12` "Restart behavior"); server checks `test_developer.py` `RelaunchTests`, `test_control.py`,
`test_desktop.py`. Plugin folders writable and the note on every failing call built 2026-10-26 (owner's items 23 and
33, `docs/30`): items 1 and 5 below; server checks `test_developer.py`, `test_plugin_folders.py`. Not yet: see "Where
it is not yet" below.

## Why

IED, the C4 compiler, Bedrock and Nuke live in one repository (`docs/99` #18), and the rule for anyone working in it is
that a bug in the compiler or the IDE is fixed at the source, never worked around (`AGENTS.md`). A chat working on a game
in IED meets those bugs too - a miscompile, a missing Bedrock function, a tool that misbehaves - and today it can only
work around them, because it sees only its own workspace. When IED runs from its source checkout, the owner wants the
chat to be able to go and fix the bug where it lives, the way the sessions that build IED do.

## When it is on

- `developer.enabled` (off by default, any tier) can be turned on only when the server is built from a source checkout:
  either its home (the folder it found `shared/host-api` in, `IED::HomeFolder`) is one - it holds `.git`,
  `C4/C4Lib/src` and `src/nuke.toml` - or it is an install whose `install.json` names one as its `source` and that
  folder still is one. `nuke install-ied` writes that record (`docs/21` "Install shape"), so the desktop app the owner
  installed from the checkout can have developer mode too. Settings > Environment > Developer shows the switch only
  then; elsewhere it says developer mode needs IED built from its source checkout, and the settings search sends the key
  to that page rather than drawing a switch of its own (`docs/07`). An install from elsewhere, or with no record, offers
  nothing whatever the setting says.
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
   The tools' results name such paths with `ied:/` again (`fs_list`, `fs_glob`, `search_text`, a write's "saved"
   line), so they can be passed back as they are (2026-10-26).
   **The plugins' folders become writable** (2026-10-26, `docs/13` "Plugin folders"): without developer mode
   `plugins:/<plugin id>/` - a plugin's skills, docs and manifest - is read-only; with it a write there goes to the
   plugin's folder in `ied:/`: a server run from the checkout loads its built-in plugins from it, and an installed one's
   come from it (the root's `home: true`: `<home>/plugins/chat` is `ied:/plugins/chat`, so the change lands in the
   source, not in the install a reinstall replaces). The c4 plugin is the toolchain's copy `nuke build` makes from
   `ied:/plugins/c4/` (the built-in, and the source of each toolchain's copy) and `ied:/C4/docs/`; the section says to change those and rebuild.
2. **Permissions as the chat's mode says.** Writes into the source have the subject `fs.write(ied:/<path>)` and
   commands run there `shell(ied:/<command>)`, and they are decided like any other (`docs/11`): auto mode runs them
   without asking, acceptEdits writes and asks for commands, ask mode asks until the owner allows them - "Allow for this
   chat" on the card adds a rule naming the root (the file's folder, `fs.write(ied:/src/projects/**)`; a command's first
   word, `shell(ied:/nuke *)`) - and a deny rule still refuses. Turning developer mode on is the owner's say that a chat
   may change IED's source (owner, 2026-10-23: the point is that the AI fixes the language and IED and iterates on its
   own). A command needs the checkout trusted, as any workspace (`docs/11`); the owner's is.
3. **A system message section** (`docs/14` item 5, "IED itself"): this IED is built from source at `<path>`; when a
   failure is in the compiler, Bedrock, Nuke or IED rather than in the workspace, fix it there - read
   `ied:/AGENTS.md` and `ied:/C4/AGENTS.md` first, record a C4 bug in `ied:/C4/docs/status/known-bugs.md`, add a test
   that would have caught it, run `nuke build` and `nuke gates` in `ied:/`, and say in the chat what was fixed; when the
   running IED needs the fix, `ied_relaunch` installs it and the chat continues on the new build. And every failing
   tool call is a chance to fix IED's tools (owner, 2026-10-23: "any time it has a failing tool call it should either
   fix the tool call or update the system prompt for that tool to correct the confusion"): the chat finds out why the
   call failed; a tool that did something wrong is fixed in `ied:/`, and a call that was wrong because the tool's
   description or the system prompt misled it gets that text corrected in `ied:/` - the core tools' descriptions in
   `plugins/chat/code` (each tool's `Spec`), the system prompt in `plugins/chat/prompts`, a plugin's tools in its
   manifest or code, a plugin's skills in its `skills/` - then the call is made again. Since 2026-10-26 (owner: "when
   developer mode is enabled if the AI has a failing tool call then we should ALWAYS update the tool description system
   prompt skills or fix the tool so that this issues is never hit again") there is no exception for a failure the tool
   reported correctly, and the section says where the plugins' folders are written (item 1). About 500 tokens
   (`maxTokens` 600); only while developer mode is on.
4. **Tools** (a deferred group `ied`, loaded with `tool_search`, `docs/13`):
   - `ied_rebuild()`: `nuke build` in the source, the end of its output; a compiler build refreshes the `dev`
     toolchain, so the workspace's next build uses the fix at once when it pins `dev`.
   - `ied_gates()`: `nuke gates`, the C4 test suites, with a summary of what failed.
   - `ied_relaunch()`: `nuke install-ied` through the settings page's build (one at a time; a build already running is
     the one it waits for, and the page and the status bar show it), then `IED::RelaunchServer`: the desktop app, or
     else the server itself, relaunches onto the new install (`docs/27` "Relaunch"). Its result says IED relaunches in a
     few seconds and ends the turn, and it marks the chat (`IED::ToolResult::restarts`), so once IED is back the chat
     goes on by itself with IED's message "IED restarted onto its new build, as your call to ied_relaunch asked..."
     (`docs/12` "Restart behavior"). A build that fails installs nothing, relaunches nothing and gives the end of its
     log.
   - `ied_known_bugs()`: the open entries of `known-bugs.md`, so a chat checks whether a bug is known before filing it.

   The three long tools run on threads of their own (`ownThread`, `docs/26` section 8): before, a 10-minute
   `ied_gates` held the plugin's thread, and every `ied:/` path of every chat (the `ai.roots` binding runs there) failed
   after 30 s.

5. **A note on every failing call** (2026-10-26): the plugin contributes an `aiToolFailureNotes` entry (`docs/26`
   section 8; `when` as the section's), and the chat puts it after the result of each call that fails - an error of the
   call itself, arguments its schema refuses, a tool that does not exist - for the model only (the card shows the
   error alone): "[Developer mode] fs_read failed. Before you go on, change IED so that this cannot happen again, and say
   in the chat what you changed: fix the tool's code in ied:/ if it did something wrong; otherwise correct what misled
   your call - the tool's description or schema (a core tool's Spec in ied:/plugins/chat/code, a plugin's tool in its
   ied-plugin.json or its code), the system prompt (ied:/plugins/chat/prompts) or the skill you followed
   (plugins:/<plugin id>/skills/, which writes to its source in ied:/). Then make the call again." Not after a call the
   owner denied or a rule refused, one cut off by a stopped run, a command or build that ran and failed (its exit code is
   the result: `ToolOutcome::reportsFailure`, `IED::ToolResult::reportsFailure` for a plugin's tool such as
   `ied_gates`), or a write whose language server reports errors (`docs/24` "After an AI edit"). Server check
   `test_developer.py` checks the paths it names exist in the checkout.

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
- **Rebuild and relaunch** (the desktop app only) does the same, then, when that build succeeds, calls
  `window.iedDesktop.relaunch()` (`docs/27`) from the window that asked, whether or not its settings page is still
  open (`relaunchWhenBuilt`; a failed or canceled build relaunches nothing): the app records its windows and the workspace each shows, stops the
  server, starts the new install and opens the windows again. Every chat, build and terminal resumes from disk
  (`docs/12` restart rules). **Relaunch** alone is offered after a build that succeeded.
- **In a browser,** or a desktop app from before relaunch, the page says "Restart IED to use the new build: `ied stop`,
  then open IED" after a build instead.
- **The server owns the build** (`AGENTS.md` hard rule 2): the state lives in the plugin and the topic
  `ied.developer/state` carries it - the snapshot `{source, install: {commit, dirty, installedMs} | null, head, behind,
  build: {state, startedMs, endedMs, exitCode, lines}}`, the event `changed` (a new snapshot) and `build.lines`
  (`{lines}`, the lines added). A window closed during a build finds it, its log and its end when it opens again;
  `ied.developer/refresh` reads HEAD and `install.json` again (the page asks when it opens).
- A chat relaunches with `ied_relaunch` (above), the same build and the same relaunch; `ied_rebuild` builds
  (`nuke build`) and installs nothing.

## What it does not do

- It does not relaunch on its own: a chat calls `ied_relaunch`, or the owner clicks Rebuild and relaunch. Windows
  and their workspaces come back; what a window had open beyond its workspace comes back from the server's layouts.
- It does not commit or push. The owner reviews the change (the chat's Modified files list shows `ied:/` files too) and
  commits it.
- It never touches another checkout: `ied:/` is the source this server was built from, nothing else.

## Where it lives (the host stays generic)

Nothing here is in the host. Developer mode is a default plugin, `ied.developer` (`plugins/developer`), built on the
plugin API like the others, and the chat plugin learns of the source through a binding, not a dependency
(`AGENTS.md` hard rules 1 and 7):

- **Which server** (2026-10-26, `docs/01` "Processes"): the plugin's code runs in the master and in each workspace
  server (`runtime.servers`, `docs/26` "Servers"). The master's copy owns the install build (the settings page's
  `ied.developer/*` methods and topic, the `ied.developer.install` binding, marked `master`), so there is one build at
  a time whichever workspace asks; a workspace server's copy gives the chat its `ai.roots`, the section and the tools,
  and its `ied_relaunch` calls the master's `ied.developer.install` and `IED::RelaunchServer`, which relaunches the
  master and with it every workspace server.
- **`IED::OpenFolder(path, ws, problem)`** (new in `src/api/workspace.ch`, permission `workspace.open`): a plugin opens a
  folder as a workspace, or finds the open one rooted there - the same as the owner opening it, so its watcher, document
  store and plugins run. Generic: any plugin with the permission may use it. It opens the folder inside the calling
  plugin's workspace server, with the id `<wsId>.<n>` (developer mode's source in a chat of `w2` is `w2.1`): the chat's
  `ied:/` tools reach it in their own process, and it closes with that server.
- **`ai.roots`** (a binding the chat plugin consumes, optional; `kind: function`): `{wsId}` -> `{roots: [{alias, wsId,
  title, folder, confirm, home}]}`, other workspaces a chat of `wsId` may reach. A path `<alias>:/<path>` in a file tool
  or a `cwd` names `<path>` in that workspace; `confirm` makes every write and command there ask until a rule naming the
  root allows it, whatever the mode (the subjects `fs.write(<alias>:/<path>)`, `shell(<alias>:/: <command>)`). A root
  whose `folder` holds a plugin's folder takes the writes `plugins:/` refuses otherwise; `home` says the root is the
  source the server's home was built or installed from, so it holds the home's plugins at the same place (`docs/13`
  "Plugin folders"). The developer plugin provides it with one root, `{alias: "ied", title: "IED source", confirm:
  false, home: true}`, while developer mode is on; nothing is reachable otherwise. `plugins` is the chat's own alias,
  never a root's.
- **`aiToolFailureNotes`** (a contribution point, `docs/26` section 8): the plugin's code contributes item 5's note with
  the section's `when`.
- **`ied.developer.install`** (a binding the plugin provides and its own `ied_relaunch` consumes): `{}` ->
  `{state, exitCode, tail}` once `nuke install-ied` has ended; it waits as long as the build takes, so it is for a tool
  on a thread of its own.
- **`IED::RelaunchServer(reason, problem)`** (`src/api/relaunching.ch`, permission `server.relaunch`): the host's
  relaunch (`docs/27` "Relaunch"); generic, for any plugin that installs IED.
- **The system message section and the tools** are entries the plugin's code contributes when it activates
  (`PluginContext::Contribute`), only when the server runs from source, each with `when: config.developer.enabled`; the
  tools are deferred (`tool_search` loads them, group `ied.developer`).
- **The setting** is the plugin's `developer.enabled` (off by default; it has no effect without the source); its
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
6. Autonomy: off by default, the chat's mode decides in `ied:/`, `ied_relaunch` with the host's relaunch, and the chat
   going on after it.
