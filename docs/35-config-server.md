# 35 - The config server

## Purpose

Owner, 2026-11-13: "a config server running on true nas that will serve settings for things like local AI models and
default plugins that will get auto downloaded off the config server and even have config server plugins so the config
server can do things like talk to my local model manager on my spark to auto update local model settings so i only need
to do so in one location ... we will also need some way to easaly deploy the server plugins for development." And: "the
server will also need to provide skills so shared information like how to deploy services on the nas can be shared."

One place holds what every IED of the owner's should share: the local models' profiles, default plugins, skills. Every
IED (desktop, laptop, a workspace on another machine) follows it, and server plugins keep it current from the services
that know (the model manager knows which models run and how).

## Shape

The config server is `ied-server` itself, given a config folder (`--config-root <folder>`): one program, one plugin
runtime, the same protocol code. On TrueNAS it runs as a Linux container (`docker/config-server/`, C4 D-86) with the
folder on a dataset. It serves:

| What | Where in the config folder | Clients get it as |
|---|---|---|
| Settings | `settings.json` (edited by hand or replaced through the API) and `published/<plugin>.json` (what server plugins publish) | the **Shared** tier, below the App tier (docs/07) |
| Client plugins | `plugins/<id>/` (plugin packages, docs/26) | the **shared** plugin scope: loaded like app plugins, below them |
| Skills | `skills/<name>/SKILL.md` and its files | shared skills, after the app's in the search order (docs/26 section 8) |
| Server plugins | `server-plugins/<id>/` (C4 plugins, `runtime.kind` "c4") | not sent: they run on the config server |
| Clients | `access.json` (token hashes, docs/11) | - |

A server plugin is an ordinary C4 plugin package (docs/26 section 2) loaded by the config server from
`server-plugins/` (the plugin host's `server` source); besides the plugin API it may call
`IED::Shared::Publish(key, valueJson, problem)` (`src/api/Shared.ch`), which sets a key of the served settings under the
plugin's name (`published/<id>.json`, so it survives restarts and redeploys), and `IED::Shared::Unpublish(key,
problem)`; `IED::Shared::IsConfigServer()` says whether they work. Elsewhere they fail with the problem "IED::Shared works
only on a config server". Published values come after `settings.json`'s, by the key's merge kind - the config server's
own settings catalog says it, and a key none of its plugins declares replaces: a hand-written value in `settings.json` is
the default a plugin's refines. Server plugins hot-reload as IED's do: a change under `server-plugins/<id>/` (a deploy,
or an edit on the NAS) compiles and starts it again; a build that does not compile leaves the running one in place
with the diagnostics as its problem. A server plugin's code runs in the master whatever its `runtime.servers` says and
starts when it loads (a config server opens no workspace, so `onWorkspace...` activations never come); a plugin of
another runtime kind fails with a problem saying so. Removing a server plugin removes what it published.

## The API (`/config/v1/`)

Every request carries `Authorization: Bearer <token>`. A **reader** token (a client IED) reads; an **admin** token
(the owner's deploy tooling) also writes. Tokens are random 32-byte values as 64 hex digits, printed once on stdout
when made (`ied-server --config-root <folder> --config-token reader|admin <name>`, which writes `access.json` and ends;
the server need not be stopped, it reads `access.json` again when it changes); the server keeps only their SHA-256 in
`access.json` (`{version: 1, tokens: [{name, role, sha256, createdMs}]}`) and compares digests in constant time. Making
a token again with a name replaces that name's token: the old one stops working at once. The security log
(`<data>/logs/security.log`) gets `config-token-made` (name and role, never the token) from the command, and
`config-deployed`, `config-removed` and `config-settings-replaced` with the admin token's name from the server.

Over the LAN the server listens with TLS when `--tls-identity` names a PKCS #12 file (its password in the environment
variable `IED_TLS_PASSWORD`, taken out of the environment at start), else plain HTTP; it then prints `https://` URLs.
The docs/11 rule holds: a server that listens beyond loopback needs credentials, here the tokens - a config server
starts beyond loopback without an owner password (its web client is then reached only with a launch token, which no
other machine gets, until one is set). The config API is no browser's: no session cookie, no Host or Origin check (a
page cannot send the header to another origin), served whatever name the server is reached by. A server without
`--config-root` answers `/config/` with 404.

| Method | Path | Answers |
|---|---|---|
| GET | `/config/v1/state[?since=<version>&wait=<s>]` | `{ version, settings, plugins: [{ id, version, hash }], skills: [{ name, hash }] }`; with `since` it waits (up to `wait`, 30 s when absent, at most 60 s) until the version differs, so a client follows changes with one open request |
| GET | `/config/v1/plugins/<id>/files` | `[{ path, size, hash }]` of the package |
| GET | `/config/v1/plugins/<id>/file/<path>` | the file's bytes |
| GET | `/config/v1/skills/<name>/files`, `/file/<path>` | the same for a skill folder (`server-plugins/...` only with an admin token) |
| PUT | `/config/v1/<kind>/<id>/stage/<path>` (admin) | one file of a package being deployed (`kind` plugins, skills or server-plugins), into `<kind>/.staging/<id>/`; `{path, size, hash}` |
| DELETE | `/config/v1/<kind>/<id>/stage` (admin) | drops what is staged for it (a deploy starts with it) |
| POST | `/config/v1/<kind>/<id>/commit` (admin) | the staged files, whose list (`[{ path, hash }]`) the body gives and the server checks - exactly those files with those SHA-256s, a plugin's `ied-plugin.json` with that `id` (a server plugin's `runtime.kind` "c4"), a skill's `SKILL.md` - replace the package: the staging folder renamed into place, the old one moved aside and removed; the deploy. `{kind, id, hash, previous, unchanged, version}` |
| DELETE | `/config/v1/<kind>/<id>` (admin) | removes it (a server plugin's published values with it) |
| GET | `/config/v1/server-plugins` (admin) | `{plugins: [{id, version, state, problems, code: {state, problem?, builds, building?}, diagnostics, published}], settingsProblem}`: each server plugin as the plugin host has it - loaded, its code compiled and runs (`building` while a build is queued or compiling) - its compile diagnostics, and what it published |
| GET, PUT | `/config/v1/settings` (admin) | `settings.json` as written; replaced (checked: a JSON object, comments allowed) |

Refusals are JSON, `{error, message, problems?}`: 401 `unauthenticated` (no token or an unknown one, with
`WWW-Authenticate: Bearer`), 403 `forbidden` (a reader's write), 400 `bad_request` (a package name that is not letters,
digits, `.`, `_` and `-`, a path with `..`, a drive, a backslash or a hidden `.staging`, a list that does not read, a
package that is not one), 404 `not_found`, 405, 409 `conflict` (staged files that are not the list, a package whose
folder cannot be moved because a file of it is open). A file is at most 16 MiB (the HTTP server's body limit).

`version` is a hash of everything served (the served settings and each package's id and hash); `hash` a package's: the
SHA-256 of every file's path, size and SHA-256 in path order (`src/configserver/ConfigPackages.cf`), which the server and
the clients compute alike. The server watches its folder: a change of anything served - a deploy, a hand edit, a
publish - makes a new version and wakes the waiting state requests; a change under `server-plugins/` reloads the server
plugins; a deploy's staging is never told. A `settings.json` that does not read is served as it last read, the problem
in `server-plugins`' `settingsProblem` and the log. Staging left by a stop is removed at the next start.

## The client

App setting `configServer.url` (`https://nas.lan:8780/`), `configServer.token` (`{ "env": "IED_CONFIG_TOKEN" }`: the
environment variable of the IED server that holds the reader token; docs/07: never in a settings file - `{ "secret":
"<name>" }` waits for the secret store, docs/11 "Secrets", and until then says so as the status) and
`configServer.certificate` (a PEM file of CA certificates trusted for the server's https besides the system's: a NAS's
own CA). All three are User settings a config server cannot set (`"x-ied-shared": false`). The master server (docs/01)
follows the config server with a waiting `/config/v1/state` request (25 s, then again) on a thread of its own
(`src/configserver/SharedMirror.cf`) and mirrors what it serves into `<app data>/shared/`: `shared/settings.json` (the
served settings, under a comment saying they are the config server's), `shared/plugins/<id>/`, `shared/skills/<name>/`;
each package whose copy is not whole and current is fetched into `shared/.staging/`, every file checked against its
SHA-256 and the whole against the package hash, then renamed into place, so a reader never sees half a package; a
package the server no longer serves is removed. A change lands as any edit of those files does: every server's app data
watcher (`src/host/Appdata.cf`) reads the Shared tier again and reloads the plugins, and the skills are found. A shared
plugin installed or removed is in the client's security log (`shared-plugin-installed`, `shared-plugin-removed`, with
the plugin, its hash and the server).

While the config server cannot be followed the mirror stays as it was and IED goes on with it; IED never waits for it
to start. The status is the `server` topic's `configServer` field (`{state, url, error?, version?, syncedMs?, plugins,
skills}`, `configServer.changed` when it changes) - `off` (no URL), `connecting`, `following`, `unreachable` (the
network, or an HTTP error), `refused` (401/403, or a token the settings cannot give) or `failed` (what it served did not
mirror) - a line in the IED output for each change, a warning notification when following stops (once, until it
follows again), and while it is in trouble a "Config server" warning in the status bar (its tooltip says why; a phone
has the notification). It tries again after 1 s, doubling to 30 s; a change of the app settings tries at once. Clearing
`configServer.url` stops following and keeps the mirror (delete `<app data>/shared/` to drop it).

Precedence:

- **Settings:** default < **Shared** < App < Solution < Project. The owner's own app setting wins over the shared
  one on that machine. The Shared tier may set what the App tier may, but for `"x-ied-shared": false` settings; a key no
  plugin of this machine declares is no problem there (the config server serves every machine), a value its schema
  refuses is. The settings window labels such a value's source "Shared". docs/07's tightening rule for
  security-relevant settings is not built for any tier yet; until it is, a config server can set `ai.permissions` like
  the owner's own file can.
- **Plugins:** built-in < **shared** < app = server < project. Shared plugins are trusted as the config server is: the
  owner configured it with a token, which is the approval (docs/11), and the security log records each install.
- **Skills:** project > plugin > app > **shared**.

## Deploying (development)

`tools/config-server/deploy.py` uploads a folder as a package:

```
python -I tools/config-server/deploy.py server-plugin path\to\models-plugin
python -I tools/config-server/deploy.py skill path\to\skills\nas-deploy --watch
python -I tools/config-server/deploy.py plugin path\to\plugin --server https://nas.lan:8780/ --token <admin token>
python -I tools/config-server/deploy.py server-plugin spark.models --remove
```

It reads the server from `--server`, else `IED_CONFIG_SERVER`, else `configServer.url` of this machine's app settings,
and the admin token from `--token`, else `IED_CONFIG_ADMIN_TOKEN` (`--cafile` or `IED_CONFIG_CAFILE` for an https
server whose certificate the system does not trust). A plugin's id is its manifest's, a skill's its folder's name
(`--id` names another). It clears the package's staging, stages the folder's files (skipping `.git`, `build`,
`__pycache__` and `*.pyc`), commits them with their list and, for a server plugin, waits for its new build to end and
prints its state, diagnostics and what it published - exit 1 when the build did not compile (the previous build runs
on). `--watch` uploads again on every change: edit, save, the plugin runs on the NAS a second later.

The owner's loop for a server plugin, once: on the NAS, `ied-server --config-root /mnt/tank/ied-config --config-token
admin deploy` (in the container: `docker run --rm -u 568:568 -v /mnt/tank/apps/ied-config:/data
ied-config-server:latest --config-token admin deploy`), and `reader <machine>` for each IED that follows it; on the desktop, `setx IED_CONFIG_ADMIN_TOKEN
<admin token>` and, in Settings > Environment, `configServer.url` and `configServer.token` `{ "env": "IED_CONFIG_TOKEN"
}` with `setx IED_CONFIG_TOKEN <reader token>` (IED's server reads the environment it starts with). Then `deploy.py
server-plugin <folder> --watch`.

The config server itself deploys to TrueNAS as a custom app (`docker/config-server/compose.yaml`; the owner's
NAS deploy tools install it) once the server builds for Linux (C4 D-86).

## As built (2026-11-13)

- Server: `src/configserver/` - `ConfigServer` (the folder, its watcher, the state and its versions, staging, commits,
  removals, published values), `ConfigApi` (`/config/v1/` routing and token checks, reached from `src/web/Front.cf`
  before the Host check), `ConfigAccess` (tokens), `ConfigPackages` (package files, hashes, names and paths),
  `SharedMirror` (the client); `--config-root`, `--config-token`, `--tls-identity` in `src/host/Config.cf` and
  `src/Main.cf`; the Shared tier in `src/settings/` (`TierFile::m_Shared`, `SettingInfo::m_Shareable`); the `shared`
  and `server` plugin sources in `src/plugins/`; `IED::Shared` in `src/api/Shared.ch`; the code summary's `building`
  (`src/plugins/Code.cf`); shared skills in `plugins/chat/code/prompts/Contributed.cf`; the status bar item in
  `frontend/src/shell/statusbar.js`.
- Checks: unit `server.configserver` (tokens, bearer headers, names and paths, package hashes, merging what is
  published, versions, the command line, the client's settings); server checks `test_config_server.py` (token auth:
  none, wrong, reader, admin, a token made while it runs, a name's token replaced, the security log; the long poll
  waking on a hand edit; a settings.json that does not read; deploys of a skill, a client plugin and a server plugin
  with `deploy.py`; a client following and mirroring them, the Shared tier in `settings.get`, the app tier overriding
  it, the shared plugin loaded with its setting; a server plugin's published value merged and reaching the client, its
  redeploy reloading it, a build that does not compile, its removal; commits refused for staged files that are not the
  list; `IED::Shared` refused on a server that is no config server; an unreachable config server leaving the mirror as
  it was, then followed once it is up on that port; a refused token and a secret the client cannot read); screenshots of
  the status bar's warning (desktop) and the notification (phone) for a config server that cannot be reached.

## Open items

- A client's own published values (a laptop's models) are not sent back: the config server is the one writer.
- The container (`docker/config-server/` - its Dockerfile runs `ied-server --config-root /data/config --app-data
  /data/app --data /data/state --host 0.0.0.0 --port 8780`, tokens made by running the image with `--config-token` - and
  `tools/config-server/image.py`, which makes its folder from a Linux build) needs the server's Linux build (C4 D-86);
  `ied config deploy` (the CLI's form of `deploy.py`) is not built.
- The secret store (`{ "secret": ... }`, docs/11) and docs/07's tightening rule for security-relevant settings.
