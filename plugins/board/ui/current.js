// current.js - the board the owner works on now (docs/32 "The board in the UI"): the one shown or touched last, else
// another board still open. The Board tool window shows its layers, plan and history.

const boards = new Set();
const listeners = new Set();
let current = null;

function set(view) {
	if (current === view) return;
	current = view;
	for (const fn of listeners) fn(view);
}

export function addBoard(view) {
	boards.add(view);
	if (!current) set(view);
}

export function removeBoard(view) {
	boards.delete(view);
	if (current === view) set([...boards].pop() || null);
}

export function setCurrentBoard(view) {
	if (boards.has(view)) set(view);
}

export function currentBoard() {
	return current;
}

// `fn(view)` each time the board worked on changes; returns the function that stops it.
export function onCurrentBoard(fn) {
	listeners.add(fn);
	return () => listeners.delete(fn);
}
