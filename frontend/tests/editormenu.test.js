// editormenu.test.js - the editor's right-click menu (docs/15 "Context menus"): a plugin's own rows head a contributed
// menu; the language server's actions it lists (those at the cursor, then the fixes of the line's other problems, the
// preferred first); and, in a real Monaco, a right click or Shift+F10 opening `editor/context` with them, the cursor
// moved to the click as Visual Studio moves it.

import { test, assertEqual, assertTrue } from "./harness.js";
import { withOwnRows } from "../src/shell/menus.js";
import { menuActionsOf } from "/plugins/ied.editor/ui/lspconvert.js";
import { attachEditorMenu, rowsOf } from "/plugins/ied.editor/ui/editormenu.js";
import { loadMonaco } from "/plugins/ied.editor/ui/monaco.js";

const CONTRIBUTED = [{ kind: "item", label: "Rename...", command: "ied.editor.rename" }, { kind: "separator" }, { kind: "item", label: "Cut", command: "ied.editor.cut" }];
const run = () => {};

test("editor menu: a plugin's own rows come first, a line between them and the contributed ones", () => {
	const rows = withOwnRows([{ label: "Create definition", run, icon: "$(lightbulb)" }], CONTRIBUTED);
	assertEqual(rows.map((row) => row.kind === "separator" ? "---" : row.label), ["Create definition", "---", "Rename...", "---", "Cut"], "own, line, contributed");
	assertEqual([rows[0].kind, rows[0].enabled, rows[0].icon], ["item", true, "$(lightbulb)"], "an item, enabled unless it says");
	assertEqual(withOwnRows([], CONTRIBUTED), CONTRIBUTED, "no own rows: the menu as contributed");
	assertEqual(withOwnRows(undefined, CONTRIBUTED), CONTRIBUTED, "none given");
	assertEqual(withOwnRows([{ label: "Alone", run }], []).map((row) => row.label), ["Alone"], "no contributed rows: no line");
	assertEqual(withOwnRows([{ label: "No run" }, { run }, null], CONTRIBUTED), CONTRIBUTED, "a row without a label or a run is left out");
	assertEqual(withOwnRows([{ label: "Off", run, enabled: false }], []).map((row) => row.enabled), [false], "a disabled one stays disabled");
});

const fix = (title, extra) => Object.assign({ title, kind: "quickfix", diagnostics: [{ message: "m" }] }, extra);

test("editor menu: the server's actions it lists - the cursor's, then the line's other fixes, the preferred first", () => {
	const atCursor = [fix("In place"), { title: "Extract function", kind: "refactor.extract" }, fix("In A.cf", { isPreferred: true })];
	const onLine = [fix("In A.cf", { isPreferred: true }), fix("Add the include"), { title: "Extract the line", kind: "refactor.extract" }];
	assertEqual(menuActionsOf(atCursor, onLine, 8).map((a) => a.title), ["In A.cf", "In place", "Extract function", "Add the include"],
		"preferred first; each title once; a refactoring asked of the whole line is not listed");
	assertEqual(menuActionsOf([{ title: "Organize imports", kind: "source.organizeImports" }, { title: "Fix all", kind: "source" }, fix("Off", { disabled: { reason: "no" } })], null, 8), [],
		"source actions and disabled ones stay with Quick Actions");
	assertEqual(menuActionsOf(null, [fix("Only on the line")], 8).map((a) => a.title), ["Only on the line"], "nothing at the cursor: the line's fixes");
	assertEqual(menuActionsOf([fix("a"), fix("b"), fix("c")], null, 2).map((a) => a.title), ["a", "b"], "at most `most`");
	assertEqual(menuActionsOf([{ title: "No kind" }], null, 8).map((a) => a.title), ["No kind"], "an action without a kind is listed");
});

test("editor menu: each action is a row with Visual Studio's lightbulb, the screwdriver for a refactoring", () => {
	const done = [];
	const rows = rowsOf([fix("Preferred", { isPreferred: true }), fix("Other"), { title: "Extract", kind: "refactor.extract" }], (action) => done.push(action.title));
	assertEqual(rows.map((row) => [row.label, row.icon]), [["Preferred", "$(lightbulb-autofix)"], ["Other", "$(lightbulb)"], ["Extract", "$(tools)"]], "icons");
	rows[1].run();
	assertEqual(done, ["Other"], "a row runs its action");
});

// A real Monaco editor over two lines, its own menu off (as editorview.js makes it), with the menu attached to a fake
// `ied` that records what it shows and fake language features that answer `actions`.
async function setUp(actions) {
	const monaco = await loadMonaco({ contributions: Object.assign(() => [], { on: () => () => {} }), report: () => {}, workspace: { wsId: "", root: "" }, commands: { run: () => {} } });
	const host = document.createElement("div");
	host.style.cssText = "position: fixed; left: 0; top: 0; width: 400px; height: 120px; opacity: 0; z-index: 10000";
	document.body.append(host);
	const model = monaco.editor.createModel("public void OnLoaded();\npublic void OnUpdate();\n", "plaintext", monaco.Uri.parse("inmemory://editormenu/test.txt"));
	const editor = monaco.editor.create(host, { model, contextmenu: false });
	editor.layout();
	const shown = [];
	const ran = [];
	const ied = { menus: { show: (menuId, at, args, options) => shown.push({ menuId, at, args, options }) } };
	const view = { editor, monaco, features: { menuActions: async () => actions, runAction: (e, action) => ran.push(action.title) } };
	const detach = attachEditorMenu(ied, view);
	return {
		monaco, editor, shown, ran,
		dispose: () => {
			detach();
			editor.dispose();
			model.dispose();
			host.remove();
		},
	};
}

async function until(fn) {
	for (let i = 0; i < 100 && !fn(); i++) await new Promise((resolve) => setTimeout(resolve, 10));
	return fn();
}

test("editor menu: a right click on the text shows editor/context there, the cursor at the click, the actions first", async () => {
	const s = await setUp([fix("Create definition of 'OnLoaded' in A.cf", { isPreferred: true })]);
	try {
		s.editor.setPosition({ lineNumber: 2, column: 1 });
		const p = s.editor.getScrolledVisiblePosition({ lineNumber: 1, column: 15 });
		const box = s.editor.getDomNode().getBoundingClientRect();
		const x = box.left + p.left + 2;
		const y = box.top + p.top + p.height / 2;
		const target = document.elementFromPoint(x, y);
		const event = new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 2 });
		target.dispatchEvent(event);
		assertTrue(event.defaultPrevented, "the browser's own menu is not shown");
		assertTrue(await until(() => s.shown.length === 1), "one menu shown");
		assertEqual(s.shown[0].menuId, "editor/context", "the editor's contributed menu");
		assertTrue(Math.abs(s.shown[0].at.x - x) <= 1 && Math.abs(s.shown[0].at.y - y) <= 1, "at the click: " + JSON.stringify(s.shown[0].at) + " for " + JSON.stringify({ x, y }));
		assertEqual(s.editor.getPosition().lineNumber, 1, "the cursor went to the line clicked");
		assertEqual(s.shown[0].options.rows.map((row) => row.label), ["Create definition of 'OnLoaded' in A.cf"], "the server's action heads it");
		s.shown[0].options.rows[0].run();
		assertEqual(s.ran, ["Create definition of 'OnLoaded' in A.cf"], "picking it runs the action");
	}
	finally {
		s.dispose();
	}
});

test("editor menu: a right click inside the selection keeps it, for the menu to act on", async () => {
	const s = await setUp([]);
	try {
		s.editor.setSelection(new s.monaco.Selection(1, 1, 1, 12));
		const p = s.editor.getScrolledVisiblePosition({ lineNumber: 1, column: 5 });
		const box = s.editor.getDomNode().getBoundingClientRect();
		const x = box.left + p.left + 2;
		const y = box.top + p.top + p.height / 2;
		document.elementFromPoint(x, y).dispatchEvent(new MouseEvent("contextmenu", { bubbles: true, cancelable: true, clientX: x, clientY: y, button: 2 }));
		assertTrue(await until(() => s.shown.length === 1), "the menu shown");
		assertEqual(s.editor.getSelection().toString(), new s.monaco.Selection(1, 1, 1, 12).toString(), "the selection stays");
		assertEqual(s.shown[0].options.rows, [], "no actions: the contributed rows alone");
	}
	finally {
		s.dispose();
	}
});

test("editor menu: Shift+F10 opens it under the cursor", async () => {
	const s = await setUp([fix("Fix it")]);
	try {
		s.editor.setPosition({ lineNumber: 2, column: 5 });
		s.editor.focus();
		const event = new KeyboardEvent("keydown", { key: "F10", code: "F10", shiftKey: true, bubbles: true, cancelable: true });
		Object.defineProperty(event, "keyCode", { get: () => 121 });
		document.activeElement.dispatchEvent(event);
		assertTrue(await until(() => s.shown.length === 1), "the menu shown");
		const p = s.editor.getScrolledVisiblePosition({ lineNumber: 2, column: 5 });
		const box = s.editor.getDomNode().getBoundingClientRect();
		const under = { x: box.left + p.left, y: box.top + p.top + p.height };
		assertTrue(Math.abs(s.shown[0].at.x - under.x) <= 1 && Math.abs(s.shown[0].at.y - under.y) <= 1, "under the cursor: " + JSON.stringify(s.shown[0].at) + " for " + JSON.stringify(under));
		assertEqual(s.shown[0].options.rows.map((row) => row.label), ["Fix it"], "with the actions");
	}
	finally {
		s.dispose();
	}
});
