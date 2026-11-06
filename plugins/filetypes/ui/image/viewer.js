// viewer.js - the image viewer ("image:<path>" tabs, F-EDT-95): a picture or texture of the workspace, fitted to its
// view, zoomed about the pointer (wheel, pinch, the buttons), panned by dragging; channels shown alone, alpha over a
// checkerboard, a texel grid from 8x, the texel under the pointer (or a tap) in the status line; a DDS's elements, cube
// faces, mips and slices; a float picture's exposure. It reloads when the file changes on disk; an SVG follows its
// document as it is typed. Drawn by WebGL2 (glview.js), else by the page (domview.js).

import { h, icon, replace } from "/ui-kit/dom.js";
import { DocumentSync } from "/ui-kit/docsync.js";
import { applyChanges } from "/ui-kit/textedits.js";
import { fileUrl, fetchFile, watchFile } from "../fileurl.js";
import { downloadButton, notice, openWithButton, saveFile, toolButton } from "../chrome.js";
import { extensionOf, openPicture, openSvg } from "./sources.js";
import { GlView } from "./glview.js";
import { DomView, canvasOf } from "./domview.js";
import { attachInput } from "./input.js";
import { describe, rgbOf, texelText } from "./readout.js";
import { fitted, kept, panned, rescaled, steppedZoom, texelAt, zoomedAt, zoomLabel } from "./viewport.js";

const SVG_RENDER_MS = 150;

export class ImageViewer {
	constructor(ctx, path) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.path = path;
		this.ext = extensionOf(path);
		this.options = { channels: [true, true, true, true], checker: true, grid: true, exposure: 0 };
		this.state = null;
		this.fitting = true;
		this.picture = null;
		this.shown = null;
		this.surface = null;
		this.place = { layer: 0, level: 0, slice: 0 };
		this.bytes = -1;
		this.generation = 0;
		this.frame = 0;
		this.build();
		this.makeSurfaces();
		this.detachInput = attachInput(this.stage, this.gestures());
		this.resizer = new ResizeObserver(() => this.queueDraw());
		this.resizer.observe(this.stage);
		if (this.ext === "svg") this.followDocument();
		else {
			this.stopWatch = watchFile(this.ied, ctx.wsId, path, (change) => (change === "deleted" ? this.problem("The file was deleted.") : this.load()));
			this.load();
		}
	}

	build() {
		this.info = h("span.ft-info");
		this.selects = h("span.ft-selects");
		this.channelButtons = ["R", "G", "B", "A"].map((name, i) => h("button.ft-chan.on", { title: "Show channel " + name, "aria-pressed": "true", on: { click: () => this.toggleChannel(i) } }, name));
		this.checkerButton = toolButton("symbol-color", "Alpha over a checkerboard", () => this.toggleOption("checker"), ".on");
		this.gridButton = toolButton("symbol-numeric", "Texel grid from 8x", () => this.toggleOption("grid"), ".on");
		this.exposureInput = h("input.ft-exposure", { type: "range", min: "-10", max: "10", step: "0.1", value: "0", title: "Exposure (stops); double-click resets", on: { input: () => this.setExposure(Number(this.exposureInput.value)), dblclick: () => this.setExposure(0) } });
		this.exposureText = h("span.ft-ev", null, "EV 0.0");
		this.exposure = h("span.ft-group", { hidden: true }, this.exposureText, this.exposureInput);
		this.zoomText = h("button.btn.flat.ft-zoom", { title: "Fit, or actual size", on: { click: () => this.zoomKey(this.fitting ? "actual" : "fit") } }, "100%");
		const tools = h("div.ft-tools", null,
			this.info, this.selects,
			h("span.ft-chans", { role: "group", "aria-label": "Channels" }, this.channelButtons),
			h("span.ft-group.ft-wide", null, this.checkerButton, this.gridButton),
			this.exposure,
			h("span.grow"),
			toolButton("zoom-out", "Zoom out (-)", () => this.zoomKey("zoomOut")),
			this.zoomText,
			toolButton("zoom-in", "Zoom in (+)", () => this.zoomKey("zoomIn")),
			toolButton("screen-normal", "Fit to the view (0)", () => this.zoomKey("fit"), ".ft-wide"),
			downloadButton(this.ctx, this.path));
		this.canvas = h("canvas.ft-canvas", { hidden: true });
		this.note = h("div.ft-stage-note", null, notice(h("span.muted", null, "Opening " + this.path.split("/").pop() + "...")));
		this.stage = h("div.ft-stage", { tabindex: "0", role: "img", "aria-label": this.path }, this.canvas, this.note);
		this.readout = h("span.ft-readout.mono");
		this.status = h("div.ft-status", null, this.readout);
		this.ctx.el.classList.add("ft-view");
		this.ctx.el.append(tools, this.stage, this.status);
	}

	makeSurfaces() {
		try {
			this.gl = new GlView(this.canvas, () => this.queueDraw());
		}
		catch (e) {
			this.gl = null;
		}
		this.dom = new DomView(this.stage);
		this.dom.frame.hidden = true;
	}

	gestures() {
		const view = () => ({ width: this.stage.clientWidth, height: this.stage.clientHeight });
		return {
			zoomBy: (factor, px, py) => this.setState(zoomedAt(this.current(view()), this.current(view()).zoom * factor, px, py)),
			panBy: (dx, dy) => this.setState(panned(this.current(view()), dx, dy)),
			toggleFit: (px, py) => (this.fitting || Math.abs(this.current(view()).zoom - 1) > 1e-6 ? this.setState(zoomedAt(this.current(view()), 1, px, py)) : this.zoomKey("fit")),
			hover: (px, py) => this.showTexel(px, py),
			pick: (px, py) => this.showTexel(px, py),
			key: (name) => this.zoomKey(name),
		};
	}

	// The view now; fitted, a texture's smaller mip at mip 0's fitted size, so switching mips keeps its size on screen.
	current(view) {
		if (this.state && !this.fitting) return this.state;
		const shown = this.shown || { width: 1, height: 1 };
		const texture = this.picture && this.picture.kind === "texture" ? this.picture.texture : null;
		const base = texture && this.place.level > 0 ? { width: texture.width, height: texture.height } : shown;
		this.state = base === shown ? fitted(view, shown, false) : rescaled(fitted(view, base, false), base, shown);
		return this.state;
	}

	setState(state) {
		if (!this.shown) return;
		this.fitting = false;
		this.state = state;
		this.queueDraw();
	}

	zoomKey(name) {
		if (!this.shown) return;
		const view = { width: this.stage.clientWidth, height: this.stage.clientHeight };
		const state = this.current(view);
		if (name === "fit") {
			this.fitting = true;
			this.queueDraw();
		}
		else if (name === "actual") this.setState(zoomedAt(state, 1, view.width / 2, view.height / 2));
		else this.setState(zoomedAt(state, steppedZoom(state.zoom, name === "zoomIn" ? 1 : -1), view.width / 2, view.height / 2));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Loading

	async load() {
		const generation = ++this.generation;
		try {
			const bytes = await fetchFile(fileUrl(this.ctx.wsId, this.path));
			if (generation !== this.generation) return;
			this.bytes = bytes.length;
			const picture = await openPicture(this.ext, bytes);
			if (generation !== this.generation) return release(picture);
			this.present(picture);
		}
		catch (e) {
			if (generation === this.generation) this.problem(e.message);
		}
		return undefined;
	}

	followDocument() {
		this.text = "";
		this.sync = new DocumentSync(this.ied.client, {
			setText: (text) => this.svgChanged(text),
			applyRemote: (changes) => this.svgChanged(applyChanges(this.text, changes)),
			setReadOnly: () => {},
		}, { onState: (state) => state.error && this.problem(state.error) });
		this.sync.open(this.ctx.wsId, this.path).catch((e) => this.problem("The SVG could not be opened: " + e.message));
	}

	svgChanged(text) {
		this.text = text;
		this.bytes = new TextEncoder().encode(text).length;
		clearTimeout(this.svgTimer);
		this.svgTimer = setTimeout(async () => {
			const generation = ++this.generation;
			try {
				const picture = await openSvg(this.text);
				if (generation === this.generation) this.present(picture);
				else release(picture);
			}
			catch (e) {
				if (generation === this.generation) this.readout.textContent = e.message;
			}
		}, this.picture ? SVG_RENDER_MS : 0);
	}

	present(picture) {
		const previous = this.shown;
		release(this.picture);
		this.picture = picture;
		this.note.hidden = true;
		if (picture.kind === "texture") {
			this.place = clampPlace(this.place, picture.texture);
			this.showTextureImage(previous);
		}
		else if (picture.kind === "pixels") this.showPixels(picture.pixels, picture.format, previous);
		else if (picture.kind === "bitmap") this.showBitmap(picture, previous);
		else this.showElement(picture, previous);
		this.renderSelects();
	}

	showTextureImage(previous) {
		const texture = this.picture.texture;
		try {
			const pixels = texture.image(this.place.layer, this.place.level, this.place.slice);
			this.showPixels(pixels, texture.format, previous);
		}
		catch (e) {
			this.problem(e.message);
		}
	}

	showPixels(pixels, format, previous) {
		const fits = this.gl && this.gl.ok && Math.max(pixels.width, pixels.height) <= this.gl.maxSize;
		this.shown = { width: pixels.width, height: pixels.height, format, pixels, hdr: pixels.data instanceof Float32Array };
		if (fits) {
			this.gl.setPixels(pixels);
			this.useSurface("gl");
		}
		else {
			this.dom.setElement(canvasOf(pixels, this.options.exposure), pixels.width, pixels.height);
			this.useSurface("dom");
		}
		this.shownChanged(previous);
	}

	showBitmap(picture, previous) {
		const fits = this.gl && this.gl.ok && Math.max(picture.width, picture.height) <= this.gl.maxSize;
		this.shown = { width: picture.width, height: picture.height, format: picture.format, pixels: null, hdr: false };
		if (fits) {
			this.gl.setSource(picture.bitmap, picture.width, picture.height);
			this.useSurface("gl");
		}
		else {
			const canvas = h("canvas", { width: picture.width, height: picture.height });
			canvas.getContext("2d").drawImage(picture.bitmap, 0, 0);
			this.dom.setElement(canvas, picture.width, picture.height);
			this.useSurface("dom");
		}
		this.shownChanged(previous);
	}

	showElement(picture, previous) {
		this.shown = { width: picture.width, height: picture.height, format: picture.format, pixels: null, hdr: false };
		this.dom.setElement(picture.element, picture.width, picture.height, { vector: picture.vector });
		this.useSurface("dom");
		this.shownChanged(previous);
	}

	useSurface(name) {
		this.surface = name;
		this.canvas.hidden = name !== "gl";
		this.dom.frame.hidden = name !== "dom";
		const channels = name === "gl";
		for (const button of this.channelButtons) button.disabled = !channels;
		this.exposure.hidden = !this.shown.hdr;
	}

	// A new picture keeps the view of the last: the same area after a mip change, the same zoom after a reload.
	shownChanged(previous) {
		if (previous && this.state && !this.fitting && (previous.width !== this.shown.width || previous.height !== this.shown.height))
			this.state = rescaled(this.state, previous, this.shown);
		const texture = this.picture && this.picture.kind === "texture" ? this.picture.texture : null;
		replace(this.info, describe(this.shown, texture, this.bytes).map((part) => h("span.ft-chip", null, part)));
		this.info.title = this.path;
		this.queueDraw();
	}

	renderSelects() {
		const texture = this.picture && this.picture.kind === "texture" ? this.picture.texture : null;
		if (!texture) return replace(this.selects);
		const select = (title, count, value, label, change) => (count > 1
			? h("select.ft-select", { title, on: { change: (event) => change(Number(event.target.value)) } },
				Array.from({ length: count }, (_, i) => h("option", { value: String(i), selected: i === value ? "" : null }, label(i))))
			: null);
		const depth = Math.max(1, texture.depth >> this.place.level);
		return replace(this.selects,
			select(texture.cube ? "Cube face" : "Array element", texture.layers, this.place.layer, (i) => texture.layerName(i), (v) => this.moveTo({ layer: v })),
			select("Mip level", texture.levels, this.place.level, (i) => "Mip " + i + " (" + Math.max(1, texture.width >> i) + " x " + Math.max(1, texture.height >> i) + ")", (v) => this.moveTo({ level: v })),
			select("Depth slice", depth, this.place.slice, (i) => "Slice " + i, (v) => this.moveTo({ slice: v })));
	}

	moveTo(change) {
		this.place = clampPlace(Object.assign({}, this.place, change), this.picture.texture);
		this.showTextureImage(this.shown);
		this.renderSelects();
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Drawing

	queueDraw() {
		if (this.frame) return;
		this.frame = requestAnimationFrame(() => {
			this.frame = 0;
			this.draw();
		});
	}

	draw() {
		if (!this.shown) return;
		const view = { width: this.stage.clientWidth, height: this.stage.clientHeight };
		if (!view.width || !view.height) return;
		this.state = kept(this.current(view), view, this.shown);
		if (this.surface === "gl") this.gl.draw(this.state, Object.assign({ colors: this.colors() }, this.options));
		else this.dom.draw(this.state, this.options);
		this.zoomText.textContent = zoomLabel(this.state.zoom);
		this.zoomText.title = this.fitting ? "Fitted to the view: click for actual size (1)" : "Click to fit to the view (0)";
	}

	colors() {
		const style = getComputedStyle(this.stage);
		const token = (name) => rgbOf(style.getPropertyValue(name).trim());
		return { outside: token("--editor-bg"), checkA: token("--bg-2"), checkB: token("--bg-4"), grid: token("--text-3") };
	}

	showTexel(px, py) {
		if (px === null || !this.shown || !this.state) {
			this.readout.textContent = "";
			return;
		}
		const at = texelAt(this.state, this.shown, px, py);
		if (!at) {
			this.readout.textContent = "";
			return;
		}
		let value = null;
		if (this.shown.pixels) value = Array.from(this.shown.pixels.data.subarray((at.y * this.shown.width + at.x) * 4, (at.y * this.shown.width + at.x) * 4 + 4));
		else if (this.surface === "gl") value = this.gl.readTexel(at.x, at.y);
		this.readout.textContent = texelText(at, value, Boolean(this.shown.pixels && this.shown.pixels.data instanceof Float32Array));
	}

	toggleChannel(index) {
		this.options.channels[index] = !this.options.channels[index];
		this.channelButtons[index].classList.toggle("on", this.options.channels[index]);
		this.channelButtons[index].setAttribute("aria-pressed", String(this.options.channels[index]));
		this.queueDraw();
	}

	toggleOption(name) {
		this.options[name] = !this.options[name];
		(name === "checker" ? this.checkerButton : this.gridButton).classList.toggle("on", this.options[name]);
		this.queueDraw();
	}

	setExposure(stops) {
		this.options.exposure = stops;
		this.exposureInput.value = String(stops);
		this.exposureText.textContent = "EV " + (stops > 0 ? "+" : "") + stops.toFixed(1);
		if (this.surface === "dom" && this.shown && this.shown.pixels) this.dom.setElement(canvasOf(this.shown.pixels, stops), this.shown.width, this.shown.height);
		this.queueDraw();
	}

	problem(message) {
		this.shown = null;
		this.canvas.hidden = true;
		this.dom.frame.hidden = true;
		this.note.hidden = false;
		replace(this.note, notice(message, [
			openWithButton(this.ctx, this.path, "hex", "Open in Hex Viewer"),
			this.ext === "svg" ? openWithButton(this.ctx, this.path, "doc", "Open in Text Editor") : null,
			h("button.btn", { on: { click: () => saveFile(this.ctx, this.path) } }, icon("cloud-download"), " Download"),
		]));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The tab

	visible(shown) {
		if (shown) this.queueDraw();
	}

	focus() {
		this.stage.focus({ preventScroll: true });
	}

	dispose() {
		this.generation++;
		cancelAnimationFrame(this.frame);
		clearTimeout(this.svgTimer);
		this.detachInput();
		this.resizer.disconnect();
		if (this.stopWatch) this.stopWatch();
		if (this.sync) this.sync.dispose();
		release(this.picture);
		if (this.gl) this.gl.dispose();
		this.dom.dispose();
	}
}

function clampPlace(place, texture) {
	const level = Math.min(Math.max(0, place.level), texture.levels - 1);
	return { layer: Math.min(Math.max(0, place.layer), texture.layers - 1), level, slice: Math.min(Math.max(0, place.slice), Math.max(1, texture.depth >> level) - 1) };
}

function release(picture) {
	if (!picture) return;
	if (picture.bitmap) picture.bitmap.close();
	if (picture.url) URL.revokeObjectURL(picture.url);
}
