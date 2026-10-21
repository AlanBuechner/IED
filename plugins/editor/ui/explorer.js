// explorer.js - Solution Explorer (F-WSP, docs/15): a solution shows its groups, projects (each its root folder) and
// Solution Items; a folder or file workspace shows its folder. Folders load when opened; tree.changed keeps them
// current. A click opens a file in a preview tab, a double click or Enter keeps it open (VS).

import { h, icon } from "/ui-kit/dom.js";
import { TreeView } from "/ui-kit/tree.js";
import { fileIcon, folderIcon } from "./fileicons.js";

function parentOf(path) {
	const slash = path.lastIndexOf("/");
	return slash < 0 ? "" : path.slice(0, slash);
}

function baseName(path) {
	return path.slice(path.lastIndexOf("/") + 1);
}

// `root`'s path inside the workspace `wsRoot` ("" for the root itself), or null when it is outside.
export function relativeTo(wsRoot, root) {
	const norm = (p) => p.replace(/\\/g, "/").replace(/\/+$/, "");
	const a = norm(wsRoot);
	const b = norm(root);
	if (a.toLowerCase() === b.toLowerCase()) return "";
	if (!b.toLowerCase().startsWith(a.toLowerCase() + "/")) return null;
	return b.slice(a.length + 1);
}

export class Explorer {
	constructor(ctx, mounted) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.mounted = mounted;
		this.folders = new Map();
		const tools = h("div.ed-tools", null,
			h("span.grow"),
			h("button.ib", { title: "Collapse All", "aria-label": "Collapse All", on: { click: () => this.tree.collapseAll() } }, icon("collapse-all")),
			h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => this.refresh() } }, icon("refresh")));
		const fill = h("div.ed-fill.explorer");
		ctx.el.append(tools, fill);
		this.tree = new TreeView(fill, {
			roots: () => this.roots(),
			children: (node) => this.children(node),
			render: (node, row) => this.renderRow(node, row),
			label: (node) => node.name,
			open: (node, options) => this.open(node, options),
			menu: (node, at) => this.menu(node, at),
			select: (node) => this.ied.context.set("explorerItemKind", node.kind),
		});
		this.stopWorkspace = this.ied.workspace.on(() => this.refresh());
		this.stopTopic = this.ied.client.subscribe("ws:" + ctx.wsId, { evt: (kind, data) => this.workspaceEvent(kind, data) });
		mounted.add(this);
		this.refresh();
	}

	dispose() {
		this.mounted.delete(this);
		this.stopWorkspace();
		this.stopTopic();
		this.tree.dispose();
	}

	focus() {
		this.tree.focus();
	}

	refresh() {
		this.folders.clear();
		return this.tree.refresh().catch((e) => this.ied.report("Solution Explorer: " + e.message, e));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Nodes

	async roots() {
		const ws = this.ied.workspace;
		if (ws.kind === "solution" && ws.solution) {
			const count = ws.solution.projects.length;
			return [{ id: "sln", kind: "solution", name: "Solution '" + ws.solution.name + "' (" + count + (count === 1 ? " project)" : " projects)"), expandable: true }];
		}
		return this.list("", "root");
	}

	async children(node) {
		if (node.kind === "solution") return this.solutionChildren();
		if (node.kind === "group") return this.groupChildren(node.group, node.id);
		if (node.kind === "items") return this.ied.workspace.solution.items.map((path) => ({ id: node.id + "/" + path, kind: "file", name: baseName(path), path }));
		if (node.kind === "project") return node.path === null ? [] : this.list(node.path, node.id);
		return this.list(node.path, node.id);
	}

	solutionChildren() {
		const solution = this.ied.workspace.solution;
		const grouped = new Set();
		const collect = (group) => {
			for (const name of group.projects || []) grouped.add(name);
			for (const inner of group.groups || []) collect(inner);
		};
		for (const group of solution.groups) collect(group);
		const nodes = solution.groups.map((group) => this.groupNode(group, "sln"));
		for (const project of solution.projects) if (!grouped.has(project.name)) nodes.push(this.projectNode(project, "sln"));
		if (solution.items.length) nodes.push({ id: "sln/items", kind: "items", name: "Solution Items", expandable: true });
		return nodes;
	}

	groupNode(group, parentId) {
		return { id: parentId + "/group:" + group.name, kind: "group", name: group.name, group, expandable: true };
	}

	groupChildren(group, id) {
		const projects = this.ied.workspace.solution.projects;
		const nodes = (group.groups || []).map((inner) => this.groupNode(inner, id));
		for (const name of group.projects || []) {
			const project = projects.find((p) => p.name === name);
			if (project) nodes.push(this.projectNode(project, id));
			else nodes.push({ id: id + "/missing:" + name, kind: "missing", name, problem: "no project named " + name });
		}
		return nodes;
	}

	projectNode(project, parentId) {
		const path = relativeTo(this.ied.workspace.root, project.root);
		const node = { id: parentId + "/project:" + project.name, kind: "project", name: project.name, path, expandable: path !== null, project, problem: project.problems.length ? project.problems.join("; ") : path === null ? "its root is outside the solution's folder" : null };
		if (path !== null) this.remember(node);
		return node;
	}

	async list(path, parentId) {
		const answer = await this.ied.client.request("fs.list", { wsId: this.ctx.wsId, path });
		return answer.entries.map((entry) => {
			const node = { id: parentId + "/" + entry.name, kind: entry.kind === "dir" ? "folder" : "file", name: entry.name, path: entry.path, expandable: entry.kind === "dir" };
			if (node.expandable) this.remember(node);
			return node;
		});
	}

	// Folder-like nodes by path, so a change on disk reaches every place that shows the folder.
	remember(node) {
		if (!this.folders.has(node.path)) this.folders.set(node.path, new Set());
		this.folders.get(node.path).add(node);
	}

	renderRow(node, row) {
		const open = this.tree.expanded.has(node.id);
		const glyph = {
			solution: () => icon("project", "ft-project"), group: () => folderIcon(open), items: () => folderIcon(open),
			project: () => icon("project", "ft-project"), folder: () => folderIcon(open), missing: () => icon("warning", "c-warn"),
		}[node.kind];
		row.append(glyph ? glyph() : fileIcon(node.name), h("span.name", null, node.name));
		if (node.problem) {
			row.classList.add("problem");
			row.title = node.problem;
		}
		else if (node.path !== undefined && node.path !== null) row.title = node.path || "/";
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Actions

	open(node, options) {
		if (node.kind === "file") this.ied.tabs.open("doc:" + node.path, { preview: options.preview });
	}

	menu(node, at) {
		this.ied.context.set("explorerItemKind", node.kind);
		this.ied.menus.show("explorer/context", at, { path: node.path, kind: node.kind });
	}

	selected() {
		return this.tree.selectedNode();
	}

	workspaceEvent(kind, data) {
		if (kind === "solution.changed") return this.refresh();
		if (kind !== "tree.changed") return undefined;
		if (data.overflow) return this.refresh();
		const parents = new Set(data.changes.map((change) => parentOf(change.path)));
		if (parents.has("") && this.ied.workspace.kind !== "solution") return this.refresh();
		for (const parent of parents) for (const node of this.folders.get(parent) || []) this.tree.invalidate(node);
		return undefined;
	}
}
