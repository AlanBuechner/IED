# tools/dist - prebuilt releases on the owner's Gitea

One-command installs of the owner's programs on any of their machines, Windows or Linux, from their self-hosted Gitea
(`docs/21` "Prebuilt releases on the owner's Gitea"). The server's address is per machine, in the checkout's
`.ied/local/dist.json` (never committed):

```json
{"server": "http://<host>:<port>", "owner": "<user>", "repo": "IED"}
```

```bash
python -I tools/dist/ied_release.py                 # build HEAD for Windows and Linux, publish, check, make it latest
python -I tools/dist/publish.py commands ied        # the install commands for this server
python -I tools/dist/publish.py list ied            # published versions
python -I tools/dist/publish.py promote ied <ver>   # roll latest back (or forward)
```

On a machine:

```text
Windows (PowerShell):  irm <server>/api/packages/<owner>/generic/ied/latest/install.ps1 | iex
Linux:                 curl -fsSL <server>/api/packages/<owner>/generic/ied/latest/install.sh | sh
```

Run it again (or `ied-update`) to update. `DIST_VERSION=<version>` installs another version, `DIST_DIR` another
folder, `DIST_UNINSTALL=1` removes it; the installers' headers list the rest.

| File | What |
|---|---|
| `gitea.py` | the generic package registry: uploads, files, versions, links; server, owner and sign-in |
| `publish.py` | publishes a release folder (`release.json` + archives), points `latest` at it, prunes, rolls back |
| `install.ps1`, `install.sh` | the installers; `publish.py` fills in `@DIST_SERVER@`, `@DIST_OWNER@`, `@DIST_PRODUCT@` |
| `ied_release.py` | IED's release: worktree, Windows and Linux builds, packages, install checks |
| `tests/` | `python -I -m unittest discover -s tools/dist/tests` (a fake Gitea; the Windows installer end to end) |

Another program ships the same way: make its archives and a `release.json` (format in `publish.py`'s docstring) in a
folder, then `python -I tools/dist/publish.py --repo <its repo> publish <folder>`.
