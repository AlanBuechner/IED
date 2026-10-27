// definitionlinks.test.js - Ctrl+hover's definition links, in a real Monaco: a word whose answer left no underline (the
// language server not ready yet) is asked again while Ctrl stays down, and gets its underline then; Monaco alone keeps
// it unasked until Ctrl is released.

import { test, assertEqual } from "./harness.js";
import { loadMonaco } from "/plugins/ied.editor/ui/monaco.js";
import { ASK_AGAIN_MS, keepDefinitionLinksAsking } from "/plugins/ied.editor/ui/definitionlinks.js";

const LANGUAGE = "ied-definitionlinks-test";

function fakeIed() {
	return { contributions: Object.assign(() => [], { on: () => () => {} }), report: () => {}, workspace: { wsId: "", root: "" }, commands: { run: () => {} } };
}

// An editor over "alpha beta" whose definition provider answers nothing first, then the second line; `patched` keeps
// its links asking.
async function setUp(patched, clock) {
	const monaco = await loadMonaco(fakeIed());
	if (!monaco.languages.getLanguages().some((l) => l.id === LANGUAGE)) monaco.languages.register({ id: LANGUAGE });
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: 0; top: 0; width: 400px; height: 120px; visibility: hidden";
	document.body.append(host);
	const model = monaco.editor.createModel("alpha beta\nbeta\n", LANGUAGE, monaco.Uri.parse("inmemory://definitionlinks/" + (patched ? "patched" : "plain") + ".txt"));
	const asked = { count: 0 };
	const provider = monaco.languages.registerDefinitionProvider(LANGUAGE, {
		provideDefinition: (m) => (m !== model ? null : asked.count++ === 0 ? [] : [{ uri: model.uri, range: new monaco.Range(2, 1, 2, 5) }]),
	});
	const editor = monaco.editor.create(host, { model });
	if (patched) keepDefinitionLinksAsking(editor, () => clock.now);
	return {
		monaco, asked, links: editor.getContribution("editor.contrib.gotodefinitionatposition"),
		dispose: () => {
			provider.dispose();
			editor.dispose();
			model.dispose();
			host.remove();
		},
	};
}

test("definition links: a word hovered with Ctrl down is asked again until it has its underline", async () => {
	const clock = { now: 0 };
	const plain = await setUp(false, clock);
	try {
		const at = new plain.monaco.Position(1, 8);
		await plain.links.startFindDefinition(at);
		await plain.links.startFindDefinition(at);
		assertEqual([plain.asked.count, plain.links.linkDecorations.length], [1, 0], "Monaco alone: the empty answer stays until Ctrl is released");
	}
	finally {
		plain.dispose();
	}
	const kept = await setUp(true, clock);
	try {
		const at = new kept.monaco.Position(1, 8);   // in "beta"
		await kept.links.startFindDefinition(at);
		assertEqual([kept.asked.count, kept.links.linkDecorations.length], [1, 0], "the server had nothing yet: no underline");
		await kept.links.startFindDefinition(at);
		assertEqual(kept.asked.count, 1, "the pointer moving on it at once: not asked again yet");
		clock.now += ASK_AGAIN_MS;
		await kept.links.startFindDefinition(at);
		assertEqual([kept.asked.count, kept.links.linkDecorations.length], [2, 1], "a moment later: asked again, and underlined - Ctrl never released");
		clock.now += ASK_AGAIN_MS;
		await kept.links.startFindDefinition(at);
		assertEqual(kept.asked.count, 2, "a word with its underline is not asked again");
	}
	finally {
		kept.dispose();
	}
});
