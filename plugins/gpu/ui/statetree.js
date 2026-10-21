// statetree.js - the Pipeline view's state tree (F-GPU-20, F-GPU-21): the event's state by stage as the replayer
// describes it - groups with a summary, items with a value, nested items for tables and layouts. An item naming a
// resource shows it in the resource viewer; one naming a shader stage shows the shader.

import { h, icon, replace } from "/ui-kit/dom.js";

export class StateTree {
	constructor({ onResource, onShader }) {
		this.onResource = onResource;
		this.onShader = onShader;
		this.closed = new Set(["Descriptor Heaps"]);
		this.viewport = h("div.viewport");
		this.title = h("span.muted");
		this.el = h("div.gpu-col.gpu-stree.gpu-br", null, h("div.gpu-title", null, h("b", null, "State"), this.title), this.viewport);
		this.selectedKey = null;
	}

	message(text, error) {
		replace(this.viewport, h("div.gpu-state-msg" + (error ? ".error" : ""), null, text));
	}

	show(state) {
		this.state = state;
		this.title.textContent = "EID " + state.eid + (state.pipeline ? " \u00b7 " + state.pipeline : "");
		const rows = [];
		for (const group of state.groups || []) {
			const open = !this.closed.has(group.name);
			rows.push(h("div.gpu-srow.hd", { style: { "--d": 0 }, on: { click: () => this.toggle(group.name) } },
				icon(open ? "chevron-down" : "chevron-right"), h("span.k", null, group.name), h("span.v", null, group.summary || "")));
			if (open) for (const item of group.items || []) this.item(rows, item, 1, group.name + "/" + item.label);
		}
		replace(this.viewport, rows);
	}

	item(rows, item, depth, key) {
		const children = item.children || [];
		const open = children.length && !this.closed.has(key);
		const target = item.resource ? "resource" : item.shader ? "shader" : null;
		const row = h("div.gpu-srow" + (target ? ".link" : "") + (key === this.selectedKey ? ".sel" : ""), {
			style: { "--d": depth },
			title: item.value,
			on: {
				click: () => {
					if (children.length) this.toggle(key);
					if (item.resource) this.pick(key, () => this.onResource(item.resource, item));
					else if (item.shader) this.pick(key, () => this.onShader(item.shader));
				},
			},
		},
			children.length ? icon(open ? "chevron-down" : "chevron-right") : h("span.ci.blank"),
			h("span.k", null, item.label),
			h("span.v", null, item.value));
		rows.push(row);
		if (open) for (const child of children) this.item(rows, child, depth + 1, key + "/" + child.label);
	}

	pick(key, action) {
		this.selectedKey = key;
		action();
		if (this.state) this.show(this.state);
	}

	toggle(key) {
		if (this.closed.has(key)) this.closed.delete(key);
		else this.closed.add(key);
		if (this.state) this.show(this.state);
	}

	// The resource the event writes first: its first render target, else its depth target, else the first UAV it binds.
	static outputOf(state) {
		let first = null, depth = null, uav = null;
		const visit = (items) => {
			for (const item of items || []) {
				if (item.resource) {
					if (!first && /^RTV/.test(item.value)) first = item;
					if (!depth && /^DSV/.test(item.value)) depth = item;
					if (!uav && /^UAV/.test(item.value)) uav = item;
				}
				visit(item.children);
			}
		};
		for (const group of state.groups || []) visit(group.items);
		return first || depth || uav;
	}
}
