// chrome.js - what every viewer of the plugin has around its content: a dense toolbar (the editor's look), a status
// line, a notice in place of the content with the ways out (another viewer, the text editor, a download), and sizes
// written as people read them.
//     replace(el, notice("This browser cannot play x.mkv.", [openWith(ctx, "hex", "Open in Hex Viewer"), download(ctx)]));

import { h, icon } from "/ui-kit/dom.js";
import { fileUrl } from "./fileurl.js";

export function formatSize(bytes) {
	if (bytes < 1024) return bytes + " B";
	const units = ["KB", "MB", "GB", "TB"];
	let value = bytes / 1024;
	let unit = 0;
	while (value >= 1024 && unit < units.length - 1) {
		value /= 1024;
		unit++;
	}
	return (value >= 100 ? Math.round(value) : value.toFixed(1)) + " " + units[unit];
}

export function toolButton(iconName, title, onClick, extra) {
	return h("button.ib" + (extra || ""), { title, "aria-label": title, on: { click: onClick } }, icon(iconName));
}

// The file saved to this device (a phone's downloads, the desktop's Downloads folder).
export function saveFile(ctx, path) {
	const link = h("a", { href: fileUrl(ctx.wsId, path, { download: true }), download: path.split("/").pop(), hidden: true });
	document.body.append(link);
	link.click();
	link.remove();
}

export function downloadButton(ctx, path) {
	return toolButton("cloud-download", "Download a copy to this device", () => saveFile(ctx, path));
}

// A button opening the file in tab kind `view` ("doc" for the text editor, "hex", ...).
export function openWithButton(ctx, path, view, label) {
	return h("button.btn", { on: { click: () => ctx.ied.files.open(path, { preview: false, view }) } }, label);
}

// The content's place taken by `message` and its buttons.
export function notice(message, buttons, detail) {
	return h("div.ft-notice", { role: "status" },
		h("div.ft-notice-text", null, message),
		detail ? h("div.ft-notice-detail.muted", null, detail) : null,
		buttons && buttons.length ? h("div.ft-notice-buttons", null, buttons) : null);
}

// The plugin's stylesheet, once.
export function addStylesheet(href) {
	if (document.querySelector('link[href="' + href + '"]')) return;
	document.head.append(h("link", { rel: "stylesheet", href }));
}
