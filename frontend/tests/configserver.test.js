// configserver.test.js - Settings > Environment > Config Server's model (docs/35): the state's words and accent, where the
// reader token comes from, the URL as configServer.url keeps it, how long ago, and what the config server shares.

import { test, assertEqual, assertThrows } from "./harness.js";
import { STALE_SERVER, ago, keptNote, normalizeUrl, servedSections, shortVersion, stateLook, storeTrouble, tokenSource } from "../src/shell/configservermodel.js";

test("config server: where a token is kept, and an older server", () => {
	assertEqual(storeTrouble(null), STALE_SERVER, "a server with no secret store predates the page: restart it");
	assertEqual(STALE_SERVER.includes("ied stop"), true);
	assertEqual(storeTrouble({ available: true, protection: "owner-only", secrets: [] }), "");
	assertEqual(storeTrouble({ available: false, problem: "no store here" }), "no store here");
	assertEqual(keptNote({ protection: "owner-only" }).includes("readable by your user alone"), true, "Linux");
	assertEqual(keptNote({ protection: "dpapi" }).includes("encrypted for your Windows user"), true, "Windows");
});

test("config server: states", () => {
	assertEqual([stateLook({ state: "following" }).label, stateLook({ state: "following" }).accent], ["Following", "saved"]);
	assertEqual(stateLook({ state: "refused" }).trouble && stateLook({ state: "unreachable" }).trouble && stateLook({ state: "failed" }).trouble, true, "the troubles");
	assertEqual([stateLook({ state: "connecting" }).accent, stateLook({ state: "connecting" }).trouble], ["info", false]);
	assertEqual(stateLook(null).label, "Not following", "no status is off");
});

test("config server: where the token comes from", () => {
	const listed = { available: true, secrets: [{ name: "configServer", setMs: 1 }] };
	assertEqual(tokenSource({ secret: "configServer" }, listed), { kind: "secret", name: "configServer", stored: true });
	assertEqual(tokenSource({ secret: "other" }, listed), { kind: "secret", name: "other", stored: false }, "named but not stored");
	assertEqual(tokenSource({ env: "IED_CONFIG_TOKEN" }, listed), { kind: "env", variable: "IED_CONFIG_TOKEN" });
	assertEqual(tokenSource(null, null), { kind: "none" });
	assertEqual(tokenSource({}, listed), { kind: "none" });
});

test("config server: the URL kept", () => {
	assertEqual(normalizeUrl(" http://nas.lan:8780 "), "http://nas.lan:8780/");
	assertEqual(normalizeUrl("nas.lan:8780"), "http://nas.lan:8780/", "a bare host is http");
	assertEqual(normalizeUrl("https://nas.lan:8780/"), "https://nas.lan:8780/");
	assertEqual(normalizeUrl(""), "");
	assertThrows(() => normalizeUrl("ftp://nas.lan/"), Error, "http or https");
	assertThrows(() => normalizeUrl("http://"), Error, "a URL names a server");
});

test("config server: ago and versions", () => {
	const now = 1800000000000;
	assertEqual([ago(now - 1000, now), ago(now - 12000, now), ago(now - 4 * 60000, now), ago(now - 3 * 3600000, now)], ["just now", "12 s ago", "4 min ago", "3 h ago"]);
	assertEqual(shortVersion("3f2a1c9e01abcdef"), "3f2a1c9e01");
	assertEqual(shortVersion(""), "");
});

test("config server: what it shares", () => {
	const sections = servedSections({ served: { settings: [{ key: "ai.models", members: ["a@lan", "b@lan"] }, { key: "editor.fontSize" }], plugins: [], skills: ["nas-deploy"] } });
	assertEqual(sections.map((s) => s.id), ["settings", "plugins", "skills"]);
	assertEqual(sections[0].items, [{ name: "ai.models", members: ["a@lan", "b@lan"] }, { name: "editor.fontSize", members: null }]);
	assertEqual([sections[1].items.length, sections[2].items[0].name], [0, "nas-deploy"]);
	assertEqual(servedSections({}).every((s) => s.items.length === 0), true, "an older server says nothing of it");
});
