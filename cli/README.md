# cli/ - the `ied` command line

`ied.exe` opens folders and files in IED from a terminal or Explorer, and asks the running server about itself. It
talks to the user's `ied-server` over the control pipe (`docs/10` "Local control channel"), never HTTP, and starts the
server when none runs. A folder or file opens in the desktop app when `ied-desktop.exe` sits beside `ied.exe`
(`docs/27`), else in the default browser.

```bat
ied                      open this folder
ied src\main.cf:42:5     open a file at a line and column
ied status               the running server: version, pid, URL, workspaces, clients
ied url                  a URL that signs a browser in once
ied stop                 stop the server (returns once it has)
```

Options: `--browser` opens the default browser even when the desktop app is there; `--no-browser` prints the URL
instead of opening anything; `--control <name>` talks to another server's pipe (tests).

| File | What |
|---|---|
| `command` | the command line read into a `Command` |
| `main` | the program |
| `tests/` | unit tests (`nuke test ied`); the black-box checks are in `tools/server-checks/tests/test_control.py` |

The control channel's client, finding and starting `ied-server.exe`, locations (a Windows drive's colon is not a line
number) and the default browser are the `launcher` module's (`launcher/`), shared with `ied-desktop`.
