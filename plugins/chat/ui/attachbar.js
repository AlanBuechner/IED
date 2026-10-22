// attachbar.js - the files a message will carry (docs/04 "Attachments and @-mentions"): each uploaded as soon as it is
// added (the paper clip, a paste, a drop) and shown as a chip above the text box - an image's picture, any other file's
// icon, name and size - with a button that takes it off.

import { h, icon, replace } from "/ui-kit/dom.js";

export const MOST_ATTACHMENTS = 16;
export const MOST_ATTACHMENT_BYTES = 20 * 1024 * 1024;

export function sizeText(bytes) {
	if (bytes < 1024) return bytes + " B";
	if (bytes < 1024 * 1024) return Math.ceil(bytes / 1024) + " KB";
	return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

// upload(file) -> Promise<{blobId} | null>; fileUrl(path): a workspace file's URL; changed(): the chips changed;
// refused(text): a file that cannot go.
export class AttachBar {
	constructor(upload, fileUrl, changed, refused) {
		this.upload = upload;
		this.fileUrl = fileUrl;
		this.changed = changed;
		this.refused = refused;
		this.items = [];
		this.el = h("div.chat-attached", { hidden: true });
	}

	get count() {
		return this.items.length;
	}

	add(files) {
		for (const file of files) {
			if (this.items.length >= MOST_ATTACHMENTS) return this.refused("A message carries at most " + MOST_ATTACHMENTS + " files.");
			if (file.size > MOST_ATTACHMENT_BYTES) {
				this.refused(file.name + " is larger than 20 MB.");
				continue;
			}
			const item = { name: file.name || "pasted", bytes: file.size, blobId: null, failed: false, url: file.type.startsWith("image/") ? URL.createObjectURL(file) : null, local: true };
			item.done = this.upload(file).then((answer) => {
				if (answer) item.blobId = answer.blobId;
				else item.failed = true;
				this.render();
			});
			this.items.push(item);
		}
		this.render();
	}

	// Files the chat keeps already (a queued message's, back in the box): sent again by their path.
	keep(attachments) {
		for (const attachment of attachments) {
			this.items.push({ name: attachment.name, bytes: attachment.bytes, path: attachment.path, mime: attachment.mime, done: Promise.resolve(),
				url: attachment.mime.startsWith("image/") ? this.fileUrl(attachment.path) : null, local: false });
		}
		this.render();
	}

	remove(item) {
		this.items = this.items.filter((each) => each !== item);
		if (item.local && item.url) URL.revokeObjectURL(item.url);
		this.render();
	}

	// What chat.send carries once every upload is done; the failed ones are left out.
	async sent() {
		await Promise.all(this.items.map((item) => item.done));
		return this.items.filter((item) => item.blobId || item.path).map((item) => item.path ? { path: item.path, name: item.name, mime: item.mime } : { blobId: item.blobId, name: item.name });
	}

	clear() {
		for (const item of this.items) if (item.local && item.url) URL.revokeObjectURL(item.url);
		this.items = [];
		this.render();
	}

	render() {
		this.el.hidden = !this.items.length;
		replace(this.el, ...this.items.map((item) => {
			const state = item.failed ? ".failed" : item.blobId || item.path ? "" : ".uploading";
			const picture = item.url ? h("img", { src: item.url, alt: "" }) : icon("file");
			const why = item.failed ? "The upload failed: remove it and add it again" : item.blobId || item.path ? item.name : "Uploading " + item.name;
			return h("span.chat-attach" + state, { title: why }, picture, h("span.ellip", null, item.name), h("span.muted", null, sizeText(item.bytes)),
				h("button.ib", { title: "Remove", "aria-label": "Remove " + item.name, on: { click: () => this.remove(item) } }, icon("close")));
		}));
		this.changed();
	}
}
