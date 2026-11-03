// layoutstore.test.js - the layout store against a fake protocol client: a change made before the server's document
// arrives is made to that document, never to an empty one written over it.

import { test, assertEqual, assertTrue } from "./harness.js";
import { LayoutStore } from "../src/shell/layoutstore.js";
import { emptyDocument, withActivity, withTree } from "../src/shell/layoutdoc.js";

function fakeClient() {
	const client = { clientId: "c1", handlers: new Map(), sent: [] };
	client.subscribe = (topic, handler) => {
		client.handlers.set(topic, handler);
		return () => client.handlers.delete(topic);
	};
	client.request = (method, params) => {
		client.sent.push({ method, params });
		return Promise.resolve({ revision: params.expectedRevision + 1 });
	};
	client.newOpId = () => "op";
	return client;
}

test("layout store: a change before the server's document is made to it", () => {
	const client = fakeClient();
	const seen = [];
	const store = new LayoutStore(client, "w1", "desktop", (doc) => seen.push(doc));
	store.update((doc) => withActivity(doc, "ied.editor"));
	assertEqual(store.doc, null, "nothing yet: it waits");
	assertEqual(seen.length, 0);
	const server = Object.assign(emptyDocument(), { activity: "ied.chat", activities: { "ied.editor": { kind: "tabs", tabs: ["doc:a"], active: "doc:a", well: true } } });
	client.handlers.get("layout:w1:desktop").snap({ revision: 4, layout: server });
	assertEqual(store.doc.activity, "ied.editor", "the change, made");
	assertEqual(store.doc.activities["ied.editor"].tabs, ["doc:a"], "to the server's document");
	assertTrue(store.dirty, "and written later");
	store.dispose();
	assertEqual(client.sent[0].params.layout.activities["ied.editor"].tabs, ["doc:a"], "what is written keeps everyone's layout");
	assertEqual(client.sent[0].params.expectedRevision, 4);
});

function settled() {
	return new Promise((resolve) => setTimeout(resolve, 0));
}

function loaded(client, doc) {
	const seen = [];
	const store = new LayoutStore(client, "w1", "desktop", (next) => seen.push(next));
	client.handlers.get("layout:w1:desktop").snap({ revision: 4, layout: doc });
	return { store, seen };
}

const tree = (tab) => ({ kind: "tabs", tabs: [tab], active: tab, well: true });

test("layout store: another window's change adopted as it comes when nothing here waits", () => {
	const client = fakeClient();
	const { store } = loaded(client, emptyDocument());
	const theirs = withActivity(emptyDocument(), "ied.git");
	client.handlers.get("layout:w1:desktop").evt("layout.changed", { revision: 5, layout: theirs, by: "c2" });
	assertEqual(store.doc.activity, "ied.git");
	assertTrue(!store.dirty, "nothing to write");
	store.dispose();
});

test("layout store: another window's change keeps what this window has not written", () => {
	const client = fakeClient();
	const base = emptyDocument();
	const { store } = loaded(client, base);
	store.update((doc) => withTree(doc, "ied.chat", tree("chat:1")));
	client.handlers.get("layout:w1:desktop").evt("layout.changed", { revision: 5, layout: withTree(base, "ied.editor", tree("doc:a")), by: "c2" });
	assertEqual(Object.keys(store.doc.activities).sort(), ["ied.chat", "ied.editor"], "both");
	assertTrue(store.dirty, "the merge is written");
	store.dispose();
	assertEqual(Object.keys(client.sent[0].params.layout.activities).sort(), ["ied.chat", "ied.editor"], "what is written has both");
	assertEqual(client.sent[0].params.expectedRevision, 5);
});

test("layout store: a change on its way keeps through a conflict: the retry writes both", async () => {
	const client = fakeClient();
	const base = emptyDocument();
	const { store } = loaded(client, base);
	const channel = () => client.handlers.get("layout:w1:desktop");
	let answer = 0;
	const plain = client.request;
	client.request = (method, params) => {
		answer += 1;
		if (answer > 1) return plain(method, params);
		client.sent.push({ method, params });
		// Their change got there first: its document comes on the socket before the refusal.
		channel().evt("layout.changed", { revision: 5, layout: withTree(base, "ied.editor", tree("doc:a")), by: "c2" });
		const conflict = new Error("conflict");
		Object.assign(conflict, { code: "conflict", data: { revision: 5 } });
		return Promise.reject(conflict);
	};
	store.update((doc) => withTree(doc, "ied.chat", tree("chat:1")));
	await store.flush();
	await settled();
	assertEqual(client.sent.length, 2, "tried, then again");
	assertEqual(Object.keys(client.sent[1].params.layout.activities).sort(), ["ied.chat", "ied.editor"], "the retry has both");
	assertEqual(client.sent[1].params.expectedRevision, 5);
	assertEqual(Object.keys(store.doc.activities).sort(), ["ied.chat", "ied.editor"], "and so does this window");
	store.dispose();
});

test("layout store: a conflict waits for the document it names", async () => {
	const client = fakeClient();
	const base = emptyDocument();
	const { store } = loaded(client, base);
	const channel = () => client.handlers.get("layout:w1:desktop");
	let answer = 0;
	const plain = client.request;
	client.request = (method, params) => {
		answer += 1;
		if (answer > 1) return plain(method, params);
		client.sent.push({ method, params });
		const conflict = new Error("conflict");
		Object.assign(conflict, { code: "conflict", data: { revision: 5 } });
		setTimeout(() => channel().evt("layout.changed", { revision: 5, layout: withTree(base, "ied.editor", tree("doc:a")), by: "c2" }), 20);
		return Promise.reject(conflict);
	};
	store.update((doc) => withTree(doc, "ied.chat", tree("chat:1")));
	await store.flush();
	assertEqual(Object.keys(client.sent[1].params.layout.activities).sort(), ["ied.chat", "ied.editor"], "the retry came after it");
	store.dispose();
});

test("layout store: a drag here holds another window's change until it ends, then keeps both", () => {
	const client = fakeClient();
	const base = emptyDocument();
	const { store, seen } = loaded(client, base);
	store.hold(true);
	const before = seen.length;
	client.handlers.get("layout:w1:desktop").evt("layout.changed", { revision: 5, layout: withTree(base, "ied.editor", tree("doc:a")), by: "c2" });
	assertEqual(seen.length, before, "nothing moves under the drag");
	store.update((doc) => withTree(doc, "ied.chat", tree("chat:1")));
	store.hold(false);
	assertEqual(Object.keys(store.doc.activities).sort(), ["ied.chat", "ied.editor"]);
	store.dispose();
});

test("layout store: flush writes now, and a window's own echo changes nothing", async () => {
	const client = fakeClient();
	const { store, seen } = loaded(client, emptyDocument());
	store.update((doc) => withActivity(doc, "ied.git"));
	const before = seen.length;
	await store.flush();
	assertEqual(client.sent.length, 1, "written");
	client.handlers.get("layout:w1:desktop").evt("layout.changed", { revision: 5, layout: store.doc, by: "c1" });
	assertEqual(seen.length, before, "its own write comes back as nothing new");
	store.dispose();
});
