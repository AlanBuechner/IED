// domview.js - a picture drawn by the page itself: an SVG stays sharp at every zoom, an animated GIF, PNG or WebP keeps
// playing, and a browser without WebGL2 still shows any picture (decoded pixels through a canvas). Zoom and offset as
// viewport.js keeps them; texels crisp from 1x; the checkerboard and the texel grid are CSS.
//     const view = new DomView(stage);
//     view.setElement(img, width, height, { vector: true });
//     view.draw(state, { checker: true, grid: false });

import { h } from "/ui-kit/dom.js";

export class DomView {
	constructor(stage) {
		this.frame = h("div.ft-dom");
		this.grid = h("div.ft-dom-grid", { hidden: true });
		this.frame.append(this.grid);
		stage.append(this.frame);
		this.element = null;
		this.size = null;
		this.vector = false;
	}

	// `element` an <img> or a <canvas> of `width` x `height` texels; `vector`: drawn anew at each size (SVG).
	setElement(element, width, height, options) {
		if (this.element) this.element.remove();
		this.element = element;
		element.classList.add("ft-dom-picture");
		element.draggable = false;
		this.frame.prepend(element);
		this.size = { width, height };
		this.vector = Boolean(options && options.vector);
	}

	draw(state, options) {
		if (!this.size) return;
		const width = this.size.width * state.zoom;
		const height = this.size.height * state.zoom;
		Object.assign(this.frame.style, { left: -state.x * state.zoom + "px", top: -state.y * state.zoom + "px", width: width + "px", height: height + "px" });
		this.frame.classList.toggle("checker", Boolean(options.checker));
		this.frame.classList.toggle("pixelated", !this.vector && state.zoom >= 1);
		const gridShown = Boolean(options.grid) && state.zoom >= 8;
		this.grid.hidden = !gridShown;
		if (gridShown) this.grid.style.backgroundSize = state.zoom + "px " + state.zoom + "px";
	}

	dispose() {
		this.frame.remove();
	}
}

// Decoded pixels in a canvas (floats through `exposure` and sRGB), for a browser without WebGL2.
export function canvasOf(pixels, exposure) {
	const canvas = h("canvas", { width: pixels.width, height: pixels.height });
	const image = new ImageData(pixels.width, pixels.height);
	const source = pixels.data;
	if (source instanceof Float32Array) {
		const gain = Math.pow(2, exposure || 0);
		const encode = (v) => {
			const c = Math.max(0, v * gain);
			return Math.round(255 * Math.min(1, c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055));
		};
		for (let i = 0; i < source.length; i += 4) {
			image.data[i] = encode(source[i]);
			image.data[i + 1] = encode(source[i + 1]);
			image.data[i + 2] = encode(source[i + 2]);
			image.data[i + 3] = Math.round(255 * Math.min(1, Math.max(0, source[i + 3])));
		}
	}
	else image.data.set(source);
	canvas.getContext("2d").putImageData(image, 0, 0);
	return canvas;
}
