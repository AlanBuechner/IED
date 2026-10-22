// layoutstore.test.js - the layout store against a fake protocol client: a change made before the server's document
// arrives is made to that document, never to an empty one written over it.

import { test, assertEqual, assertTrue } from "./harness.js";
import { LayoutStore } from "../src/shell/layoutstore.js";
import { emptyDocument, withActivity } from "../src/shell/layoutdoc.js";

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
