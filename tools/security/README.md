# security - the real server's access rules

Black-box checks of `docs/11` ("Access defaults", "Authentication on the wire") against an `ied-server` executable.
Each test starts its own server with `--test-hooks` and a data folder of its own, so they cannot leak state into each
other. Stdlib only; it reuses `tools/pylib/ws.py` and the conformance client for protocol calls.

```bat
python -I tools/security/run_security.py --server build\bin\Development-windows-x64\ied-server\ied-server.exe
python -I tools/security/run_security.py --server <exe> -k lock -v
nuke conformance
```

| Module | Checks |
|---|---|
| `test_access.py` | a foreign Host refused on every path; loopback names accepted; `/healthz` open and terse; `/ws` without a session refused before the upgrade (401); `/api/*` 401 JSON; a page gets the login page (401) with the content policy, `nosniff` and `no-referrer`; only the login page's files are public; a foreign Origin refused on `/ws` (even with a session) and on auth POSTs; forged, other-port and empty cookies are no session |
| `test_tokens.py` | the `OPEN` line's 64-digit token; the exchange's 303 without the token and the cookie's attributes; a token works once; a token dies after its time (`testhooks.mintToken`); malformed tokens; the cookie opens a protocol session (`auth.devices`); `/api/auth/state`; neither `auth/devices.json` nor the log holds the cookie |
| `test_password.py` | login without a password (409); malformed logins (400); passwords have 12 characters; set then log in, devices listed, a wrong password gets no cookie; changing it needs the current one; five failures lock the address out, even for the right password, with `Retry-After`; the audit log's events in order and without secrets; `owner.json` holds an Argon2id string |
| `test_devices.py` | `auth.revoke` sends `bye auth-revoked`, closes the device's sockets (1008) and kills its cookie; logout clears the cookie and the session; device sessions survive a restart on the same port; a corrupt devices file stops the server; a non-loopback `--host` is refused until a password exists |
