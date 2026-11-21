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

## The web page (`/config/`, 2026-11-21)

Owner, 2026-11-21: "can we get some web UI for the config server so i dont need to run commands and i can see what is
set up on the server. also add the url for the config srever web ui to the nas home."

The config server serves a page of its own at `/config/` (`frontend/config/`: `index.html`, `config.js`, its model
`configmodel.js`, `config.css`, on the theme's tokens and the ui-kit, which it serves at `/config/kit/` with the codicons at
`/config/icons/` - the page needs no IED session). Its files are public (they hold no data); everything it shows comes
from the API, as an admin:

- **Overview**: the server's version and when it started, its config folder, the state's version, how many settings,
  plugins, skills and server plugins it serves, and the IEDs that follow it - each reader token with when it was last
  used (a following IED asks every 25 s).
- **Settings**: the served settings, each key with where it comes from (`settings.json` or the server plugin that
  published it), and `settings.json` itself in an editor box with Save (a JSON object, comments allowed; the server's
  refusal shown).
- **Server plugins**: each with its version, state, code (active, failed, building, its builds), compile diagnostics and
  what it published; Remove.
- **Plugins** and **Skills**: each with its version or hash and its files (a skill's `SKILL.md` readable in place); Remove.
- **Tokens**: name, role, made, last used; New token (a name and a role) shows the token once with Copy, saying
  that a token made again under a name replaces the old one; Revoke, never the last admin; a one-time sign-in link for
  another browser.
- The page follows the state with the waiting state request, so a deploy, a hand edit or a server plugin's publish
  (SparkHub's models) shows as it happens. At phone width the sections are a row of tabs over one column.

**Signing in.** The first visit asks for an admin token. `POST /config/v1/session` (that token as Bearer, body
`{device}`) makes a new admin token for the browser, named `web-<device>-<6 hex>` (so it is listed, last used and revoked
like any other), and sets it as the cookie `ied-config` (`Path=/config/; HttpOnly; SameSite=Strict`, a year); the page
never sees it. `DELETE /config/v1/session` (Sign out) removes it. A token from that cookie is taken only with the header
`X-IED-Config: 1`, which another site's page cannot send without a CORS preflight the server never grants; a bearer
token needs no header. `POST /config/v1/links` (admin) makes a one-time sign-in link, `/config/?link=<32 random bytes
as hex>`, alive 10 minutes: opening it makes the browser's token and cookie and goes on to `/config/`; it works once.
`python -I tools/config-server/deploy.py signin [--open]` prints one (or opens it) with the admin token of this machine.
The security log gets `config-signed-in` (the new token's name and the token or link that signed in),
`config-token-made` and `config-token-revoked` (by whom).

| Method | Path | Answers |
|---|---|---|
| GET | `/config/v1/whoami` | `{name, role, cookie, serverVersion, startedMs, root}` |
| POST, DELETE | `/config/v1/session` | sign in (admin): `{name}` and the cookie; sign out |
| GET, POST | `/config/v1/tokens` (admin) | `{tokens: [{name, role, createdMs, lastUsedMs?}], you}`; `{name, role}` makes one: `{name, role, token}` |
| DELETE | `/config/v1/tokens/<name>` (admin) | revokes it; 409 for the last admin token |
| POST | `/config/v1/links` (admin) | `{url, expiresMs}` |

`lastUsedMs` is kept in memory: after a restart a token shows unused until it is used again.

## The client

App setting `configServer.url` (`https://nas.lan:8780/`), `configServer.token` (never the token itself, docs/07:
`{ "secret": "configServer" }`, the reader token in the master's secret store, docs/11 "Secrets", which the settings
page below puts there; or `{ "env": "IED_CONFIG_TOKEN" }`, the environment variable of the IED server that holds it) and
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
skills, served}`, `configServer.changed` when it changes) - `off` (no URL), `connecting`, `following`, `unreachable` (the
network, or an HTTP error), `refused` (401/403, or a token the settings cannot give) or `failed` (what it served did not
mirror) - a line in the IED output for each change, a warning notification when following stops (once, until it
follows again), and while it is in trouble a "Config server" warning in the status bar (its tooltip says why, a click
opens the settings page below; a phone has the notification). `served` is what the mirror holds,
`{settings: [{key, members?}], plugins: [id], skills: [name]}`: each shared setting with the member names of an object
value (`ai.models` lists the model profiles), so a page can say what is shared without knowing any plugin's settings.
It tries again after 1 s, doubling to 30 s; a change of the app settings or of the secret store tries at once, and so
does `configServer.retry` (a master method, `{}`). The waiting state request runs on a thread of its own: a change that
names another server, another token or none leaves it unanswered, so Connect and Disconnect take effect at once rather
than after its 25 s; and it waits only while the mirror follows, so a server back after a failure is said to be at its
first answer. Clearing `configServer.url` stops following and keeps the mirror (delete `<app data>/shared/` to drop
it).

### Settings > Environment > Config Server (2026-11-21)

Owner, 2026-11-21: "we should have some ui in the settings to set the config server so settings like the local models
will auto sync."

The shell's own page (`frontend/src/shell/configserverpage.js`, its model `configservermodel.js`), beside Keyboard,
edits the three `configServer.*` settings, always in the User tier whatever tier the window edits:

- **Status**, live from the `server` topic: a dot and a word for the state (Following green, Connecting blue,
  Cannot be reached / Refused / Could not mirror red with the error, Not following gray), the URL, when the last change
  was mirrored and the version's first digits, and what is shared - Settings (each key, an object's members after it:
  `ai.models 3: qwen3.8-flash-next@lan, ...`), Plugins, Skills; while not following, what IED keeps from its last
  mirror. Retry now while it is in trouble or connecting.
- **Connection**: Server URL (a bare host gets `http://`, the URL ends with `/`), Reader token (a password field that
  never shows a token: it says where the token is - "Stored on this machine" with Remove, an environment variable by
  name, or nothing yet), CA certificate, and Connect (Apply once a URL is set) and Disconnect. Connect stores a token
  typed in with `secrets.set {name: "configServer"}`, points `configServer.token` at it (`{ "secret": "configServer" }`,
  replacing an `{ "env" }`), writes the certificate and the URL - or, with the URL unchanged, asks `configServer.retry`.
  Enter in a field is Connect. Disconnect resets `configServer.url` (the token and the mirror stay); Remove deletes the
  stored token and resets `configServer.token` when it named it. A refusal shows under the buttons.
- Under it, how a reader token is made on the config server. At phone width the label column stacks over the fields.

The chain it serves, checked 2026-11-21 on the owner's machines: a model service added in SparkHub (the Spark's model
manager) reached the NAS config server through `spark.models` in 24 s, and the owner's running IED had it in its
settings half a second later; a change of its context window and its removal went the same way in 30 s - the time is
`spark.models.pollSeconds` (30), how often the plugin reads SparkHub.

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
ied-config-server:latest --config-token admin deploy`), and `reader <machine>` for each IED that follows it; on the
desktop, `setx IED_CONFIG_ADMIN_TOKEN <admin token>` for the deploy tooling, and on each IED Settings > Environment >
Config Server: the URL, the machine's reader token, Connect. Then `deploy.py server-plugin <folder> --watch`.

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
- 2026-11-21, the settings page: `src/auth/SecretStore.cf` and `Secrets.cf` (docs/11 "Secrets"), `MirrorToken` reading
  `{ "secret" }`, `MirroredJson` (`served`), `configServer.retry`, the abandoned and non-waiting state requests in
  `SharedMirror`; `frontend/src/shell/configserverpage.js` and `configservermodel.js`, the status bar item opening it.
  Checks: unit `server.configserver` `client_settings` (a token from the store), `secret_store` (round trip, the file
  never holding a value in the clear, a sealed value moved under another name refused, removal, names) and `mirrored`;
  frontend `configserver.test.js`; server checks `test_a_stored_token_is_followed_and_never_comes_back` (Connect's
  requests from refused to following, `served`, the token in no file, log or topic, `secret-set` in the security log,
  Remove refused at once, bad names and values) and `test_a_change_of_server_is_followed_at_once` (Disconnect while the
  state request waits: off within seconds; Connect again; a server back after the pause between tries grew, followed at
  once on Retry now - before, the first request after a failure waited its 25 s); e2e `config_server_page`.
- 2026-11-21, the web page: `frontend/config/` (`index.html`, `config.js`, `configmodel.js`, `config.css`);
  `ConfigApi` serves it and its `kit/` and `icons/`, takes the `ied-config` cookie with `X-IED-Config`, and answers
  `whoami`, `session`, `tokens`, `links`; `ConfigServer::MintToken`/`RevokeToken`/`TokensJson`, `ConfigAccess::RoleOf`/
  `Remove`/`ListJson`, `RevokeConfigToken`; `c_ContentPolicy` moved to `web/Front.ch`; `deploy.py signin [--open]`. The
  owner's NAS home (`nas-home`, a separate repository) links to it ("IED Config"). Checks: unit `server.configserver`
  `web_page` (browser token names, page file names, the last admin kept); `test_the_page_is_served_without_a_token`
  (its files public with the content policy, `/config` redirected, no file outside it, the API still asking for a token)
  and `test_a_browser_signs_in_and_manages_tokens` (a reader cannot sign in; the cookie's flags; the cookie refused without
  the header; `whoami`; tokens listed without digests, made and working at once, misnamed refused, revoked at once;
  a one-time link once; sign out; the last admin refused; the security log); frontend `configpage.test.js`; headless Edge
  over every section at desktop and phone width against a local config server running the real `spark.models`.

## Open items

- A client's own published values (a laptop's models) are not sent back: the config server is the one writer.
- The container (`docker/config-server/` - its Dockerfile runs `ied-server --config-root /data/config --app-data
  /data/app --data /data/state --host 0.0.0.0 --port 8780`, tokens made by running the image with `--config-token` - and
  `tools/config-server/image.py`, which makes its folder from a Linux build) needs the server's Linux build (C4 D-86);
  `ied config deploy` (the CLI's form of `deploy.py`) is not built.
- The secret store (`{ "secret": ... }`, docs/11) and docs/07's tightening rule for security-relevant settings.
