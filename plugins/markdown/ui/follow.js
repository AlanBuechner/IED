// follow.js - a preview tethered beside a file shows the Markdown file of its group (docs/15 "Tethered tabs"), as VS
// Code's does: its group is the one holding the tab it is tethered with (its lead), and whenever that group's active
// tab is a Markdown file the preview does not show, the preview shows that file instead, in its place, tethered to it.
// The lead may be another file than the one previewed: a file opened in the place of the group's preview tab takes over
// its tethers (the shell's openTab), so browsing files there with single clicks keeps the preview with the group. A tab
// of another kind leaves the preview on the last Markdown file.

const MARKDOWN = /\.(md|markdown)$/i;

function isMarkdownDoc(ref) {
	return Boolean(ref) && ref.startsWith("doc:") && MARKDOWN.test(ref);
}

// What to do now, from `state` (ied.tabs.state()) and `partnersOf(ref)`: [{from, to, lead}], each preview to replace by
// the preview of `lead`.
export function moves(state, partnersOf) {
	const out = [];
	const taken = new Set(state.open);
	for (const preview of state.open.filter((ref) => ref.startsWith("mdpreview:"))) {
		const shown = "doc:" + preview.slice("mdpreview:".length);
		const group = partnersOf(preview).filter((ref) => ref.startsWith("doc:")).map((lead) => state.groups.find((g) => g.tabs.includes(lead))).find(Boolean);
		const next = group ? group.active : null;
		if (!isMarkdownDoc(next) || next === shown) continue;
		const to = "mdpreview:" + next.slice("doc:".length);
		if (taken.has(to)) continue;
		taken.add(to);
		out.push({ from: preview, to, lead: next });
	}
	return out;
}

// Keeps the previews following, a moment after each change of the tabs (a replace changes them again).
export function followPreviews(ied) {
	let queued = false;
	const run = () => {
		queued = false;
		for (const move of moves(ied.tabs.state(), (ref) => ied.tabs.partners(ref))) {
			ied.tabs.replace(move.from, move.to);
			ied.tabs.untether(move.to);
			ied.tabs.tether(move.lead, move.to, { closes: true });
		}
	};
	return ied.tabs.onChange(() => {
		if (queued) return;
		queued = true;
		setTimeout(run, 0);
	});
}
