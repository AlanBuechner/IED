// hoverpassthrough.js - while the link key is held (Ctrl; Cmd on a Mac: the key Ctrl+Click's go-to-definition uses), an
// editor's hovers - a symbol's documentation, the definition's preview - are see-through and let the pointer through, so
// moving on with the key held reaches the code beneath them (owner, 2026-10-27: the box that shows up when you hover
// symbols should not "get in the way when you have ctrl down"). Released, they are hovers again: readable, selectable,
// scrollable. The look is editor.css's `.links-held`.

export const HELD_CLASS = "links-held";

// Whether `event` (keyboard or mouse) holds the link key.
export function holdsLinkKey(event, mac) {
	return Boolean(mac ? event.metaKey : event.ctrlKey);
}

// Lets `editor`'s hovers pass the pointer through while the link key is held; returns the function that stops it. The
// keys are read on the window, so a key pressed while another view has the focus counts too. The editor's DOM node is
// asked each time: Monaco makes a new one when a model is set.
export function letHoversPassWhileLinking(editor, mac = /Mac|iPhone|iPad/.test(navigator.platform)) {
	let held = false;
	const show = () => {
		const node = editor.getDomNode();
		if (node) node.classList.toggle(HELD_CLASS, held);
	};
	const set = (value) => {
		held = value;
		show();
	};
	const onKey = (event) => set(holdsLinkKey(event, mac));
	const released = () => set(false);
	window.addEventListener("keydown", onKey, true);
	window.addEventListener("keyup", onKey, true);
	window.addEventListener("blur", released);
	const subscriptions = [editor.onMouseMove((event) => set(holdsLinkKey(event.event, mac))), editor.onDidChangeModel(show)];
	return () => {
		window.removeEventListener("keydown", onKey, true);
		window.removeEventListener("keyup", onKey, true);
		window.removeEventListener("blur", released);
		for (const subscription of subscriptions) subscription.dispose();
		released();
	};
}
