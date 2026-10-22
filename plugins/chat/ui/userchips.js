// userchips.js - what an owner's message named and carried, as chips under its text in the transcript (docs/04
// "Attachments and @-mentions"): a mention opens what it names where that is a place - a file, a symbol, a chat - an
// image attachment shows its picture (the workspace's files route, docs/10), any other file opens.

import { h, icon } from "/ui-kit/dom.js";
import { showImage } from "/ui-kit/imageview.js";
import { KIND_ICONS } from "./mentions.js";
import { sizeText } from "./attachbar.js";

export function workspaceFileUrl(wsId, path) {
	return "/w/" + encodeURIComponent(wsId) + "/files/" + path.split("/").map(encodeURIComponent).join("/");
}

// Where a mention opens: a function, or null for one that names no place (a folder, a terminal, an output, the Error
// List, the diff).
function opener(mention, actions) {
	if (mention.kind === "file") return () => actions.openFile(mention.ref);
	if (mention.kind === "chat") return () => actions.openChat(mention.ref);
	const place = mention.kind === "symbol" ? /^(.*):(\d+)$/.exec(mention.ref) : null;
	return place ? () => actions.openFile(place[1], Number(place[2]) - 1) : null;
}

function mentionChip(mention, actions) {
	const open = opener(mention, actions);
	const parts = [icon(KIND_ICONS[mention.kind] || "mention"), h("span.ellip", null, "@" + mention.label)];
	if (!open) return h("span.chat-chip", { title: "@" + mention.label }, ...parts);
	return h("button.chat-chip", { title: "Open @" + mention.label, on: { click: open } }, ...parts);
}

function attachmentChip(attachment, wsId, actions) {
	const title = attachment.name + " (" + sizeText(attachment.bytes) + ", " + attachment.mime + ")";
	if (attachment.mime.startsWith("image/")) {
		const url = workspaceFileUrl(wsId, attachment.path);
		const show = (event) => {
			event.preventDefault();
			showImage({ src: url, title: attachment.name });
		};
		return h("a.chat-chip.image", { href: url, target: "_blank", rel: "noopener", title, on: { click: show } }, h("img", { src: url, alt: attachment.name, loading: "lazy" }), h("span.ellip", null, attachment.name));
	}
	return h("button.chat-chip", { title: "Open " + title, on: { click: () => actions.openFile(attachment.path) } }, icon("file"), h("span.ellip", null, attachment.name), h("span.muted", null, sizeText(attachment.bytes)));
}

// The chips of a turn's user message; null when it named and carried nothing.
export function userChips(user, wsId, actions) {
	const mentions = (user && user.mentions) || [];
	const attachments = (user && user.attachments) || [];
	if (!mentions.length && !attachments.length) return null;
	return h("div.chat-chips", null, ...mentions.map((mention) => mentionChip(mention, actions)), ...attachments.map((attachment) => attachmentChip(attachment, wsId, actions)));
}
