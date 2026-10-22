# 29 - Web search, web fetch and Tor

## Purpose

The AI's two web tools - `web_search` (T-WEB-1) and `web_fetch` (T-WEB-2) - and the services behind them: a SearXNG
the server runs itself and a Tor its searches go through, so the engines it asks see Tor exits instead of this machine,
and an exit they refuse is answered with new circuits. Owner direction (2026-10-21): "a tor router only for the search
to prevent rate limiting or bot detection"; web fetch next to web search; SearXNG configured in the settings window;
**search, fetch and Tor are plugins** - the host knows none of them. This resolves the SearXNG spike of `docs/21` and
narrows `docs/04`'s egress proxy for now: Tor carries search only, `web_fetch` goes direct.

## Three built-in plugins

| Plugin | Folder | Does | Provides | Consumes |
|---|---|---|---|---|
| `ied.tor` | `plugins/tor` | runs `tor.exe` on demand: a generated `torrc`, its control port, bootstrap progress, new circuits, an idle stop | bindings `tor.proxy`, `tor.newCircuit`, `tor.status`, `tor.stop`; topic `tor:status` | - |
| `ied.websearch` | `plugins/websearch` | runs SearXNG under the private Python, its `settings.yml` made from the settings; the `web_search` tool; Settings > Web > Search | bindings `websearch.search`, `websearch.status`; methods `websearch.status`, `.search`, `.testEngines`, `.catalog`, `.restart`; topic `websearch:status` | `tor.proxy`, `tor.newCircuit` (optional) |
| `ied.webfetch` | `plugins/webfetch` | fetches a URL looking like a browser, turns HTML into Markdown in C4, hands long pages out a window at a time; the `web_fetch` tool | binding `webfetch.fetch` | - |

They talk only through bindings (AGENTS.md rule 7): search asks Tor for its proxy, and any other plugin (a sandbox's
egress, a future fetch-through-Tor) can ask the same. Each is `runtime.kind: "builtin"`, its `code/` a source folder of
`ied-server` (`src/nuke.toml`), like the browser plugin. Disabling `ied.tor` with `websearch.useTor` on makes searches
fail with the reason (never a silent search from this machine's address); disabling `ied.websearch` or `ied.webfetch`
takes its tool away. Each plugin also adds one short system-message section naming its tool, and `ied.websearch` the
`web-research` skill (query wording, narrowing engines and dates, reading in windows, bot checks, citing sources).

## Tor (`ied.tor`)

**Program.** `tor.executable`, else `<home>/vendor/tor/tor/tor.exe` - the Tor Expert Bundle, `tools/setup/deps.toml`
package `tor`. `<home>` is `IED::HomeFolder()`: the checkout in development, the install folder once installed.

**Start on demand.** The first `tor.proxy` call starts it; the binding always answers at once with the state (a binding
call waits at most 30 s, a cold bootstrap can take longer), so a caller polls it. The `torrc` is written into the
plugin's state folder (`<data>/plugins/ied.tor/`) at each start, with an empty `--defaults-torrc` (the bundle's defaults
are Tor Browser's):

```
SocksPort 127.0.0.1:auto        ControlPort 127.0.0.1:auto     CookieAuthentication 1     AvoidDiskWrites 1
DataDirectory "<state>/data"    ControlPortWriteToFile "<state>/control-port"
GeoIPFile, GeoIPv6File "<bundle>/data/geoip[6]"                Log notice file <state>/tor.log
ExitNodes {de},{nl} + StrictNodes 1   (only with tor.exitCountries)
```

Path values are quoted (a `#` in a path is no comment); `Log`'s file takes the rest of its line, so spaces are fine.
A watcher thread of the plugin's own reads the control port from `control-port`, authenticates with the cookie, sends
`TAKEOWNERSHIP` (Tor exits when the connection closes, so it never outlives a dead server; the process is also in the
server's job), polls `GETINFO status/bootstrap-phase` until `PROGRESS=100` and asks `GETINFO net/listeners/socks` for
the SOCKS port. Measured: 15-25 s for a first start, 4-6 s with a warm `data` folder. A start not bootstrapped within
2 minutes, or a Tor that exits, is `failed` with the warnings of its log; a later call tries again after 5 s.

**New circuits.** `tor.newCircuit` sends `SIGNAL NEWNYM` and answers `{changed, waitSeconds}`; Tor takes one every 10 s.

**Idle stop.** With no `tor.proxy` call for `tor.stopAfterSeconds` (default 1800; 0: never) Tor stops; the next call
starts it again, on another SOCKS port, which search notices. A changed `torrc` (exit countries, the program) restarts it.

**Status.** `tor:status` (snapshot and `status` events), `tor.status`: `{state: "stopped"|"starting"|"ready"|"failed",
progress, summary, error?, socks?, host?, port?, executable?}`.

## SearXNG (`ied.websearch`)

**Spike result (2026-10-21).** SearXNG at the pinned commit runs on Windows under the embeddable CPython 3.12 with
waitress: every dependency has a `win_amd64` wheel (it speaks to engines with `curl_cffi`, SOCKS5 included); its only
POSIX import (`pwd`, in its Valkey error path) is stubbed by the launcher, which also sets the selector event loop
`curl_cffi` needs on Windows. Measured: SearXNG starts in 1-3 s; a JSON search takes 1-3 s direct and 3-8 s through Tor.

**What runs.** `<python> -I <plugin>/service/serve.py --searxng <dir> --packages <dir> --settings <file> --state <dir>
--port-file <file> --log <file>`: the launcher puts SearXNG and its packages on `sys.path`, points SearXNG's caches
(it uses the temp folder) into `<state>/cache`, gives SearXNG a secret key of its own per start (`SEARXNG_SECRET`),
binds waitress to `127.0.0.1:0` and writes the port it got to `--port-file` once SearXNG has started; its output goes to
`<state>/searxng.log`, whose ERROR and exception lines a failed start reports. `<python>` is `websearch.python`, else
`<home>/vendor/python/python.exe`; SearXNG is `websearch.searxngFolder`, else `<home>/vendor/searxng`; its packages are
always the `searxng-packages` folder beside it.

**Its settings.yml** is made from the settings at each start (`use_default_settings`, then):

- only the configured engines are loaded (`use_default_settings.engines.keep_only`), each turned on (`disabled: false`,
  `inactive: false`) - fewer engines start faster, and none makes requests at start; a configured name SearXNG's own
  `settings.yml` does not have is left out and named in the status;
- `server`: `limiter: false`, `public_instance: false`, `image_proxy: false`;
- `search`: `formats: [html, json]`, `safe_search` from `websearch.safeSearch`, no autocomplete, and short
  `suspended_times` (5-10 minutes after a CAPTCHA, "access denied" or 429, not hours: behind Tor the next circuit often
  gets through);
- `outgoing`: `request_timeout` from `websearch.timeoutSeconds`, and with Tor `proxies: {all://: socks5h://...}`.
  SearXNG's own Tor check (`using_tor_proxy`) stays off: the proxy is the Tor plugin's, and the check's request through
  a fresh circuit failed SearXNG's start in testing.

Every search compares the text it would write with the running one's: a changed setting, Tor back on another port or a
new circuit asked for restarts SearXNG first (1-3 s), so settings apply on the next search with no listener.

**Circuits of its own.** The proxy URL carries a SOCKS user, `ied<n>`; Tor isolates streams by SOCKS credentials, so a
new `n` means new circuits (other exits) for SearXNG even over its kept-alive connections.

**Searching.** `GET /search?q=...&format=json&engines=<list>&pageno=&safesearch=&time_range=&language=` on the loopback
port. The engines are always named, so engines outside the "general" category (`github`, `stackoverflow`) take part.
SearXNG merges and ranks the results (an URL several engines found ranks higher). Two retries:

- when engines refused this exit (CAPTCHA, access denied, too many requests) and the search came back with almost
  nothing, or half the engines asked refused, and `websearch.newCircuitOnBlock` is on: once more on new circuits
  (`tor.newCircuit` and the next SOCKS user);
- when no engine answered at all (timeouts: the first circuits after Tor starts are slow): once more as it is.

**Behind Tor, measured (2026-10-21).** Which engines answer depends on the exit: Google refused every exit tried;
DuckDuckGo, Brave and Wikipedia refused some and answered others; Bing, Yahoo, GitHub and Stack Overflow mostly answered.
Bing localizes results by the exit's country, and Yahoo serves Bing's results, so asking both ranks Bing's (sometimes
poor) results first: the default engines leave Yahoo out. **Test engines** on the settings page shows the current exit's.

**Idle stop.** With no search for `websearch.stopAfterSeconds` (default 1800; 0: never) SearXNG stops; a watcher thread
also notices a SearXNG that died.

**Another instance.** `websearch.instanceUrl` (`http://host:port`) uses a SearXNG the owner runs instead: nothing is
started and Tor is that instance's business. It must have `json` in `search.formats`. Plain http only (Bedrock's HTTP
client has no TLS): a LAN or loopback instance.

**Status.** `websearch:status`: `{state: "stopped"|"starting"|"ready"|"failed"|"external", url?, summary?, error?,
viaTor, engines, unknownEngines, lastSearch?: {query, results, ms, retried, error?, failed: [[engine, reason]]}}`.

**Engine catalog.** `websearch.catalog` reads SearXNG's own `searx/settings.yml` with Bedrock's YAML reader: each
engine's name, shortcut, categories and whether SearXNG ships it off, for the settings page's picker.

### `web_search`

```
web_search(query, engines?: [string], count?: 1-20, page?: 1-10, time_range?: day|week|month|year, language?)
```

Class `net` (asks in `ask` permission mode, like the browser's tools), 180 s. The result names the engines asked and
whether it went through Tor, then the results numbered - title, URL, snippet, the engines that found it, a date when one
gave it - then SearXNG's answers and suggestions, the engines that failed with SearXNG's reason (`google (access
denied)`) so the model knows the list is partial, and how to get more (`page=2`, `web_fetch(url)`). `count` defaults to
`websearch.results` (8). An engine that is not configured is refused with the configured ones named.

## Fetch (`ied.webfetch`)

```
web_fetch(url, format?: markdown|text|raw, start?: >=0, max_chars?: 1000-100000)
```

Class `net`, 120 s. http and https only; an address without a scheme gets `https://`.

**Transport.** Looking like a browser, because bot checks refuse what does not: the private Python runs
`<plugin>/service/fetch.py`, whose `curl_cffi` (installed with SearXNG's packages) impersonates `webfetch.impersonate`
(Chrome by default: its TLS and HTTP/2 fingerprint and headers). Measured: Medium and npm, which answer Windows' curl
with Cloudflare's 403, answer it with the page. Without the private Python or `curl_cffi`, or with `webfetch.impersonate`
off, Windows' own `%SystemRoot%\System32\curl.exe` (`webfetch.curl` overrides) runs with `webfetch.userAgent` and a
browser's `Accept` headers: `-sS -L --globoff --compressed --max-redirs 8 --proto =http,https --proto-redir =http,https
--max-time --max-filesize`. Both write the body to standard output and a report - status, content type, final URL, exit
code, message - to standard error after a marker (curl through `--write-out "%{stderr}..."`), so one reader serves both.
A body past `webfetch.maxBytes` (5 MB) is cut there and said to be. Direct: no Tor (owner).

**Bot checks.** A page that is one (Cloudflare's "Just a moment...", "Attention Required", a CAPTCHA, "Prove your
humanity") comes back as an error naming it and suggesting `browser_open`, which runs a real browser. Stack Overflow's
JavaScript challenge stops every fetcher that is not one.

**Conversion.** HTML becomes Markdown in C4 (`code/html.cf`): a tolerant tokenizer (tags, attributes, comments,
character references - named, decimal, hex, the legacy ones without `;` - and raw text in `title` and `textarea`;
`script` and `style` leave nothing), then a writer that keeps headings, paragraphs, line breaks, lists (nested,
numbered from `start`, items closed by the next), links made absolute against the final URL or `<base href>` (same-page
`#` links stay text), images as their alt text (`[image: ...]`: the model sees no images through `web_fetch`, and their
addresses are long), `pre` as fenced code with its `language-x` class, inline `code`, emphasis, block quotes, tables as
pipe rows, and rules. Marks are held until text follows, so an empty element leaves none. Left out with everything
inside: `head`, `nav`, `aside`, `footer`, `noscript`, `template`, `svg`, `math`, `iframe`, `button`, `select`, forms'
`textarea`, elements marked `hidden` or `aria-hidden`, and page chrome - an ARIA role of navigation, menu, search,
banner, complementary, contentinfo, toolbar or dialog, or a class or id word such as nav, menu, sidebar, footer,
breadcrumb, cookie, consent, banner, share, dropdown, portlet, noprint (never on `html`, `body`, `main`, `article`).
When the page has a `<main>`, only the first one is written; else its only `<article>`. `format: text` writes the same
without Markdown marks; `raw` gives the body as received. A body declared `iso-8859-1`/`windows-1252` (its type or a
`<meta>`), or not UTF-8 and saying nothing, is read as Windows-1252. JSON, XML and other `text/*` come back raw;
anything else (PDF, images, archives) is refused with its type and size, and a hint (`browser_open` shows PDFs).
Pages drawn by scripts (GitHub's repository README, MDN's compatibility tables) have little or no text: the result says
so, and `browser_open` renders them.

**Paging.** The text is returned from character `start`, at most `max_chars` (default `webfetch.maxChars`, 20,000),
ending with `[Characters a-b of n: web_fetch with start=b gives the next ones.]`. The last 16 pages fetched are kept
for 10 minutes, per URL and format, so reading on fetches once.

**Result.** A header (`URL`, `Status`, `Type`, `Title`, `Length` in characters, a note when the body was cut) and the
text. A status of 400 and up is an error result that still carries the first 2,000 characters.

## Settings (all app tier)

SearXNG and Tor are one per server, so a solution cannot ask for other engines or for no Tor.

| Key | Default | Page |
|---|---|---|
| `websearch.engines` | `["brave", "duckduckgo", "google", "bing", "wikipedia", "stackoverflow", "github"]` | Web > Search (its own editor) |
| `websearch.useTor` | `true` | Web > Search |
| `websearch.results` | `8` | Web > Search |
| `websearch.safeSearch` | `"off"` (`moderate`, `strict`) | Web > Search |
| `websearch.language` | `"auto"` | Web > Search |
| `websearch.timeoutSeconds` | `8` | Web > Search |
| `websearch.newCircuitOnBlock` | `true` | Web > Search |
| `websearch.stopAfterSeconds` | `1800` | Web > Search |
| `websearch.instanceUrl` | `""` | Web > Search |
| `websearch.python`, `websearch.searxngFolder` | `""` | Web > Search |
| `tor.executable` | `""` | Web > Tor |
| `tor.exitCountries` | `[]` | Web > Tor |
| `tor.stopAfterSeconds` | `1800` | Web > Tor |
| `webfetch.maxChars` | `20000` | Web > Fetch |
| `webfetch.maxBytes` | `5242880` | Web > Fetch |
| `webfetch.timeoutSeconds` | `30` | Web > Fetch |
| `webfetch.impersonate` | `"chrome"` (`edge`, `safari`, `firefox`, `off`) | Web > Fetch |
| `webfetch.userAgent` | a desktop Chrome's (curl only) | Web > Fetch |
| `webfetch.curl` | `""` | Web > Fetch |

**The Search page** (`settingsPages` of `ied.websearch`, view `SearchSettings`, every `websearch.*` key;
`plugins/websearch/ui/`): SearXNG's and Tor's state live from their topics (ready, starting with Tor's bootstrap
percentage, failed with the reason), the last search (results, time, a retry, the engines that failed), **Restart
SearXNG** and **New circuits**; the engines (each with its categories from SearXNG's catalog; add one from the catalog,
remove one) and **Test engines**, which searches with all of them and shows what each found or why it failed; **Try a
search**, the results as the AI gets them; then the settings window's own rows for the other keys (`ctx.settings.row`,
a generic addition to the settings page API, `docs/07`). Web > Fetch and Web > Tor are the generic rows.

## Install

- `setup.bat` (group `ied`, installed by default) installs SearXNG's source (`searxng`), pip (`pip`, a pinned wheel),
  SearXNG's Python packages (`searxng-packages`, a package of kind `pip`: `pip install --target vendor/searxng-packages
  --require-hashes --only-binary=:all: --no-deps` from `tools/setup/searxng-requirements.txt`, every package pinned with
  its `win_amd64`/`cp312` wheel's hash; installed again when that file changes) and the Tor Expert Bundle (`tor`).
- `nuke install-ied` copies `vendor/python`, `vendor/searxng/searx`, `vendor/searxng-packages` and `vendor/tor` into the
  install when they are present, and says which are missing.
- Without them the tools still load and say what is missing ("SearXNG is not installed: run setup.bat ..., or set
  websearch.instanceUrl"); `web_fetch` falls back to Windows' curl.

## As built (2026-10-21)

Checked in the app (a private server, a scripted model): a chat's `web_search` (approval card, results through Tor from
a stopped Tor in 10 s) and `web_fetch` of the docs page it found (0.4 s), at desktop and phone width; the Search page's
live state through a cold Tor bootstrap, Test engines, New circuits (SearXNG back in 1 s on other exits), Try a search,
a row's edit written to the app tier; Web > Tor and Web > Fetch. `web_fetch` read docs.rs, MDN, Wikipedia, GitHub's API
(JSON), refused a PDF, and named Stack Overflow's bot check. Tests: 34 C4 unit tests beside the plugins' code (torrc and
control replies; settings.yml read back with Bedrock's YAML reader; the search URL; SearXNG's JSON; the model's text;
character references, the tokenizer, URL resolution, Markdown and text conversion, chrome, charsets, windows, the two
fetchers' command lines and report), `frontend/tests/websearch.test.js`, and the server check
`test_a_binding_through_its_own_binding_reaches_another_plugin`.

Found and fixed on the way, in the host: a plugin's binding calling the plugin's own binding (which runs at once, on the
plugin's thread, with the server lock released) left the thread marked as holding the lock, so a binding of another
plugin called from inside it waited on the lock's condition without owning the lock, and the server hung for good. The
host now clears the mark wherever it releases the lock for plugin code (`CodeUnlocked`, `src/plugins/code.ch`).

## Licenses

SearXNG (AGPL-3.0) runs unmodified as its own process; the launcher and the fetcher are IED's. Tor (BSD-3), curl_cffi
(MIT) and the other packages likewise. `docs/21` lists them.

## Open items

- Fetch through Tor (opt-in per call or per solution) and the general egress policy of `docs/04` - not wanted now.
- PDF to text for `web_fetch`.
- A `summary` format for `web_fetch` (the `summarizer` role, `docs/13`).
- A readability-style score for pages with neither `<main>` nor one `<article>` (today: the whole body without chrome).
