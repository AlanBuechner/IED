// panels.js - what the Pipeline view can show in the resource viewer's place: a pipeline's shader (its disassembly and
// reflection: bindings, constant buffers, inputs and outputs) and a draw's vertex inputs (the Mesh Viewer's table,
// F-GPU-33).

import { h, icon, replace } from "/ui-kit/dom.js";
import { DOT, value } from "./format.js";

export class ShaderPanel {
	constructor({ onClose }) {
		this.title = h("b");
		this.reflection = h("div.refl");
		this.code = h("pre");
		this.el = h("div.gpu-shader", null,
			h("div.gpu-rv-tools", null, icon("symbol-method"), this.title, h("span.grow"),
				h("button.ib", { title: "Back to the resource", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			this.reflection, this.code);
	}

	message(text) {
		this.title.textContent = "Shader";
		replace(this.reflection);
		this.code.textContent = text;
	}

	show(shader) {
		const stage = { vs: "Vertex", ps: "Pixel", cs: "Compute", hs: "Hull", ds: "Domain", gs: "Geometry", as: "Amplification", ms: "Mesh" }[shader.stage] || shader.stage;
		this.title.textContent = stage + " shader " + DOT + " " + shader.pipeline + " " + DOT + " " + (shader.model || shader.kind) + " " + DOT + " " + shader.size + " B";
		const rows = [];
		const section = (name, items, render) => {
			if (!items || !items.length) return;
			rows.push(h("div.gpu-srow.hd", null, h("span.k", null, name), h("span.v", null, String(items.length))));
			for (const item of items) rows.push(render(item));
		};
		section("Bindings", shader.bindings, (b) => h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, b.register + (b.space ? " space" + b.space : "")), h("span.v", null, b.name + " " + DOT + " " + b.type + (b.count > 1 ? " [" + b.count + "]" : ""))));
		section("Constant buffers", shader.constantBuffers, (c) => h("div", null,
			h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, c.name), h("span.v", null, c.size + " B")),
			(c.variables || []).map((v) => h("div.gpu-srow", { style: { "--d": 2 } }, h("span.k", null, v.type + " " + v.name), h("span.v", null, "+" + v.offset)))));
		section("Inputs", shader.inputs, (p) => h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, p.semantic), h("span.v", null, "v" + p.register)));
		section("Outputs", shader.outputs, (p) => h("div.gpu-srow", { style: { "--d": 1 } }, h("span.k", null, p.semantic), h("span.v", null, "o" + p.register)));
		replace(this.reflection, rows);
		this.reflection.hidden = !rows.length;
		this.code.textContent = shader.disassembly || shader.problem || "No disassembly: this shader's compiler is not available here.";
	}
}

export class MeshPanel {
	constructor({ onClose }) {
		this.title = h("b");
		this.table = h("div.gpu-mesh-table");
		this.el = h("div.gpu-shader", null,
			h("div.gpu-rv-tools", null, icon("symbol-structure"), this.title, h("span.grow"),
				h("button.ib", { title: "Back to the resource", "aria-label": "Close", on: { click: onClose } }, icon("close"))),
			this.table);
	}

	message(text) {
		this.title.textContent = "Vertex input";
		replace(this.table, h("div.gpu-state-msg", null, text));
	}

	// The replayer's mesh result: attributes, the vertices' float4s attribute after attribute, the draw's indices.
	show(mesh, data) {
		const floats = new Float32Array(data);
		const attributes = mesh.attributes || [];
		this.title.textContent = "Vertex input " + DOT + " EID " + mesh.eid + " " + DOT + " " + mesh.count + " vertices " + DOT + " " + (mesh.indices || []).length + " indices " + DOT + " " + mesh.topology;
		const head = h("tr", null, h("th", null, "VTX"), attributes.map((a) => h("th", { colspan: String(Math.max(1, a.components)) }, a.name + a.index + " (" + a.format + ")")));
		const shown = Math.min(mesh.count, 2000);
		const rows = [];
		for (let v = 0; v < shown; v++) {
			const cells = [h("td", null, String(mesh.first + v))];
			attributes.forEach((a, ai) => {
				const at = (ai * mesh.count + v) * 4;
				for (let c = 0; c < Math.max(1, a.components); c++) cells.push(h("td", null, value(floats[at + c])));
			});
			rows.push(h("tr", null, cells));
		}
		replace(this.table, h("table", null, h("thead", null, head), h("tbody", null, rows)),
			mesh.count > shown ? h("div.gpu-state-msg", null, "The first " + shown + " of " + mesh.count + " vertices.") : null);
	}
}
