// index.js - the browser plugin's UI module (docs/27 "The Browser tab"): the "browser:<pageId>" tab kind showing a page
// that lives on the server, View > Web Browser, Browser Pages... (every page of the workspace, the AI's too), View in
// Browser for a workspace file, a toast when the AI opens a page, and closing a page with its tab.

import { icon } from "/ui-kit/dom.js";
import { showPicker } from "/ui-kit/picker.js";
import { BrowserView } from "./browserview.js";

export function activate(ied) {
	addStylesheet(ied.plugin.base + "ui/browser.css");
	ied.tabs.kind("browser", { view: "BrowserTab", title: () => "Browser", icon: () => "$(globe)" });
	ied.tabs.onClose("browser", (ref) => {
		ied.client.request("browser.close", { pageId: ref.slice("browser:".length) }).catch(() => {});
	});
	ied.commands.handle("ied.browser.open", (args) => openPage(ied, args && args.url));
	ied.commands.handle("ied.browser.openFile", (args) => {
		const root = ied.workspace.root;
		if (args && args.path && root) openPage(ied, fileUrl(root, args.path));
	});
	ied.commands.handle("ied.browser.pages", () => pickPage(ied));
	watchNewPages(ied);
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
