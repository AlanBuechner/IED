// websearch.test.js - Settings > Web > Search's pure parts (plugins/websearch, docs/29): what SearXNG's and Tor's states
// read as, the last search, an engine's test, and the engine list's edits.

import { test, assertEqual } from "./harness.js";
import { engineTestLine, lastSearchLine, searxLine, seconds, torLine, withEngine, withoutEngine } from "/plugins/ied.websearch/ui/model.js";

test("websearch: SearXNG's state as a line and a tone", () => {
	assertEqual(searxLine(undefined), { text: "", tone: "idle" }, "nothing said before the first snapshot");
	assertEqual(searxLine(null).tone, "bad", "the plugin's topic is gone");
	assertEqual(searxLine({ state: "ready", url: "http://127.0.0.1:5000" }), { text: "Running at http://127.0.0.1:5000", tone: "ok" });
	assertEqual(searxLine({ state: "starting", summary: "waiting for Tor: 45% Loading" }), { text: "Waiting for Tor: 45% Loading", tone: "busy" });
	assertEqual(searxLine({ state: "failed", error: "SearXNG is not installed" }), { text: "SearXNG is not installed", tone: "bad" });
	assertEqual(searxLine({ state: "stopped" }).tone, "idle");
	assertEqual(searxLine({ state: "external", url: "http://nas:8888" }).text, "Using your SearXNG at http://nas:8888");
});

test("websearch: Tor's state, and when it does not matter", () => {
	assertEqual(torLine({ state: "ready", socks: "socks5h://127.0.0.1:9050" }, true, false), { text: "Connected (socks5h://127.0.0.1:9050)", tone: "ok" });
	assertEqual(torLine({ state: "starting", progress: 45, summary: "Loading" }, true, false), { text: "Connecting: 45% Loading", tone: "busy" });
	assertEqual(torLine({ state: "ready" }, false, false).tone, "idle", "search does not use Tor");
	assertEqual(torLine({ state: "ready" }, true, true).tone, "idle", "the owner's SearXNG decides");
	assertEqual(torLine(undefined, true, false), { text: "", tone: "idle" }, "before the first snapshot");
	assertEqual(torLine(null, true, false).tone, "bad", "the Tor plugin is not running");
});

test("websearch: the last search and an engine's test", () => {
	assertEqual(lastSearchLine(null), "");
	assertEqual(lastSearchLine({ query: "q", results: 1, ms: 2500, retried: true, failed: [["google", "access denied"]] }),
		"Last search \"q\": 1 result in 2.5 s; tried again on new circuits; failed: google (access denied)");
	assertEqual(lastSearchLine({ query: "q", error: "Tor failed" }), "Last search \"q\" failed: Tor failed");
	assertEqual(engineTestLine({ name: "bing", results: 10 }), { text: "10 results", tone: "ok" });
	assertEqual(engineTestLine({ name: "bing", results: 0 }), { text: "no results", tone: "warn" });
	assertEqual(engineTestLine({ name: "google", results: 0, error: "access denied" }), { text: "access denied", tone: "bad" });
	assertEqual([seconds(850), seconds(7800)], ["850 ms", "7.8 s"]);
});

test("websearch: engines added once, lowercased, and removed", () => {
	assertEqual(withEngine(["brave"], " Bing "), ["brave", "bing"]);
	assertEqual(withEngine(["brave"], "brave"), ["brave"], "already there");
	assertEqual(withEngine(["brave"], "  "), ["brave"], "nothing typed");
	assertEqual(withoutEngine(["brave", "bing"], "brave"), ["bing"]);
});
