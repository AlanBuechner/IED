// texview.js - the resource viewer (F-GPU-30, F-GPU-31): a texture the replayer read after an event, as RGBA32F, drawn
// by WebGL2 so range, channels, gamma and NaN/Inf/negative highlighting change at once, on any device. Wheel zooms
// around the pointer, a drag pans, a double click fits; a click picks the texel, whose exact typed value the replayer
// reads (the status line shows it).

import { h, icon, replace } from "/ui-kit/dom.js";
import { value } from "./format.js";

const VERTEX = `#version 300 es
in vec2 corner;
uniform vec4 placement;   // x, y, width, height in clip space
out vec2 uv;
void main() {
	uv = corner;
	gl_Position = vec4(placement.xy + corner * placement.zw, 0.0, 1.0);
}`;

const FRAGMENT = `#version 300 es
precision highp float;
uniform highp sampler2D image;
uniform vec2 range;
uniform vec4 channels;
uniform int mode;      // 0 normal, 1 NaN/Inf/negative
uniform int gammaOn;
in vec2 uv;
out vec4 color;
vec3 encode(vec3 c) {
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
	vec4 t = texture(image, uv);
	bvec4 nan = isnan(t);
	bvec4 inf = isinf(t);
	if (mode == 1) {
		if (any(nan)) { color = vec4(1.0, 0.0, 1.0, 1.0); return; }
		if (any(inf)) { color = vec4(1.0, 1.0, 0.0, 1.0); return; }
		if (any(lessThan(t, vec4(0.0)))) { color = vec4(0.0, 0.6, 1.0, 1.0); return; }
	}
	vec4 v = clamp((t - range.x) / max(range.y - range.x, 1e-20), 0.0, 1.0);
	float shown = channels.r + channels.g + channels.b;
	vec3 rgb;
	if (shown < 0.5) rgb = vec3(v.a);
	else if (shown < 1.5) rgb = vec3(dot(v.rgb, channels.rgb));
	else rgb = v.rgb * channels.rgb;
	if (gammaOn == 1) rgb = encode(rgb);
	color = vec4(rgb, 1.0);
}`;

export class TextureViewer {
	constructor({ onPick }) {
		this.onPick = onPick;
		this.channels = [1, 1, 1, 0];
		this.mode = 0;
		this.gamma = true;
		this.range = [0, 1];
		this.zoom = 0;         // screen pixels per texel; 0 fits
		this.offset = [0, 0];  // texel at the view's top-left
		this.info = null;
		this.canvas = h("canvas", { tabindex: "0" });
		this.marker = h("div.gpu-pick", { hidden: true });
		this.note = h("div.gpu-state-msg", { hidden: true });
		this.view = h("div.gpu-rv-view", null, this.canvas, this.marker, this.note);
		this.nameLabel = h("b.ellip");
		this.mipSelect = h("select.gpu-wide", { title: "Mip", on: { change: () => this.onChange("mip", +this.mipSelect.value) } });
		this.sliceSelect = h("select.gpu-wide", { title: "Slice", on: { change: () => this.onChange("slice", +this.sliceSelect.value) } });
		this.channelButtons = ["R", "G", "B", "A"].map((c, i) => h("button" + (this.channels[i] ? ".on" : ""), { title: "Channel " + c, on: { click: () => this.toggleChannel(i) } }, c));
		this.visualize = h("select", { title: "Visualize", on: { change: () => { this.mode = +this.visualize.value; this.draw(); } } },
			h("option", { value: "0" }, "Visualize: Color"), h("option", { value: "1" }, "Visualize: NaN / Inf / negative"));
		this.minInput = h("input", { type: "number", step: "any", title: "Range low", on: { change: () => this.setRange(+this.minInput.value, this.range[1]) } });
		this.maxInput = h("input", { type: "number", step: "any", title: "Range high", on: { change: () => this.setRange(this.range[0], +this.maxInput.value) } });
		this.gammaButton = h("button.ib.gpu-wide", { title: "Gamma: show linear values sRGB-encoded", on: { click: () => { this.gamma = !this.gamma; this.draw(); this.tools(); } } }, icon("color-mode"));
		this.toolbar = h("div.gpu-rv-tools", null, this.nameLabel, this.mipSelect, this.sliceSelect, h("span.gpu-chan", null, this.channelButtons), this.visualize,
			h("span.range.gpu-wide", null, "Range"), this.minInput, this.maxInput,
			h("button.ib.gpu-wide", { title: "Fit the range to the texture's values", on: { click: () => this.fitRange() } }, icon("arrow-both")),
			this.gammaButton, h("span.grow"),
			h("button.ib", { title: "Fit to the view (double click)", on: { click: () => this.fit() } }, icon("screen-normal")));
		this.status = h("div.gpu-rv-status");
		this.el = h("div.gpu-rv", null, this.toolbar, this.view, this.status);
		this.onChange = () => {};
		this.setupGl();
		this.input();
		new ResizeObserver(() => this.draw()).observe(this.view);
	}

	setupGl() {
		const gl = this.canvas.getContext("webgl2", { antialias: false, premultipliedAlpha: false });
		this.gl = gl;
		if (!gl) return;
		const compile = (type, source) => {
			const shader = gl.createShader(type);
			gl.shaderSource(shader, source);
			gl.compileShader(shader);
			if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
			return shader;
		};
		const program = gl.createProgram();
		gl.attachShader(program, compile(gl.VERTEX_SHADER, VERTEX));
		gl.attachShader(program, compile(gl.FRAGMENT_SHADER, FRAGMENT));
		gl.linkProgram(program);
		this.program = program;
		const buffer = gl.createBuffer();
		gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
		gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);
		const vao = gl.createVertexArray();
		gl.bindVertexArray(vao);
		const corner = gl.getAttribLocation(program, "corner");
		gl.enableVertexAttribArray(corner);
		gl.vertexAttribPointer(corner, 2, gl.FLOAT, false, 0, 0);
		this.vao = vao;
		this.texture = gl.createTexture();
		this.uniforms = Object.fromEntries(["placement", "image", "range", "channels", "mode", "gammaOn"].map((n) => [n, gl.getUniformLocation(program, n)]));
	}

	message(text) {
		this.note.hidden = !text;
		this.note.textContent = text || "";
		if (text) {
			this.info = null;
			this.marker.hidden = true;
			replace(this.status);
			this.draw();
		}
	}

	// `info` is the replayer's texture result, `data` its RGBA32F texels.
	show(info, data) {
		const first = !this.info || this.info.id !== info.id;
		this.info = info;
		this.data = new Float32Array(data);
		this.note.hidden = true;
		this.nameLabel.textContent = info.name;
		this.nameLabel.title = info.name + " \u00b7 " + info.srcWidth + " x " + info.srcHeight + " \u00b7 " + info.format;
		this.gamma = !!info.linear;
		if (first) {
			this.fitRange();
			this.zoom = 0;
		}
		if (!this.gl) {
			this.message("This browser has no WebGL2: textures cannot be shown here.");
			return;
		}
		const gl = this.gl;
		gl.bindTexture(gl.TEXTURE_2D, this.texture);
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
		gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, info.width, info.height, 0, gl.RGBA, gl.FLOAT, this.data);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		this.tools();
		this.draw();
		this.describe(null);
	}

	tools() {
		const info = this.info;
		if (!info) return;
		const options = (count, selected) => Array.from({ length: count }, (_, i) => h("option", { value: String(i), selected: i === selected }, String(i)));
		replace(this.mipSelect, options(info.mips || 1, info.mip || 0).map((o, i) => (o.textContent = "Mip " + i, o)));
		this.mipSelect.hidden = (info.mips || 1) <= 1;
		replace(this.sliceSelect, options(info.slices || 1, info.slice || 0).map((o, i) => (o.textContent = "Slice " + i, o)));
		this.sliceSelect.hidden = (info.slices || 1) <= 1;
		this.channelButtons.forEach((b, i) => b.classList.toggle("on", !!this.channels[i]));
		this.gammaButton.classList.toggle("on", this.gamma);
		this.minInput.value = String(+this.range[0].toPrecision(6));
		this.maxInput.value = String(+this.range[1].toPrecision(6));
	}

	toggleChannel(i) {
		this.channels[i] = this.channels[i] ? 0 : 1;
		this.tools();
		this.draw();
	}

	setRange(low, high) {
		if (!Number.isFinite(low) || !Number.isFinite(high)) return;
		this.range = [low, high];
		this.tools();
		this.draw();
	}

	// The range covering the visible channels' values (0..1 for values inside it, as PIX starts).
	fitRange() {
		const info = this.info;
		if (!info) return;
		let low = Infinity, high = -Infinity;
		for (let c = 0; c < 3; c++) {
			if (!this.channels[c] || c >= (info.channels || 4)) continue;
			low = Math.min(low, info.min[c]);
			high = Math.max(high, info.max[c]);
		}
		if (!Number.isFinite(low)) low = 0, high = 1;
		if (low >= 0 && high <= 1) low = 0, high = Math.max(high, low + 1e-6) > 0 && high > 0.5 ? 1 : Math.max(high, 1e-6);
		if (high <= low) high = low + 1;
		this.setRange(low, high);
	}

	fit() {
		this.zoom = 0;
		this.draw();
	}

	// Screen pixels per texel and the texel at the top-left, fitting when zoom is 0.
	placement() {
		const w = this.view.clientWidth, h2 = this.view.clientHeight;
		const info = this.info;
		if (!this.zoom) {
			const scale = Math.min(w / info.width, h2 / info.height) * 0.98;
			return { scale, x: (w - info.width * scale) / 2, y: (h2 - info.height * scale) / 2 };
		}
		return { scale: this.zoom, x: -this.offset[0] * this.zoom, y: -this.offset[1] * this.zoom };
	}

	draw() {
		const gl = this.gl;
		if (!gl) return;
		const ratio = window.devicePixelRatio || 1;
		const w = Math.max(1, this.view.clientWidth), hgt = Math.max(1, this.view.clientHeight);
		this.canvas.width = Math.round(w * ratio);
		this.canvas.height = Math.round(hgt * ratio);
		gl.viewport(0, 0, this.canvas.width, this.canvas.height);
		const background = getComputedStyle(this.view).backgroundColor.match(/[\d.]+/g) || [0, 0, 0];
		gl.clearColor(background[0] / 255, background[1] / 255, background[2] / 255, 1);
		gl.clear(gl.COLOR_BUFFER_BIT);
		if (!this.info) return;
		const p = this.placement();
		const left = p.x / w * 2 - 1, top = 1 - p.y / hgt * 2;
		const width = this.info.width * p.scale / w * 2, height = this.info.height * p.scale / hgt * 2;
		gl.useProgram(this.program);
		gl.bindVertexArray(this.vao);
		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.texture);
		gl.uniform4f(this.uniforms.placement, left, top, width, -height);
		gl.uniform1i(this.uniforms.image, 0);
		gl.uniform2f(this.uniforms.range, this.range[0], this.range[1]);
		gl.uniform4f(this.uniforms.channels, ...this.channels);
		gl.uniform1i(this.uniforms.mode, this.mode);
		gl.uniform1i(this.uniforms.gammaOn, this.gamma ? 1 : 0);
		gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
		this.placeMarker();
	}

	// The source texel under a view point, or null outside the texture.
	texelAt(clientX, clientY) {
		if (!this.info) return null;
		const rect = this.view.getBoundingClientRect();
		const p = this.placement();
		const tx = (clientX - rect.left - p.x) / p.scale, ty = (clientY - rect.top - p.y) / p.scale;
		if (tx < 0 || ty < 0 || tx >= this.info.width || ty >= this.info.height) return null;
		const sx = Math.min(this.info.srcWidth - 1, Math.floor(tx * this.info.srcWidth / this.info.width));
		const sy = Math.min(this.info.srcHeight - 1, Math.floor(ty * this.info.srcHeight / this.info.height));
		return { x: sx, y: sy, shown: [Math.floor(tx), Math.floor(ty)] };
	}

	placeMarker() {
		if (!this.picked || !this.info) {
			this.marker.hidden = true;
			return;
		}
		const p = this.placement();
		const tx = (this.picked.x + 0.5) * this.info.width / this.info.srcWidth, ty = (this.picked.y + 0.5) * this.info.height / this.info.srcHeight;
		this.marker.hidden = false;
		this.marker.style.left = p.x + tx * p.scale + "px";
		this.marker.style.top = p.y + ty * p.scale + "px";
	}

	input() {
		let drag = null;
		this.view.addEventListener("wheel", (e) => {
			if (!this.info) return;
			e.preventDefault();
			const rect = this.view.getBoundingClientRect();
			const p = this.placement();
			const cx = e.clientX - rect.left, cy = e.clientY - rect.top;
			const tx = (cx - p.x) / p.scale, ty = (cy - p.y) / p.scale;
			const scale = Math.min(64, Math.max(0.02, p.scale * (e.deltaY < 0 ? 1.25 : 0.8)));
			this.zoom = scale;
			this.offset = [tx - cx / scale, ty - cy / scale];
			this.draw();
		}, { passive: false });
		this.view.addEventListener("pointerdown", (e) => {
			if (!this.info || e.button !== 0) return;
			const p = this.placement();
			drag = { x: e.clientX, y: e.clientY, moved: false, start: [-p.x / p.scale, -p.y / p.scale], scale: p.scale };
			this.view.setPointerCapture(e.pointerId);
		});
		this.view.addEventListener("pointermove", (e) => {
			if (drag) {
				const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
				if (Math.abs(dx) + Math.abs(dy) > 3) drag.moved = true;
				if (drag.moved) {
					this.zoom = drag.scale;
					this.offset = [drag.start[0] - dx / drag.scale, drag.start[1] - dy / drag.scale];
					this.draw();
				}
				return;
			}
			const texel = this.texelAt(e.clientX, e.clientY);
			if (texel) this.describe(texel);
		});
		this.view.addEventListener("pointerup", (e) => {
			if (drag && !drag.moved) {
				const texel = this.texelAt(e.clientX, e.clientY);
				if (texel) {
					this.picked = texel;
					this.placeMarker();
					this.onPick(texel.x, texel.y);
				}
			}
			drag = null;
		});
		this.view.addEventListener("dblclick", () => this.fit());
	}

	// The status line: the texel under the pointer (its displayed value), then the pick's exact value.
	describe(texel) {
		const info = this.info;
		if (!info) return;
		const parts = [];
		if (texel) {
			const i = (texel.shown[1] * info.width + texel.shown[0]) * 4;
			const v = Array.from(this.data.subarray(i, i + 4)).slice(0, Math.max(1, info.channels || 4));
			parts.push(h("span", null, h("span.k", null, "Pixel"), "(" + texel.x + ", " + texel.y + ")"));
			parts.push(h("span", null, h("span.k", null, info.depth ? "Depth" : "RGBA"), v.map((x) => h("span" + (Number.isNaN(x) || !Number.isFinite(x) ? ".nan" : ""), null, value(x) + " "))));
		}
		if (this.pickText) parts.push(h("span", null, h("span.k", null, "Pick"), this.pickText));
		parts.push(h("span", null, h("span.k", null, "Texture"), info.name + " \u00b7 " + info.srcWidth + "x" + info.srcHeight + " \u00b7 " + info.view + (info.step > 1 ? " \u00b7 shown 1:" + info.step : "")));
		if (info.nan || info.inf) parts.push(h("span.nan", null, info.nan + " NaN, " + info.inf + " Inf"));
		replace(this.status, parts);
	}

	setPick(text) {
		this.pickText = text;
		this.describe(null);
	}
}
