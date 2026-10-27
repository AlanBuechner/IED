// explorer.js - Solution Explorer (F-WSP, docs/15): a solution shows its groups, projects (each its root folder) and
// Solution Items, or its folder (Switch Views, remembered per workspace on this device); a folder or file workspace
// shows its folder. Folders load when opened; tree.changed keeps them
// current. A click opens a file in a preview tab, a double click or Enter keeps it open (VS). New files and folders
// and renames are named in place; a delete asks first. A folder can be added as a project, and a project moved or
// removed (its files kept, or its folder deleted), through the server's project.* methods. The search box above the
// tree (Ctrl+;) keeps the rows that match and the folders that lead to them (explorersearch.js); emptying it gives the
// tree back as it was.

import { h, icon } from "/ui-kit/dom.js";
import { TreeView } from "/ui-kit/tree.js";
import { choiceDialog, confirmDialog, promptDialog } from "/ui-kit/dialog.js";
import { SEARCH_LIMIT, SearchState, holds, markedParts } from "./explorersearch.js";

const SEARCH_DELAY_MS = 200;

function parentOf(path) {
	const slash = path.lastIndexOf("/");
	return slash < 0 ? "" : path.slice(0, slash);
}

function baseName(path) {
	return path.slice(path.lastIndexOf("/") + 1);
}

const VIEW_KEY = "ied.explorer.folderView:";

// Whether a solution workspace shows its folder here: a choice of how to look, like a scroll position, so this device
// keeps it.
function readFolderView(root) {
	try {
		return localStorage.getItem(VIEW_KEY + root) === "1";
	}
	catch {
		return false;
	}
}

function writeFolderView(root, folderView) {
	try {
		if (folderView) localStorage.setItem(VIEW_KEY + root, "1");
		else localStorage.removeItem(VIEW_KEY + root);
	}
	catch {
		// a page without storage forgets the choice
	}
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
	constructor(ctx, mounted, tasks) {
		this.ctx = ctx;
		this.ied = ctx.ied;
		this.mounted = mounted;
		this.startup = null;          // the startup project's name, drawn bold as VS does
		this.folders = new Map();
		this.rootNames = new Map();   // a project's root folder -> its names, for icons that go by marker files
		this.viewButton = h("button.ib", { on: { click: () => this.switchView() } });
		const tools = h("div.ed-tools", null,
			this.viewButton,
			h("span.grow"),
			h("button.ib", { title: "Collapse All", "aria-label": "Collapse All", on: { click: () => this.tree.collapseAll() } }, icon("collapse-all")),
			h("button.ib", { title: "Refresh", "aria-label": "Refresh", on: { click: () => this.reload() } }, icon("refresh")));
		this.searchField = h("input.input-field.ex-search-field", {
			type: "search", placeholder: "Search Solution Explorer (Ctrl+;)", "aria-label": "Search Solution Explorer", spellcheck: "false",
			on: { input: () => this.searchSoon(), keydown: (event) => this.searchKey(event) },
		});
		this.searchNote = h("span.ex-search-note.muted", { "aria-live": "polite" });
		this.search = null;           // the SearchState while the box holds a query
		this.searchTimer = null;
		this.searchSeq = 0;           // answers to an older query are dropped
		this.expandedBefore = null;   // the rows open before the search, given back when it ends
		const fill = h("div.ed-fill.explorer");
		ctx.el.append(tools, h("div.ed-tools.ex-search", null, this.searchField, this.searchNote), fill);
		this.tree = new TreeView(fill, {
			roots: () => this.roots(),
			children: (node) => this.children(node),
			render: (node, row) => this.renderRow(node, row),
			label: (node) => node.name,
			open: (node, options) => this.open(node, options),
			menu: (node, at) => this.menu(node, at),
			select: (node) => this.selectedNode(node),
		});
		this.stopWorkspace = this.ied.workspace.on(() => this.reload());
		this.stopTopic = this.ied.client.subscribe("ws:" + ctx.wsId, { evt: (kind, data) => this.workspaceEvent(kind, data) });
		tasks.follow(ctx.wsId);
		this.stopTasks = tasks.on(() => this.startupIs(tasks.offer ? tasks.offer.startupProject : null));
		mounted.add(this);
		this.refresh();
	}

	dispose() {
		clearTimeout(this.searchTimer);
		this.mounted.delete(this);
		this.stopWorkspace();
		this.stopTopic();
		this.stopTasks();
		this.tree.dispose();
	}

	startupIs(name) {
		if (name === this.startup) return;
		this.startup = name;
		this.tree.draw();
	}

	focus() {
		this.tree.focus();
	}

	// A solution's folder view: the workspace's folder as a folder workspace shows it.
	showsFolder() {
		const ws = this.ied.workspace;
		return ws.kind !== "solution" || !ws.solution || readFolderView(ws.root);
	}

	switchView() {
		const ws = this.ied.workspace;
		writeFolderView(ws.root, !readFolderView(ws.root));
		this.reload();
	}

	// The tree read again: searched again while the box holds a query.
	reload() {
		return this.search ? this.runSearch() : this.refresh();
	}

	renderViewButton() {
		const ws = this.ied.workspace;
		const folder = this.showsFolder();
		this.viewButton.hidden = ws.kind !== "solution" || !ws.solution;
		this.viewButton.title = folder ? "Switch to Solution View" : "Switch to Folder View";
		this.viewButton.setAttribute("aria-label", this.viewButton.title);
		this.viewButton.replaceChildren(icon(folder ? "list-tree" : "folder"));
	}

	refresh() {
		this.renderViewButton();
		this.folders.clear();
		this.rootNames.clear();
		return this.tree.refresh().catch((e) => this.ied.report("Solution Explorer: " + e.message, e));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Nodes

	async roots() {
		const ws = this.ied.workspace;
		if (!this.showsFolder()) {
			const count = ws.solution.projects.length;
			if (this.search) this.tree.expanded.add("sln");
			return [{ id: "sln", kind: "solution", name: "Solution '" + ws.solution.name + "' (" + count + (count === 1 ? " project)" : " projects)"), expandable: true }];
		}
		const nodes = await this.list("", "root");
		return this.search ? this.searched(nodes) : nodes;
	}

	// While searching, a node's children are those the search keeps, except under a row that matched by its own name
	// and leads to nothing else: opening it shows all it holds.
	async children(node) {
		const nodes = await this.childrenOf(node);
		if (!this.search) return nodes;
		if (node.unfiltered) return nodes.map((child) => Object.assign(child, { unfiltered: true }));
		return this.searched(nodes);
	}

	async childrenOf(node) {
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
		row.append(this.iconOf(node), h("span.name", null, ...this.nameParts(node)));
		if (node.kind === "project" && node.name === this.startup) row.classList.add("startup");
		if (node.problem) {
			row.classList.add("problem");
			row.title = node.problem;
		}
		else if (node.path !== undefined && node.path !== null) row.title = node.path || "/";
	}

	// A node's icon (docs/15 "Icons"): the solution; folders, a solution's groups and its Solution Items as folders; a
	// project as the plugins draw its language; a file by its name.
	iconOf(node) {
		const icons = this.ied.icons;
		const open = this.tree.expanded.has(node.id);
		if (node.kind === "solution") return icons.solution();
		if (node.kind === "group" || node.kind === "items" || node.kind === "folder") return icons.folder(open);
		if (node.kind === "project") return icons.project(node.project, this.markerNames(node));
		if (node.kind === "missing") return icon("warning", "c-warn");
		return icons.file(node.name);
	}

	// The names in a project's root folder when its icon goes by marker files there, listed once; the row is drawn
	// again when they arrive. A root that cannot be listed already says why on its row (its problem).
	markerNames(node) {
		if (node.path === null || !this.ied.icons.needsMarkers(node.project)) return [];
		if (!this.rootNames.has(node.path)) {
			this.rootNames.set(node.path, []);
			this.ied.client.request("fs.list", { wsId: this.ctx.wsId, path: node.path }).then((answer) => {
				this.rootNames.set(node.path, answer.entries.map((entry) => entry.name));
				this.tree.draw();
			}, () => {});
		}
		return this.rootNames.get(node.path);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Search (VS "Search Solution Explorer"): the box's text, looked for in the names of the workspace's files and
	// folders, its projects and its groups, a moment after the typing stops.

	focusSearch() {
		this.searchField.focus({ preventScroll: true });
		this.searchField.select();
	}

	searchSoon() {
		clearTimeout(this.searchTimer);
		this.searchTimer = setTimeout(() => this.runSearch(), SEARCH_DELAY_MS);
	}

	async runSearch() {
		clearTimeout(this.searchTimer);
		const text = this.searchField.value.trim();
		const seq = ++this.searchSeq;
		if (!text) return this.endSearch();
		this.searchNote.textContent = "Searching...";
		let answer;
		try {
			answer = await this.ied.client.request("fs.files", { wsId: this.ctx.wsId, query: text, maxResults: SEARCH_LIMIT });
		}
		catch (e) {
			if (seq === this.searchSeq) this.searchNote.textContent = "The search failed: " + e.message;
			return undefined;
		}
		if (seq !== this.searchSeq) return undefined;
		const paths = answer.files.map((file) => file.path);
		if (!this.expandedBefore) this.expandedBefore = new Set(this.tree.expanded);
		this.search = new SearchState(text, paths, this.showsFolder() ? null : this.ied.workspace.solution, paths.length >= SEARCH_LIMIT);
		this.tree.expanded.clear();
		await this.refresh();
		if (seq === this.searchSeq) this.noteSearch();
		return undefined;
	}

	// The box emptied: the tree as it was before the search, its rows opened as they were.
	endSearch() {
		this.searchSeq++;
		clearTimeout(this.searchTimer);
		this.searchNote.textContent = "";
		if (!this.search) return undefined;
		this.search = null;
		this.tree.expanded = this.expandedBefore || new Set();
		this.expandedBefore = null;
		return this.refresh();
	}

	noteSearch() {
		const count = this.tree.rows.filter((row) => this.matchesSearch(row.node)).length;
		const more = this.search.capped ? "; more files hold it: type more to narrow" : "";
		this.searchNote.textContent = (count ? count + (count === 1 ? " result" : " results") : "No results") + more;
	}

	// Escape empties the box (and, empty, goes back to the tree); Down or Enter goes to the first result.
	searchKey(event) {
		if (event.key === "Escape") {
			if (this.searchField.value) {
				this.searchField.value = "";
				this.endSearch();
			}
			else this.tree.focus();
		}
		else if (event.key === "ArrowDown" || event.key === "Enter") {
			const first = this.tree.rows.findIndex((row) => this.matchesSearch(row.node));
			this.tree.focus();
			if (first >= 0) this.tree.moveTo(first);
		}
		else return;
		event.preventDefault();
		event.stopPropagation();
	}

	// Whether a row matches the search by its own name.
	matchesSearch(node) {
		const search = this.search;
		if (!search || !node || !node.kind) return false;
		if (node.kind === "project") return search.projects.has(node.name);
		if (node.kind === "group") return search.groups.has(node.name);
		if (node.kind === "file" || node.kind === "folder") return search.matched.has(node.path);
		return node.kind === "missing" && holds(node.name, node.name, search.query);
	}

	// The rows of `nodes` the search keeps, those that lead to a match opened.
	searched(nodes) {
		const search = this.search;
		const solution = this.ied.workspace.solution;
		const pathOf = (name) => {
			const project = solution ? solution.projects.find((p) => p.name === name) : null;
			return project ? relativeTo(this.ied.workspace.root, project.root) : null;
		};
		const kept = [];
		for (const node of nodes) {
			let keep = false;
			let open = false;
			if (node.kind === "file" || node.kind === "folder") {
				keep = search.keepsPath(node.path);
				open = node.kind === "folder" && search.above.has(node.path);
			}
			else if (node.kind === "project") {
				keep = search.keepsProject(node.name, node.path);
				open = search.opensProject(node.path);
			}
			else if (node.kind === "group") {
				keep = search.keepsGroup(node.group, pathOf);
				open = search.opensGroup(node.group, pathOf);
			}
			else if (node.kind === "items") keep = open = solution.items.some((path) => search.keepsPath(path));
			else keep = this.matchesSearch(node);
			if (!keep) continue;
			node.unfiltered = this.matchesSearch(node) && !open;
			if (open) this.tree.expanded.add(node.id);
			kept.push(node);
		}
		return kept;
	}

	// A row's name, the parts that hold the query marked.
	nameParts(node) {
		if (!this.matchesSearch(node)) return [node.name];
		return markedParts(node.name, this.search.query).map((part) => (typeof part === "string" ? part : h("mark.ex-hit", null, part.mark)));
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Actions

	open(node, options) {
		if (node.kind === "file") this.ied.tabs.open("doc:" + node.path, { preview: options.preview });
	}

	menu(node, at) {
		this.selectedNode(node);
		this.ied.menus.show("explorer/context", at, { path: node.path, kind: node.kind, name: node.name, project: node.kind === "project" ? node.project.name : undefined });
	}

	// The context keys of the selected item: its kind, a project's kind (a library cannot be the startup project), and a
	// file's extension (".md") for menus that fit its type.
	selectedNode(node) {
		this.ied.context.set("explorerItemKind", node.kind);
		this.ied.context.set("explorerProjectKind", node.kind === "project" ? node.project.kind || "" : undefined);
		const name = node.kind === "file" && node.path ? baseName(node.path) : "";
		this.ied.context.set("explorerItemExtension", name.lastIndexOf(".") > 0 ? name.slice(name.lastIndexOf(".")).toLowerCase() : "");
	}

	selected() {
		return this.tree.selectedNode();
	}

	// Where a new item goes: into the selected folder or project, beside the selected file, or at the top of a folder
	// workspace. {parent: the tree node to add the row under (null: the top), path}; null in a solution's own nodes.
	targetOf(node) {
		if (!node) return this.showsFolder() ? { parent: null, path: "" } : null;
		if (node.kind === "folder" || (node.kind === "project" && node.path !== null)) return { parent: node, path: node.path };
		if (node.kind !== "file") return null;
		const parentId = node.id.slice(0, node.id.lastIndexOf("/"));
		const parent = this.tree.findNode(parentId);
		if (parent && parent.kind === "items") return null;
		return { parent, path: parentOf(node.path) };
	}

	async create(kind) {
		const target = this.targetOf(this.selected());
		if (!target) {
			this.ied.report("Choose a folder or a project to add a " + kind + " to.");
			return;
		}
		const name = await this.tree.editLabel({ parent: target.parent, template: { id: "\u0000new", kind, name: "", expandable: false } });
		if (!name) return;
		const path = target.path ? target.path + "/" + name : name;
		const request = kind === "folder" ? ["fs.mkdir", { wsId: this.ctx.wsId, path }] : ["fs.write", { wsId: this.ctx.wsId, path, text: "", expectedRevision: 0 }];
		try {
			await this.ied.client.request(request[0], request[1]);
			if (kind === "file") this.ied.tabs.open("doc:" + path, { preview: false });
		}
		catch (e) {
			this.ied.report(e.code === "conflict" ? "'" + path + "' already exists." : "Could not create '" + path + "': " + e.message, e);
		}
	}

	async rename(node) {
		if (!node || (node.kind !== "file" && node.kind !== "folder")) return;
		const dot = node.kind === "file" ? node.name.lastIndexOf(".") : -1;
		const name = await this.tree.editLabel({ node, value: node.name, select: [0, dot > 0 ? dot : node.name.length] });
		if (!name) return;
		const folder = parentOf(node.path);
		const to = folder ? folder + "/" + name : name;
		try {
			await this.ied.client.request("fs.rename", { wsId: this.ctx.wsId, from: node.path, to });
		}
		catch (e) {
			this.ied.report(e.code === "conflict" ? "'" + to + "' already exists." : "Could not rename '" + node.path + "': " + e.message, e);
		}
	}

	// To the trash (docs/10 "Files"), with an Undo that puts it back.
	async remove(node) {
		if (!node || (node.kind !== "file" && node.kind !== "folder")) return;
		const what = node.kind === "folder" ? "'" + node.name + "' and everything in it" : "'" + node.name + "'";
		const sure = await confirmDialog({ title: "Delete", message: what + " will be moved to the trash.", confirm: "Delete", danger: true });
		if (!sure) return;
		try {
			const result = await this.ied.client.request("fs.delete", { wsId: this.ctx.wsId, path: node.path, recursive: node.kind === "folder" });
			this.ied.toast("Deleted '" + node.name + "'.", { action: { label: "Undo", run: () => this.restore(result.trashId, node.path) } });
		}
		catch (e) {
			this.ied.report("Could not delete '" + node.path + "': " + e.message, e);
		}
	}

	async restore(trashId, path) {
		try {
			await this.ied.client.request("trash.restore", { id: trashId });
		}
		catch (e) {
			this.ied.report(e.code === "conflict" ? "'" + path + "' was not restored: something is there now." : "Could not restore '" + path + "': " + e.message, e);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Projects (docs/03 "Managing projects"): the server changes the solution (through its build system when one owns
	// it), and every Solution Explorer shows it from solution.changed.

	async project(method, params, done) {
		try {
			const result = await this.ied.client.request(method, Object.assign({ wsId: this.ctx.wsId }, params));
			this.ied.toast(done(result));
			return result;
		}
		catch (e) {
			this.ied.report(e.message, e);
			return null;
		}
	}

	addFolderAsProject(node) {
		if (!node || node.kind !== "folder") return undefined;
		return this.project("project.add", { folder: node.path }, (made) => "Added the project " + made.project + ".");
	}

	async addExistingFolder() {
		const folder = await promptDialog({ title: "Add Existing Folder as Project", message: "The folder, relative to the solution's folder.", placeholder: "tools/packer", confirm: "Add" });
		if (folder) await this.project("project.add", { folder }, (made) => "Added the project " + made.project + ".");
	}

	async moveProject(node) {
		if (!node || node.kind !== "project") return;
		if (!node.path) {
			this.ied.report("'" + node.name + "' " + (node.path === null ? "lies outside the solution's folder" : "is the solution's own folder") + ": it cannot move.");
			return;
		}
		const to = await promptDialog({ title: "Move Project", message: "Move '" + node.name + "' to the folder (relative to the solution's folder):", value: node.path, confirm: "Move",
			check: (text) => (text.replace(/\\/g, "/").replace(/\/+$/, "") === node.path ? "Give another folder" : "") });
		if (to) await this.project("project.move", { project: node.name, to }, () => "Moved " + node.name + " to " + to + ".");
	}

	async removeProject(node) {
		if (!node || node.kind !== "project") return;
		const choices = [{ label: "Remove", value: "remove", primary: true }];
		if (node.path) choices.push({ label: "Delete", value: "delete", danger: true });
		const message = "'" + node.name + "' will be removed from the solution." + (node.path ? " Remove keeps its files; Delete moves its folder to the trash." : "");
		const choice = await choiceDialog({ title: "Remove Project", message, choices });
		if (!choice) return;
		await this.project("project.remove", { project: node.name, deleteFiles: choice === "delete" },
			() => (choice === "delete" ? "Deleted " + node.name + "; its folder is in the trash." : "Removed " + node.name + " from the solution; its files are kept."));
	}

	workspaceEvent(kind, data) {
		if (kind === "solution.changed") return this.reload();
		if (kind !== "tree.changed") return undefined;
		if (this.search) return this.searchSoon();
		if (data.overflow) return this.refresh();
		const parents = new Set(data.changes.map((change) => parentOf(change.path)));
		if (parents.has("") && this.showsFolder()) return this.refresh();
		for (const parent of parents) for (const node of this.folders.get(parent) || []) this.tree.invalidate(node);
		return undefined;
	}
}
