# cli/ - the `ied` command line

`ied.exe` opens folders and files in IED from a terminal or Explorer, and asks the running server about itself. It
talks to the user's `ied-server` over the control pipe (`docs/10` "Local control channel"), never HTTP, and starts the
server when none runs.

```bat
ied                      open this folder
ied src\main.cf:42:5     open a file at a line and column
ied status               the running server: version, pid, URL, workspaces, clients
ied url                  a URL that signs a browser in once
ied stop                 stop the server (returns once it has)
```

Options: `--no-browser` prints the URL instead of opening it; `--control <name>` talks to another server's pipe (tests).

| File | What |
|---|---|
| `command` | the command line read into a `Command` (a Windows drive's colon is not a line number) |
| `client` | JSON-RPC lines over `Net::LocalStream`, finding and starting `ied-server.exe`, waiting for it to go |
| `main` | the program |
| `tests/` | unit tests (`nuke test ied`); the black-box checks are in `tools/server-checks/tests/test_control.py` |
