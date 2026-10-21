// navigation.js - Navigate Backward / Forward (VS Ctrl+- / Ctrl+Shift+-): the places the person's cursor jumped between
// on this page - into another document, or more than FAR_LINES lines away in the same one - as a history that going back
// and forward walks. A small move updates the current place instead of adding one.

const FAR_LINES = 10;
const MOST = 100;

export class Navigation {
	constructor() {
		this.places = [];   // {path, line, col}, 0-based
		this.index = -1;
	}

	// The cursor is at `place`.
	visit(place) {
		const current = this.places[this.index];
		if (current && current.path === place.path && Math.abs(current.line - place.line) < FAR_LINES) {
			this.places[this.index] = place;
			return;
		}
		this.places = this.places.slice(0, this.index + 1);
		this.places.push(place);
		if (this.places.length > MOST) this.places.shift();
		this.index = this.places.length - 1;
	}

	canGoBack() {
		return this.index > 0;
	}

	canGoForward() {
		return this.index < this.places.length - 1;
	}

	// The place to go back to, or null; it becomes the current one.
	back() {
		if (!this.canGoBack()) return null;
		this.index -= 1;
		return this.places[this.index];
	}

	forward() {
		if (!this.canGoForward()) return null;
		this.index += 1;
		return this.places[this.index];
	}
}
