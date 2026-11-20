// editormenu.js - the editor's right-click menu (docs/15 "Context menus"): Visual Studio's - Quick Actions and
// Refactorings, Rename, Peek and Go To Definition, Find All References, Toggle Header / Code File, Breakpoint, Cut, Copy,
// Paste - from the plugins' `editor/context` contributions, and above them the fixes and refactorings the language server
// has for the place clicked, by name, as Visual Assist lists its refactorings there ("Create definition of 'OnLoaded' in
// PlayerControler.cf"). Monaco's own menu is off (editorview.js `contextmenu: false`); Shift+F10 and the menu key open
// this one at the cursor, a long press on a phone as a sheet.

export const WAIT_MS = 600;   // how long the menu waits for the language server's actions before it shows without them
export const MOST = 8;        // the most actions listed; Quick Actions and Refactorings has them all

// Monaco's mouse target types (MouseTargetType) the menu opens on: the hidden textarea (the menu key), text, the empty
// space after it. A right click in an overlay widget (the find box's input) or on injected text gets the browser's menu.
const TEXTAREA = 1;
const CONTENT_TEXT = 6;
const CONTENT_EMPTY = 7;
const OVERLAY_WIDGET = 12;

// The rows an action list gives the menu: its title, the lightbulb Visual Studio draws (the preferred fix's with a spark,
// a refactoring's screwdriver), what it does.
export function rowsOf(actions, run) {
	return actions.map((action) => ({
		label: action.title,
		icon: action.isPreferred ? "$(lightbulb-autofix)" : typeof action.kind === "string" && action.kind.startsWith("refactor") ? "$(tools)" : "$(lightbulb)",
		run: () => run(action),
	}));
}

export function attachEditorMenu(ied, editor) {
	const ed = editor.editor;
	const monaco = editor.monaco;
	let latest = 0;   // a menu asked for later wins: an earlier one's late answer shows nothing

	const show = async (at) => {
		const mine = ++latest;
		const features = editor.features;
		const actions = features ? await features.menuActions(editor, WAIT_MS, MOST).catch(() => []) : [];
		if (mine !== latest || !ed.getModel()) return;
		const rows = features ? rowsOf(actions, (action) => features.runAction(editor, action)) : [];
		ied.menus.show("editor/context", at || cursorAnchor(ed), undefined, { rows });
	};

	const listeners = [
		ed.onContextMenu((e) => {
			if (e.target.type === OVERLAY_WIDGET) return;
			if (e.target.type === CONTENT_TEXT && e.target.detail && e.target.detail.injectedText) return;
			e.event.preventDefault();
			e.event.stopPropagation();
			if (e.target.type !== TEXTAREA && e.target.type !== CONTENT_TEXT && e.target.type !== CONTENT_EMPTY) return;
			// As Visual Studio: the cursor goes where the click was, unless that is in the selection, which the menu acts on.
			ed.focus();
			const position = e.target.position;
			if (position && !ed.getSelections().some((selection) => selection.containsPosition(position))) ed.setPosition(position);
			const browser = e.event.browserEvent;
			show(e.target.type === TEXTAREA || !browser ? null : { x: browser.clientX, y: browser.clientY });
		}),
		ed.onKeyDown((e) => {
			const shiftF10 = e.shiftKey && !e.ctrlKey && !e.altKey && !e.metaKey && e.keyCode === monaco.KeyCode.F10;
			if (!shiftF10 && e.keyCode !== monaco.KeyCode.ContextMenu) return;
			e.preventDefault();
			e.stopPropagation();
			show(null);
		}),
	];
	return () => {
		latest += 1;
		listeners.forEach((listener) => listener.dispose());
	};
}

// Under the cursor, for a menu opened from the keyboard.
function cursorAnchor(ed) {
	const position = ed.getPosition();
	const box = ed.getDomNode().getBoundingClientRect();
	if (!position) return { x: box.left, y: box.top };
	ed.revealPosition(position);
	const coords = ed.getScrolledVisiblePosition(position);
	return coords ? { x: box.left + coords.left, y: box.top + coords.top + coords.height } : { x: box.left, y: box.top };
}
