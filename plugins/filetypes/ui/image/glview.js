// glview.js - a picture drawn by WebGL2 into a canvas the size of its view: nearest texels when zoomed in and mipmapped
// when zoomed out, channels shown alone or masked, alpha over a checkerboard, a texel grid from 8x, and a float
// picture's exposure, sRGB-encoded. One full-view triangle; the fragment shader finds its texel.
//     const view = new GlView(canvas);
//     if (view.ok) view.setPixels({ width, height, data });   // RGBA8 or RGBA floats
//     view.draw(state, { channels: [1, 1, 1, 1], checker: true, grid: true, exposure: 0, colors });

const VERTEX = `#version 300 es
void main() {
	gl_Position = vec4(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0, 0.0, 1.0);
}`;

const FRAGMENT = `#version 300 es
precision highp float;
uniform highp sampler2D image;
uniform vec2 imageSize;
uniform vec2 offset;
uniform float zoom;
uniform float viewHeight;
uniform vec4 channels;
uniform int hdr;
uniform float exposure;
uniform int checker;
uniform float checkerSize;
uniform int grid;
uniform vec3 outside;
uniform vec3 checkA;
uniform vec3 checkB;
uniform vec3 gridColor;
out vec4 color;
vec3 encode(vec3 c) {
	c = max(c, vec3(0.0));
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
void main() {
	vec2 p = vec2(gl_FragCoord.x, viewHeight - gl_FragCoord.y);
	vec2 texel = offset + p / zoom;
	if (texel.x < 0.0 || texel.y < 0.0 || texel.x >= imageSize.x || texel.y >= imageSize.y) {
		color = vec4(outside, 1.0);
		return;
	}
	vec3 back = outside;
	if (checker == 1) {
		vec2 cell = floor(p / checkerSize);
		back = mod(cell.x + cell.y, 2.0) < 1.0 ? checkA : checkB;
	}
	vec4 t = texture(image, texel / imageSize);
	if (hdr == 1) t.rgb = encode(t.rgb * exp2(exposure));
	float shown = channels.r + channels.g + channels.b;
	vec3 rgb;
	if (shown < 0.5) rgb = vec3(t.a);
	else if (shown < 1.5) rgb = vec3(dot(t.rgb, channels.rgb));
	else rgb = t.rgb * channels.rgb;
	float alpha = channels.a > 0.5 && shown > 0.5 ? clamp(t.a, 0.0, 1.0) : 1.0;
	vec3 c = mix(back, clamp(rgb, 0.0, 1.0), alpha);
	if (grid == 1 && zoom >= 8.0) {
		vec2 f = fract(texel) * zoom;
		if (f.x < 1.0 || f.y < 1.0) c = mix(c, gridColor, 0.45);
	}
	color = vec4(c, 1.0);
}`;

const UNIFORMS = ["image", "imageSize", "offset", "zoom", "viewHeight", "channels", "hdr", "exposure", "checker", "checkerSize", "grid", "outside", "checkA", "checkB", "gridColor"];
const CHECKER_CSS_PX = 8;

export class GlView {
	constructor(canvas, onRestored) {
		this.canvas = canvas;
		this.input = null;
		this.size = null;
		canvas.addEventListener("webglcontextlost", (event) => event.preventDefault());
		canvas.addEventListener("webglcontextrestored", () => {
			this.setup();
			if (this.input) this.upload(this.input);
			if (onRestored) onRestored();
		});
		this.setup();
	}

	get ok() {
		return Boolean(this.gl && this.program);
	}

	get maxSize() {
		return this.gl ? this.gl.getParameter(this.gl.MAX_TEXTURE_SIZE) : 0;
	}

	setup() {
		const gl = this.canvas.getContext("webgl2", { antialias: false, alpha: false, premultipliedAlpha: false, preserveDrawingBuffer: true });
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
		if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
		this.program = program;
		this.uniforms = Object.fromEntries(UNIFORMS.map((name) => [name, gl.getUniformLocation(program, name)]));
		this.texture = gl.createTexture();
		this.framebuffer = gl.createFramebuffer();
	}

	// Decoded pixels: {width, height, data: Uint8Array (RGBA8) | Float32Array (RGBA floats)}.
	setPixels(pixels) {
		this.upload({ pixels });
	}

	// A decoded browser picture (an ImageBitmap): read back texel by texel through readTexel.
	setSource(source, width, height) {
		this.upload({ source, width, height });
	}

	upload(input) {
		this.input = input;
		const gl = this.gl;
		if (!gl) return;
		gl.bindTexture(gl.TEXTURE_2D, this.texture);
		gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
		gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
		gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
		gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
		const pixels = input.pixels;
		const float = pixels && pixels.data instanceof Float32Array;
		if (pixels && float) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, pixels.width, pixels.height, 0, gl.RGBA, gl.FLOAT, pixels.data);
		else if (pixels) gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, pixels.width, pixels.height, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels.data);
		else gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, input.source);
		if (!float) gl.generateMipmap(gl.TEXTURE_2D);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, float ? gl.LINEAR : gl.LINEAR_MIPMAP_LINEAR);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
		gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
		this.size = pixels ? { width: pixels.width, height: pixels.height } : { width: input.width, height: input.height };
		this.hdr = float;
	}

	// The canvas drawn for `state` (zoom and offset in CSS pixels, as viewport.js keeps them).
	draw(state, options) {
		const gl = this.gl;
		if (!gl || !this.size || gl.isContextLost()) return;
		const ratio = window.devicePixelRatio || 1;
		const width = Math.max(1, Math.round(this.canvas.clientWidth * ratio));
		const height = Math.max(1, Math.round(this.canvas.clientHeight * ratio));
		if (this.canvas.width !== width || this.canvas.height !== height) {
			this.canvas.width = width;
			this.canvas.height = height;
		}
		gl.viewport(0, 0, width, height);
		gl.useProgram(this.program);
		gl.activeTexture(gl.TEXTURE0);
		gl.bindTexture(gl.TEXTURE_2D, this.texture);
		const u = this.uniforms;
		const c = options.colors;
		gl.uniform1i(u.image, 0);
		gl.uniform2f(u.imageSize, this.size.width, this.size.height);
		gl.uniform2f(u.offset, state.x, state.y);
		gl.uniform1f(u.zoom, state.zoom * ratio);
		gl.uniform1f(u.viewHeight, height);
		gl.uniform4f(u.channels, ...options.channels.map((on) => (on ? 1 : 0)));
		gl.uniform1i(u.hdr, this.hdr ? 1 : 0);
		gl.uniform1f(u.exposure, options.exposure || 0);
		gl.uniform1i(u.checker, options.checker ? 1 : 0);
		gl.uniform1f(u.checkerSize, CHECKER_CSS_PX * ratio);
		gl.uniform1i(u.grid, options.grid ? 1 : 0);
		gl.uniform3f(u.outside, ...c.outside);
		gl.uniform3f(u.checkA, ...c.checkA);
		gl.uniform3f(u.checkB, ...c.checkB);
		gl.uniform3f(u.gridColor, ...c.grid);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}

	// A browser picture's texel as stored: [r, g, b, a], 0..255.
	readTexel(x, y) {
		const gl = this.gl;
		if (!gl || !this.input || !this.input.source) return null;
		gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
		gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, this.texture, 0);
		const out = new Uint8Array(4);
		gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, out);
		gl.bindFramebuffer(gl.FRAMEBUFFER, null);
		return Array.from(out);
	}

	dispose() {
		const gl = this.gl;
		if (gl && !gl.isContextLost()) {
			gl.deleteTexture(this.texture);
			gl.deleteFramebuffer(this.framebuffer);
			gl.deleteProgram(this.program);
			const lose = gl.getExtension("WEBGL_lose_context");
			if (lose) lose.loseContext();
		}
		this.input = null;
	}
}
