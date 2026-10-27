// router.test.js - a tab's sockets (ws/router.js, docs/10 "Routing"): which server a request or a topic goes to, one
// socket per workspace server through the master's /w/<wsId>/ws, the state the shell shows while a workspace server
// restarts, and the workspace servers' look in the status bar, its banner and the start page (shell/servers.js).

import { test, assertEqual, assertTrue } from "./harness.js";
import { Router, routeOf, topicWorkspace, servingWorkspace, combinedState } from "../src/ws/router.js";
import { serverLook, serverTitle, upFor, serverBanner } from "../src/shell/servers.js";
import { openState } from "../src/shell/workspaces.js";

const ROUTES = { methods: new Set(["ws.restart", "ai.usage", "auth.devices"]), topics: new Set(["server", "aiusage"]) };

test("router: the master's routes go to the master, whatever is shown", () => {
	assertEqual(routeOf(ROUTES, "w2", { method: "ws.restart", params: { wsId: "w2" } }), "master", "the master's method even with a wsId");
	assertEqual(routeOf(ROUTES, "w2", { topic: "aiusage" }), "master");
	assertEqual(routeOf(ROUTES, "w2", { topic: "server" }), "master");
});

test("router: a request naming a workspace goes to its server, a topic holding one too", () => {
	assertEqual(routeOf(ROUTES, "w1", { method: "fs.read", params: { wsId: "w2", path: "a.txt" } }), "w2");
	assertEqual(routeOf(ROUTES, "w1", { topic: "settings:w3" }), "w3");
	assertEqual(routeOf(ROUTES, "w1", { topic: "layout:w2:phone" }), "w2");
	assertEqual(routeOf(ROUTES, "w1", { topic: "out:w2:build" }), "w2");
	assertEqual(routeOf(ROUTES, "w1", { method: "doc.open", params: { wsId: "w2.1", path: "src/a.cf" } }), "w2", "a folder a plugin opened is its server's");
});

test("router: anything else goes to the shown workspace's server, or with none shown to the master", () => {
	assertEqual(routeOf(ROUTES, "w2", { method: "chat.send", params: { chatId: "c-12" } }), "w2");
	assertEqual(routeOf(ROUTES, "w2", { topic: "chat:c-12" }), "w2");
	assertEqual(routeOf(ROUTES, "w2", { topic: "doc:d4" }), "w2");
	assertEqual(routeOf(ROUTES, null, { method: "settings.get", params: {} }), "master");
	assertEqual(routeOf(ROUTES, null, { topic: "settings:app" }), "master", "settings:app names no workspace");
});

test("router: topic keys and workspace ids", () => {
	assertEqual(topicWorkspace("ws:w12"), "w12");
	assertEqual(topicWorkspace("chat:w1x"), null, "only a whole part that is a wsId");
	assertEqual(topicWorkspace("server"), null);
	assertEqual(topicWorkspace("ied.developer/state"), null);
	assertEqual(servingWorkspace("w3.2"), "w3");
	assertEqual(servingWorkspace("w3"), "w3");
});

test("router: the shell shows the master's state, then the shown workspace's socket's", () => {
	assertEqual(combinedState("reconnecting", "live"), "reconnecting");
	assertEqual(combinedState("live", "reconnecting"), "reconnecting");
	assertEqual(combinedState("live", null), "live", "no workspace shown");
	assertEqual(combinedState("live", "live"), "live");
});

class Socket {
	constructor(url) {
		this.url = url;
		this.sent = [];
	}
	send(text) {
		this.sent.push(JSON.parse(text));
	}
	close() {
		this.onclose && this.onclose({});
	}
	open() {
		this.onopen && this.onopen({});
	}
	deliver(frame) {
		this.onmessage && this.onmessage({ data: JSON.stringify(frame) });
	}
}

function timers() {
	return { setTimeout: () => 0, clearTimeout: () => {}, setInterval: () => 0, clearInterval: () => {}, now: () => 0 };
}

function router() {
	const sockets = [];
	const left = [];
	const r = new Router({
		base: "ws://ied.test",
		identity: { clientId: "c-1", deviceId: "d-1", deviceClass: "desktop" },
		openSocket: (url) => {
			const socket = new Socket(url);
			sockets.push(socket);
			return socket;
		},
		timers: timers(),
		random: () => 0.5,
		onLeave: (reason) => left.push(reason),
	});
	r.start();
	const live = (socket, epoch) => {
		socket.open();
		socket.deliver({ t: "welcome", proto: 1, serverVersion: "t", sessionEpoch: epoch || "e-1", clientId: "c-1" });
	};
	return { r, sockets, left, live, at: (path) => sockets.filter((s) => s.url === "ws://ied.test" + path) };
}

test("router: the master's socket first, a workspace's opened when it is shown, sent where they belong", () => {
	const env = router();
	assertEqual(env.sockets.map((s) => s.url), ["ws://ied.test/ws"]);
	env.live(env.sockets[0]);
	env.r.setRoutes({ methods: ["ws.restart"], topics: ["server"] });
	env.r.show("w2");
	const [w2] = env.at("/w/w2/ws");
	assertTrue(Boolean(w2), "the shown workspace's server's socket through the master");
	env.live(w2, "e-ws");
	env.r.request("fs.list", { wsId: "w2", path: "" });
	env.r.request("ws.restart", { wsId: "w2" });
	env.r.subscribe("chat:c-1", {});
	assertEqual(w2.sent.filter((f) => f.t === "req").map((f) => f.method), ["fs.list"]);
	assertEqual(env.sockets[0].sent.filter((f) => f.t === "req").map((f) => f.method), ["ws.restart"]);
	assertEqual(w2.sent.filter((f) => f.t === "sub").map((f) => f.topics), [["chat:c-1"]]);
});

test("router: while the shown workspace's server restarts the master stays live and the state says reconnecting", () => {
	const env = router();
	const states = [];
	env.r.onState((s) => states.push(s));
	env.live(env.sockets[0]);
	env.r.show("w1");
	const [w1] = env.at("/w/w1/ws");
	env.live(w1);
	assertEqual(env.r.state, "live");
	w1.close();
	assertEqual(env.r.state, "reconnecting", "the workspace's socket is down");
	assertEqual(env.r.master.state, "live", "the master's is not");
	assertEqual(states[states.length - 1], "reconnecting");
});

test("router: a closed workspace's socket goes; a bye auth-revoked on any socket leaves", () => {
	const env = router();
	env.live(env.sockets[0]);
	env.r.show("w1");
	const [w1] = env.at("/w/w1/ws");
	env.live(w1);
	env.r.forget("w1");
	assertTrue(!env.r.workspaceClient("w1"), "forgotten");
	env.r.show("w3");
	const [w3] = env.at("/w/w3/ws");
	env.live(w3);
	w3.deliver({ t: "bye", reason: "auth-revoked" });
	assertEqual(env.left, ["auth-revoked"]);
});

test("servers: the status bar's look, the tooltip and how long a server is up", () => {
	assertEqual(serverLook("ready").text, "Server");
	assertEqual(serverLook("failed").icon, "error");
	assertEqual(serverLook("restarting").icon, "sync");
	assertEqual(upFor(0, 30000), "under a minute");
	assertEqual(upFor(0, 5 * 60000), "5 min");
	assertEqual(upFor(0, 125 * 60000), "2 h 5 min");
	const title = serverTitle("Game", { state: "ready", pid: 42, startedMs: 0, restarts: 2, log: "C:/logs/workspace-1.log" }, 60000);
	assertTrue(title.includes("pid 42") && title.includes("restarted 2 times") && title.includes("workspace-1.log"), title);
});

test("servers: the banner while the shown workspace's server restarts or failed, with Restart", () => {
	const restarted = [];
	const shell = (state, problem) => ({
		wsId: "w1",
		workspaceServer: () => ({ state, problem, log: "C:/logs/w.log" }),
		restartWorkspaceServer: () => restarted.push("w1"),
	});
	assertEqual(serverBanner(shell("ready")), null);
	assertEqual(serverBanner(shell("starting")), null, "a first start is the connection banner's");
	const restarting = serverBanner(shell("restarting", "its last run ended: code 1"));
	assertTrue(restarting.textContent.includes("restarting") && restarting.textContent.includes("code 1"), restarting.textContent);
	const failed = serverBanner(shell("failed", "its last run ended: code 1"));
	assertEqual(failed.dataset.banner, "server");
	failed.querySelector("button").click();
	assertEqual(restarted, ["w1"]);
});

test("servers: the start page says when an open workspace's server is not ready", () => {
	assertEqual(openState({ wsId: "w1", server: { state: "ready" } }, "w1"), "shown");
	assertEqual(openState({ wsId: "w2", server: { state: "restarting" } }, "w1"), "open, server restarting");
	assertEqual(openState({ wsId: "w2" }, "w1"), "open", "the mock's workspaces have no server");
});
