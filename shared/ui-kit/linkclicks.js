// linkclicks.js - the links of rendered Markdown (a Markdown preview, a chat answer; docs/15 "Links"): a click and a
// middle click are answered alike, by the view that shows the link and never by the page, which following one would
// leave the IDE (and a middle click would open IED's own page again in a new window - in the desktop app, a second IED
// window). The view gets {kind: "web" | "mail" | "path", href}: a web or mail address, or a path as the text writes it,
// which the view reads from its own folder. A middle click does not start the system's autoscroll either.
//     answerLinks(el, (target) => target.kind === "path" ? openFile(target.href) : openAddress(ied, target));

// What a link's `href` names: {kind, href}, or null for a place in the page ("#heading") or a scheme IED opens nothing of.
export function linkTarget(href) {
	if (!href || href.startsWith("#")) return null;
	const scheme = /^([a-z][a-z0-9+.-]*):/i.exec(href);
	if (!scheme) return { kind: "path", href };
	const name = scheme[1].toLowerCase();
	if (name === "http" || name === "https") return { kind: "web", href };
	if (name === "mailto") return { kind: "mail", href };
	return null;
}

// The link an event is on, with what it names; null for none, or for one the browser opens on purpose: a file to save
// (`download`), or a file of the server's marked to open beside the page (a workspace image of a chat answer).
function linkOf(event) {
	const link = event.target && event.target.closest ? event.target.closest("a[href]") : null;
	if (!link || link.hasAttribute("download")) return null;
	const target = linkTarget(link.getAttribute("href"));
	if (link.hasAttribute("target") && (!target || target.kind === "path")) return null;
	return { link, target };
}

// Answers the clicks and middle clicks on the links inside `el` with `open(target, event)`.
export function answerLinks(el, open) {
	const follow = (event) => {
		const found = linkOf(event);
		if (!found) return;
		event.preventDefault();
		if (found.target) open(found.target, event);
	};
	el.addEventListener("click", (event) => event.button === 0 && follow(event));
	el.addEventListener("auxclick", (event) => event.button === 1 && follow(event));
	el.addEventListener("mousedown", (event) => {
		if (event.button === 1 && linkOf(event)) event.preventDefault();
	});
}

// A web address in a Browser tab of IED when a plugin opens them (`ied.browser.open`, as the editor's links do), else in
// a window of its own; a mail address in the system's mail program.
export function openAddress(ied, target) {
	const browser = target.kind === "web" && ied.contributions("commands").some((c) => c.value.id === "ied.browser.open");
	if (browser) ied.commands.run("ied.browser.open", { url: target.href });
	else window.open(target.href, "_blank", "noopener,noreferrer");
}
