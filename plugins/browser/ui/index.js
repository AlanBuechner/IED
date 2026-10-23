// index.js - the browser plugin's UI module (docs/27 "The Browser tab"): the "browser:<pageId>" tab kind showing a page
// that lives on the server, View > Web Browser, Browser Pages... (every page of the workspace, the AI's too), View in
// Browser for a workspace file, an HTML file's page beside its editor (reloaded when the file is saved), a toast when
// the AI opens a page, and closing a page with its tab (one the AI still works in closes when it is done).

import { icon } from "/ui-kit/dom.js";
import { showPicker } from "/ui-kit/picker.js";
import { BrowserView } from "./browserview.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/browser.css");
	ied.tabs.kind("browser", { view: "BrowserTab", title: () => "Browser", icon: () => "$(globe)" });
	ied.tabs.onClose("browser", (ref) => {
		ied.client.request("browser.close", { pageId: ref.slice("browser:".length) })
			.then((answer) => answer && answer.closed === false && ied.toast("The AI is still using this page: it closes when the AI is done with it."))
			.catch(() => {});
	});
	ied.commands.handle("ied.browser.open", (args) => openPage(ied, args && args.url));
	ied.commands.handle("ied.browser.openFile", (args) => {
		const root = ied.workspace.root;
		if (args && args.path && root) openPage(ied, fileUrl(root, args.path));
	});
	ied.commands.handle("ied.browser.pages", () => pickPage(ied));
	const followPreviews = reloadPreviewsOnSave(ied);
	ied.commands.handle("ied.browser.previewBeside", async (args) => {
		await previewBeside(ied, args && args.path ? args.path : ied.context.get("resourcePath"));
		followPreviews();
	});
	watchNewPages(ied);
}

// An HTML file's page beside its editor, tethered to it as a Markdown preview is: shown with the file, closed with it.
// One open already is shown and loaded again.
async function previewBeside(ied, path) {
	const root = ied.workspace.root;
	if (!path || !root) return;
	const open = previewOf(ied, path);
	if (open) {
		ied.tabs.open(open, { preview: false });
		ied.client.request("browser.reload", { pageId: open.slice("browser:".length) }).catch(() => {});
		return;
	}
	try {
		const { pageId } = await ied.client.request("browser.open", { wsId: ied.workspace.wsId, url: fileUrl(root, path), width: 1280, height: 800, scale: window.devicePixelRatio || 1 });
		ied.tabs.openBeside("doc:" + path, "browser:" + pageId, { tether: true, closes: true });
	}
	catch (e) {
		ied.toast("The page did not open: " + e.message, { level: "error" });
	}
}

// The Browser tab tethered to the editor of `path`, or null.
function previewOf(ied, path) {
	return ied.tabs.partners("doc:" + path).find((ref) => ref.startsWith("browser:")) || null;
}

// Each Browser tab tethered to a workspace file's editor loads again when that file is saved. Returns the function that
// looks at the tethers again (a tether made after its tab opened changes no tab).
function reloadPreviewsOnSave(ied) {
	const watchers = new Map();   // "browser:<pageId>" -> {path, stop}
	const follow = (state) => {
		const wanted = new Map();
		for (const ref of state.open) {
			if (!ref.startsWith("browser:")) continue;
			const doc = ied.tabs.partners(ref).find((partner) => partner.startsWith("doc:"));
			if (doc) wanted.set(ref, doc.slice("doc:".length));
		}
		for (const [ref, watcher] of watchers) {
			if (wanted.get(ref) === watcher.path) continue;
			watcher.stop();
			watchers.delete(ref);
		}
		for (const [ref, path] of wanted)
			if (!watchers.has(ref)) watchers.set(ref, { path, stop: watchSaves(ied, path, () => ied.client.request("browser.reload", { pageId: ref.slice("browser:".length) }).catch(() => {})) });
	};
	ied.tabs.onChange(follow);
	follow(ied.tabs.state());
	return () => follow(ied.tabs.state());
}

// Calls `saved()` each time the workspace file `path` is saved; returns the function that stops it.
function watchSaves(ied, path, saved) {
	let stop = null;
	let stopped = false;
	ied.client.request("doc.open", { wsId: ied.workspace.wsId, path }).then((opened) => {
		if (stopped) return;
		stop = ied.client.subscribe("doc:" + opened.docId, { snap: () => {}, evt: (kind) => kind === "doc.saved" && saved() });
	}).catch(() => {});
	return () => {
		stopped = true;
		if (stop) stop();
	};
}

// A workspace file as a file: URL ("C:\\site" + "pages/a b.html" -> "file:///C:/site/pages/a%20b.html").
export function fileUrl(root, path) {
	const full = (root.replace(/[\\/]+$/, "") + "/" + path.replace(/^[\\/]+/, "")).replace(/\\/g, "/");
	return "file:///" + full.split("/").map((part, i) => (i === 0 && /^[A-Za-z]:$/.test(part) ? part : encodeURIComponent(part))).join("/");
}

async function openPage(ied, url) {
	const wsId = ied.workspace.wsId;
	if (!wsId) return;
	try {
		const { pageId } = await ied.client.request("browser.open", { wsId, url: url || "about:blank", width: 1280, height: 800, scale: window.devicePixelRatio || 1 });
		ied.tabs.open("browser:" + pageId, { preview: false });
	}
	catch (e) {
		ied.toast("The page did not open: " + e.message, { level: "error" });
	}
}

async function pickPage(ied) {
	const wsId = ied.workspace.wsId;
	if (!wsId) return;
	const { pages } = await ied.client.request("browser.pages", { wsId });
	showPicker({
		placeholder: "Open a page of this workspace",
		label: "Browser pages",
		empty: "No page is open. View > Web Browser opens one.",
		provide: async (text) => pages
			.filter((p) => !text || (p.title + " " + p.url).toLowerCase().includes(text.toLowerCase()))
			.map((p) => ({ label: p.title || p.url || "about:blank", detail: (p.opener === "ai" ? "AI - " : "") + p.url, icon: icon("globe"), pageId: p.pageId })),
		pick: (item) => ied.tabs.open("browser:" + item.pageId, { preview: false }),
	});
}

// What a new page's toast says: the AI's pages and pages a page opened (target="_blank", window.open) render whether
// or not anyone looks, so they are told as they open; a person's own new page is already in their tab.
export function newPageNotice(page) {
	const where = page.url || "a page";
	if (page.opener === "ai") return "The AI opened " + where;
	if (page.parent) return "A page opened " + where;
	return null;
}

// New pages that open without a tab, told as they open; Show puts one in a tab.
function watchNewPages(ied) {
	let stop = null;
	const follow = () => {
		if (stop) stop();
		stop = null;
		const wsId = ied.workspace.wsId;
		if (!wsId) return;
		stop = ied.client.subscribe("browser:" + wsId, {
			snap: () => {},
			evt: (kind, data) => {
				const notice = kind === "page.added" ? newPageNotice(data) : null;
				if (notice) ied.toast(notice, { action: { label: "Show", run: () => ied.tabs.open("browser:" + data.pageId, { preview: false }) } });
			},
		});
	};
	if (ied.workspace.on) ied.workspace.on(follow);
	follow();
}

export function BrowserTab(ctx) {
	return new BrowserView(ctx, ctx.ref.slice("browser:".length));
}

function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(Object.assign(document.createElement("link"), { rel: "stylesheet", href }));
}
