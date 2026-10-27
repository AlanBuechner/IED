// definitionlinks.js - Ctrl held over the code underlines each symbol the pointer reaches that has a definition (Monaco's
// go-to-definition-at-position contribution, the link a Ctrl+Click follows), for as long as Ctrl is held (owner,
// 2026-10-26: "i have to keep re pressing crtl when i should be able to just hold it down and move my mouse around where
// ever i want"). Monaco asks the language server once per word under the pointer and then keeps that word until Ctrl is
// released: a word whose answer was empty - the server still starting or reading the file - or came after the pointer,
// the cursor or the scroll had moved, stayed without its underline however long it was hovered. Here such a word is
// asked again when the pointer moves on it a moment later, while no answer is on its way; one with its underline is left
// as it is.

const CONTRIBUTION = "editor.contrib.gotodefinitionatposition";
// How long after an answer that left no underline the same word is asked again.
export const ASK_AGAIN_MS = 400;

// Keeps `editor`'s Ctrl+hover links asking; returns the function that stops it. `now` is the clock (tests give theirs).
export function keepDefinitionLinksAsking(editor, now = () => Date.now()) {
	const contribution = editor.getContribution(CONTRIBUTION);
	if (!contribution || typeof contribution.startFindDefinition !== "function") return () => {};
	const original = contribution.startFindDefinition;
	let asking = 0;
	let answeredAt = -Infinity;
	contribution.startFindDefinition = function (position) {
		const stale = this.currentWordAtPosition && this.linkDecorations.length === 0;
		if (stale && !asking && now() - answeredAt >= ASK_AGAIN_MS) this.currentWordAtPosition = null;
		const before = this.previousPromise;
		const run = original.call(this, position);
		if (this.previousPromise !== before) {
			asking += 1;
			const done = () => {
				asking -= 1;
				answeredAt = now();
			};
			Promise.resolve(run).then(done, done);
		}
		return run;
	};
	return () => {
		contribution.startFindDefinition = original;
	};
}
