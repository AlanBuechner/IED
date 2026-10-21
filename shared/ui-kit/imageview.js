// imageview.js - an image at its size over the page (a lightbox), its name above it; Escape, a click beside it or
// the close button ends it. Images larger than the window are fitted to it.
//     showImage({ src: "/w/w1/files/shots/screen.png", title: "shots/screen.png" });

import { h, icon } from "./dom.js";

export function showImage({ src, title }) {
	const previous = document.activeElement;
	const close = () => {
		veil.remove();
		if (previous && previous.isConnected) previous.focus({ preventScroll: true });
	};
	const box = h("div.image-view", {
		role: "dialog", "aria-modal": "true", "aria-label": title, tabindex: "-1",
		on: {
			keydown: (event) => {
				event.stopPropagation();
				if (event.key !== "Escape") return;
				event.preventDefault();
				close();
			},
		},
	},
	h("div.image-view-head", null, h("span.mono.grow", null, title),
		h("button.btn.flat", { "aria-label": "Close", title: "Close (Esc)", on: { click: close } }, icon("close"))),
	h("img", { src, alt: title }));
	const veil = h("div.dialog-veil.image-veil", { on: { mousedown: (event) => event.target === veil && close() } }, box);
	document.body.append(veil);
	box.focus();
	return close;
}
