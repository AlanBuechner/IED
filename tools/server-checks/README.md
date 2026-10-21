# server-checks - what only the real server does

Black-box checks of an `ied-server` executable beyond the shared protocol suite (`tools/conformance`, which the mock
passes too): its access rules (`docs/11` "Access defaults", "Authentication on the wire") and file watching.
Each test starts its own server with `--test-hooks` and a data folder of its own, so they cannot leak state into each
other. Stdlib only; it reuses `tools/pylib/ws.py` and the conformance client for protocol calls.

```bat
python -I tools/server-checks/run_server_checks.py --server build\bin\Development-windows-x64\ied-server\ied-server.exe --cli build\bin\Development-windows-x64\ied\ied.exe
python -I tools/server-checks/run_server_checks.py --server <exe> -k lock -v
nuke conformance
```

| Module | Checks |
|---|---|
| `test_access.py` | a foreign Host refused on every path; loopback names accepted; `/healthz` open and terse; `/ws` without a session refused before the upgrade (401); `/api/*` 401 JSON; a page gets the login page (401) with the content policy, `nosniff` and `no-referrer`; only the login page's files are public; a foreign Origin refused on `/ws` (even with a session) and on auth POSTs; forged, other-port and empty cookies are no session |
| `test_tokens.py` | the `OPEN` line's 64-digit token; the exchange's 303 without the token and the cookie's attributes; a token works once; a token dies after its time (`testhooks.mintToken`); malformed tokens; the cookie opens a protocol session (`auth.devices`); `/api/auth/state`; neither `auth/devices.json` nor the log holds the cookie |
| `test_password.py` | login without a password (409); malformed logins (400); passwords have 12 characters; set then log in, devices listed, a wrong password gets no cookie; changing it needs the current one; five failures lock the address out, even for the right password, with `Retry-After`; the audit log's events in order and without secrets; `owner.json` holds an Argon2id string |
| `test_watching.py` | outside changes reach the client (created, changed, deleted, renamed); an outside write moves the revision so a stale `fs.write` conflicts; the server's own write, mkdir, rename and delete are each reported once; a clean open document reloads (origin `disk`), a dirty one gets `doc.diskChanged`, and revision 0 when its file is deleted; `.git/` is quiet; 300 files arrive in a few batches; created-then-deleted within a batch is nothing |
| `test_control.py` | one server per control pipe (a second exits 3); `status`; `url` signs in once; `open` of a file opens its folder once (any case), publishes `workspace.opened` and ends in `#open=`; JSON-RPC errors; `stop` says bye and ends the process. With `--cli`: `ied status` / `url` / `open --no-browser`, "no server runs", usage, and starting a server when none runs (under its own `LOCALAPPDATA`) then stopping it |
| `test_plugins.py` | declarative built-ins load and contribute; broken manifests fail with their reasons (schema pointer, API range, js kind, a builtin outside the install, not JSON), folders without a manifest are not plugins; an app plugin needs approval of its exact content and again after a change; an approved app plugin overrides the built-in of its id; disabling survives a restart; dependencies (in range, too old, missing, optional, on a failed plugin, cycles); plugin files served only from inside a loaded plugin, to a session |
| `test_trust.py` | a folder opens untrusted; trusting it is kept in app data across restarts (nothing written into the folder), and so is taking it back; both go to the security log |
| `test_settings.py` | the catalog from a plugin's schema (tiers, merge, page); defaults and the app file's values; `settings.set` keeping a file's comments, the solution tier winning, objects merged and lists appended, reset falling back; refusals (unknown key, a tier the key does not allow, a value its schema refuses with the problems, a stale revision, an unknown tier); hand edits live with problems on their lines and the last valid value kept; settings gone with their plugin and back; an app plugin appearing without a restart |
| `test_devices.py` | `auth.revoke` sends `bye auth-revoked`, closes the device's sockets (1008) and kills its cookie; logout clears the cookie and the session; device sessions survive a restart on the same port; a corrupt devices file stops the server; a non-loopback `--host` is refused until a password exists |
