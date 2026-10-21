"""The shell's interactions, end to end (docs/15, docs/19): each scenario drives the page the way a person does and
screenshots the states that matter. Both servers run every scenario unless it names one."""

import json
import time

from harness import check

SCENARIOS = []
DESKTOPS = ("desktop", "laptop")


def scenario(viewports=DESKTOPS, targets=("mock", "real")):
	def register(fn):
		SCENARIOS.append((fn.__name__, fn, viewports, targets))
		return fn
	return register


def tab_names(run, cls=".tab"):
	return run.texts(".dock-group.well " + cls + " .name")


def drag(page, start, end, steps=12):
	page.mouse.move(*start)
	page.mouse.down()
	page.mouse.move(start[0] + 15, start[1] + 15, steps=3)
	page.mouse.move(*end, steps=steps)
	time.sleep(0.2)
	page.mouse.up()
	time.sleep(0.6)


# ---------------------------------------------------------------------------------------------------------------
# Desktop and tablet

@scenario(viewports=("desktop", "laptop", "tablet"))
def shell_chrome(run):
	page = run.open()
	check(page.is_visible(".sidebar .sb-ws"), "the workspace switcher")
	check(page.is_visible('.sb-act[data-activity="ied.editor"].on'), "the editor activity, shown")
	check(run.texts(".menubar .mi")[:2] == ["File", "View"], "the menu bar in VS order: %s" % run.texts(".menubar .mi"))
	run.page.wait_for_selector(".tree-row:has-text('README.md')")
	check("Solution Explorer" in run.texts(".panel-title"), "Solution Explorer docked")
	check("Output" in run.texts(".panel-title"), "Output docked")
	check(run.texts(".statusbar .conn-state") == ["Ready"], "the status bar says Ready")
	check(page.is_visible(".menubar .conn.live"), "the title says live")
	run.shot("loaded")


@scenario()
def preview_and_pinned_tabs(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab.preview:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('main.c')")
	page.click(".tree-row:has-text('main.c')")
	page.wait_for_selector(".tab.preview:has-text('main.c')")
	check(tab_names(run) == ["main.c"], "the preview tab was replaced: %s" % tab_names(run))
	page.dblclick(".tab:has-text('main.c')")
	page.wait_for_selector(".tab:not(.preview):has-text('main.c')")
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab.preview:has-text('README.md')")
	check(tab_names(run) == ["main.c", "README.md"], "a pinned tab stays: %s" % tab_names(run))
	run.shot("two-tabs")
	page.hover(".tab:has-text('main.c')")
	page.click(".tab:has-text('main.c') .x")
	page.wait_for_selector(".tab:has-text('main.c')", state="detached")
	check(tab_names(run) == ["README.md"], "closed")


@scenario()
def menus_and_palette(run):
	page = run.open()
	page.click("button.mi:has-text('View')")
	page.wait_for_selector(".menu")
	labels = run.texts(".menu .menu-item .label")
	for wanted in ("Command Palette...", "Solution Explorer", "Output", "Editor", "Notifications"):
		check(wanted in labels, "View has %s: %s" % (wanted, labels))
	check("Ctrl+Alt+O" in run.texts(".menu .menu-item .kbd"), "menu items show their keys")
	run.shot("view-menu")
	page.keyboard.press("ArrowDown")
	page.keyboard.press("Escape")
	page.wait_for_selector(".menu", state="detached")
	page.keyboard.press("Control+Shift+P")
	page.wait_for_selector(".palette-input")
	page.keyboard.type("keyboard")
	time.sleep(0.2)
	run.shot("palette")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab:has-text('Keyboard Shortcuts')")
	rows = run.texts("table.grid tbody tr td:first-child")
	check("View: Command Palette..." in rows and "View: Output" in rows, "the shortcuts table: %s" % rows)
	run.shot("shortcuts")


@scenario()
def docking_persists(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab:has-text('README.md')")
	page.keyboard.press("Control+Shift+P")
	page.keyboard.type("about")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab:has-text('About IED')")
	tab = page.locator(".tab:has-text('About IED')").bounding_box()
	well = page.locator(".dock-group.well").first.bounding_box()
	drag(page, (tab["x"] + 30, tab["y"] + 12), (well["x"] + well["width"] - 20, well["y"] + well["height"] / 2))
	check(len(page.query_selector_all(".dock-group.well")) == 2, "the tab made a second document group")
	run.shot("split")
	time.sleep(0.8)
	run.reload()
	check(len(page.query_selector_all(".dock-group.well")) == 2, "the split is kept by the server")
	check(page.is_visible(".tab:has-text('About IED')"), "with its tab")
	run.shot("reloaded")


@scenario()
def tab_context_menu(run):
	page = run.open()
	for name in ("README.md", ".gitignore" if run.target.kind == "mock" else "docs"):
		page.click(".tree-row:has-text('%s')" % name)
	if run.target.kind == "real":
		page.wait_for_selector(".tree-row:has-text('notes.md')")
		page.dblclick(".tree-row:has-text('notes.md')")
	else:
		page.dblclick(".tree-row:has-text('.gitignore')")
	page.dblclick(".tree-row:has-text('README.md')")
	time.sleep(0.4)
	before = tab_names(run)
	check(len(before) == 2, "two tabs open: %s" % before)
	page.click(".tab:has-text('README.md')", button="right")
	page.wait_for_selector(".menu")
	labels = run.texts(".menu .menu-item .label")
	check(labels[:3] == ["Close", "Close All But This", "Close All"], "the tab menu: %s" % labels)
	run.shot("menu")
	page.click(".menu .menu-item:has-text('Close All But This')")
	page.wait_for_function("document.querySelectorAll('.dock-group.well .tab').length === 1")
	check(tab_names(run) == ["README.md"], "only README.md is left: %s" % tab_names(run))


@scenario()
def tool_windows_close_and_come_back(run):
	page = run.open()
	page.click(".dock-group.tool:has(.panel-title:has-text('Output')) .panel-title button")
	page.wait_for_selector(".panel-title:has-text('Output')", state="detached")
	run.shot("closed")
	page.click(".dock-group.well")
	page.keyboard.press("Control+Alt+O")
	page.wait_for_selector(".panel-title:has-text('Output')")
	page.keyboard.press("Control+Alt+L")
	page.wait_for_function("document.activeElement.closest('.view') && document.activeElement.closest('.view').dataset.ref === 'ied.explorer'", timeout=5000)
	run.shot("back")


@scenario()
def splitter_resizes(run):
	page = run.open()
	sash = page.locator(".dock > .dock-split > .dock-sash").first.bounding_box()
	before = page.locator(".dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	drag(page, (sash["x"] + sash["width"] / 2, sash["y"] + 200), (sash["x"] + 120, sash["y"] + 200), steps=6)
	after = page.locator(".dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	check(abs(after - before - 120) < 6, "Solution Explorer grew by the drag: %d -> %d" % (before, after))
	time.sleep(0.8)
	run.reload()
	kept = page.locator(".dock-group.tool:has(.panel-title:has-text('Solution Explorer'))").bounding_box()["width"]
	check(abs(kept - after) < 3, "the size is kept: %d vs %d" % (kept, after))


@scenario(viewports=("desktop", "laptop", "phone"))
def notifications(run):
	page = run.open()
	run.request("server.notify", {"text": "Build finished: 0 errors", "level": "info"})
	page.wait_for_selector(".toast:has-text('Build finished')")
	selector = ".phone-head .badge" if run.phone else ".sidebar .badge"
	page.wait_for_selector(selector)
	check(run.texts(selector) == ["1"], "the bell counts one: %s" % run.texts(selector))
	run.shot("toast")
	page.click(".phone-head button[aria-label^='Notifications']" if run.phone else ".sidebar .sb-bell")
	page.wait_for_selector(".notification-center .nc-item:has-text('Build finished')")
	run.shot("center")
	page.click(".notification-center .nc-item button")
	page.wait_for_selector(".notification-center .nc-empty")
	page.wait_for_selector(selector, state="detached")


@scenario()
def output_streams(run):
	page = run.open()
	if run.target.kind == "mock":
		page.select_option("select[aria-label='Show output from']", "build")
		run.request("task.run", {"wsId": "w1", "task": "build"})
		run.wait_text(".output-text .line", "Build succeeded", timeout=20000)
		check(page.query_selector(".output-text .a-yellow, .output-text .a-byellow, .output-text .a-green, .output-text .a-bgreen") is not None, "ANSI colors became runs")
	else:
		run.wait_text(".output-text .line", "opened w1")
	run.shot("output")


@scenario()
def resume_after_server_restart(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	page.wait_for_selector(".tab:has-text('README.md')")
	time.sleep(0.6)
	epoch = run.request("server.info")["sessionEpoch"]
	page.evaluate("ied_shell.client.request('testhooks.restart', {}).catch(() => {})")
	page.wait_for_function("ied_shell.client.state !== 'live'", timeout=10000)
	run.shot("reconnecting")
	page.wait_for_function("ied_shell.client.state === 'live'", timeout=20000)
	page.wait_for_selector(".banner[data-banner='connection']", state="detached")
	check(run.request("server.info")["sessionEpoch"] != epoch, "a new epoch")
	check(page.is_visible(".tab:has-text('README.md')"), "the tab is still there")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	run.shot("resumed")


@scenario(targets=("mock",))
def reopen_mid_stream(run):
	page = run.open()
	page.select_option("select[aria-label='Show output from']", "build")
	run.request("task.run", {"wsId": "w1", "task": "flood", "params": {"lines": 3000}})
	run.wait_text(".output-text .line", "line")
	page.context.close()
	time.sleep(1.0)
	page = run.open(fresh=False)
	page.select_option("select[aria-label='Show output from']", "build")
	page.wait_for_function("[...document.querySelectorAll('.output-text .line')].some(l => /3000/.test(l.textContent))", timeout=30000)
	run.shot("reopened")


@scenario()
def trust_banner(run):
	page = run.open()
	run.request("ws.setTrust", {"wsId": page.evaluate("ied_shell.wsId"), "trusted": False})
	page.wait_for_selector(".banner[data-banner='trust']")
	run.shot("untrusted")
	page.click(".banner[data-banner='trust'] button:has-text('Trust Workspace')")
	page.wait_for_selector(".banner[data-banner='trust']", state="detached")


@scenario()
def plugins_page(run):
	page = run.open()
	page.click("button.mi:has-text('Extensions')")
	page.click(".menu .menu-item:has-text('Manage Plugins')")
	page.wait_for_selector(".plugin-card[data-plugin='ied.editor']")
	check("loaded" in run.texts(".plugin-card[data-plugin='ied.editor'] .chip"), "the editor plugin is loaded")
	run.shot("plugins")


@scenario(viewports=("desktop",))
def disable_and_enable_a_plugin(run):
	page = run.open()
	page.click("button.mi:has-text('Extensions')")
	page.click(".menu .menu-item:has-text('Manage Plugins')")
	page.wait_for_selector(".plugin-card[data-plugin='ied.editor'] button:has-text('Disable')")
	page.click(".plugin-card[data-plugin='ied.editor'] button:has-text('Disable')")
	page.wait_for_selector('.sb-act[data-activity="ied.editor"]', state="detached")
	page.wait_for_selector('.sb-act[data-activity="shell.home"].on')
	page.wait_for_selector(".panel-title:has-text('Solution Explorer')", state="hidden")
	run.shot("disabled")
	page.click(".hint:has-text('Manage Plugins')")
	page.wait_for_selector(".plugin-card[data-plugin='ied.editor'] button:has-text('Enable')")
	page.click(".plugin-card[data-plugin='ied.editor'] button:has-text('Enable')")
	page.wait_for_selector('.sb-act[data-activity="ied.editor"]')
	page.wait_for_selector('.sb-act[data-activity="shell.home"]', state="detached")
	page.click('.sb-act[data-activity="ied.editor"]')
	page.wait_for_selector(".panel-title:has-text('Solution Explorer')")
	page.wait_for_selector(".tree-row:has-text('README.md')")
	run.shot("enabled")


# ---------------------------------------------------------------------------------------------------------------
# Solution Explorer file operations

def exists(run, path):
	try:
		run.request("fs.read", {"wsId": run.page.evaluate("ied_shell.wsId"), "path": path})
		return True
	except Exception:
		return False


@scenario()
def explorer_file_operations(run):
	page = run.open()
	name = "made-" + run.viewport
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New File...')")
	page.wait_for_selector(".tree-edit")
	run.shot("naming")
	page.keyboard.type(name + ".c")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s.c')" % name)
	page.wait_for_selector(".tab:has-text('%s.c')" % name)
	check(exists(run, "src/%s.c" % name), "the new file is on disk")
	page.click(".tree-row:has-text('%s.c')" % name)
	page.keyboard.press("F2")
	page.wait_for_selector(".tree-edit")
	check(page.evaluate("(() => { const i = document.querySelector('.tree-edit'); return [i.selectionStart, i.selectionEnd]; })()") == [0, len(name)], "the name is selected, not its extension")
	page.keyboard.type(name + "-renamed")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name)
	check(exists(run, "src/%s-renamed.c" % name) and not exists(run, "src/%s.c" % name), "renamed on disk")
	page.click(".tree-row:has-text('%s-renamed.c')" % name)
	page.keyboard.press("Delete")
	page.wait_for_selector(".popup.dialog")
	run.shot("confirm-delete")
	page.keyboard.press("Escape")
	page.wait_for_selector(".popup.dialog", state="detached")
	check(exists(run, "src/%s-renamed.c" % name), "Escape keeps the file")
	page.keyboard.press("Delete")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name, state="detached")
	check(not exists(run, "src/%s-renamed.c" % name), "deleted on disk")
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New Folder')")
	page.keyboard.type(name + "-dir")
	page.keyboard.press("Escape")
	page.wait_for_selector(".tree-edit", state="detached")
	check(not page.is_visible(".tree-row:has-text('%s-dir')" % name), "Escape makes nothing")
	page.click(".tree-row:has-text('src')", button="right")
	page.click(".menu-item:has-text('New Folder')")
	page.keyboard.type(name + "-dir")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-dir')" % name)
	run.shot("folder")
	page.click(".tree-row:has-text('%s-dir')" % name)
	page.keyboard.press("F2")
	page.keyboard.type(name + "-moved")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tree-row:has-text('%s-moved')" % name)
	page.click(".tree-row:has-text('%s-moved')" % name)
	page.keyboard.press("Delete")
	check("everything in it" in page.text_content(".popup.dialog"), "a folder's delete says it takes its contents")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-moved')" % name, state="detached")


@scenario(viewports=("desktop",))
def renamed_document_follows(run):
	"""A file renamed while it is open: its tab moves to the new name with the unsaved text (docs/10)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("fs.write", {"wsId": ws, "path": "src/follow.c", "text": "int x;\n", "expectedRevision": 0})
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('follow.c')")
	page.dblclick(".tree-row:has-text('follow.c')")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/follow.c') && ied_shell.views.entries.get('doc:src/follow.c').instance && ied_shell.views.entries.get('doc:src/follow.c').instance.model", timeout=30000)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+End")
	page.keyboard.type("int y;")
	page.wait_for_selector(".tab:has-text('follow.c') .dirty")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/follow.c').instance.sync.pending === 0", timeout=10000)
	run.request("fs.rename", {"wsId": ws, "from": "src/follow.c", "to": "src/followed.c"})
	page.wait_for_selector(".tab:has-text('followed.c')")
	page.wait_for_selector(".tab:has-text('follow.c')", state="detached")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/followed.c') && ied_shell.views.entries.get('doc:src/followed.c').instance && ied_shell.views.entries.get('doc:src/followed.c').instance.model", timeout=30000)
	text = page.evaluate("ied_shell.views.entries.get('doc:src/followed.c').instance.model.getValue()")
	check(text == "int x;\nint y;", "the unsaved text came along: %r" % text)
	page.wait_for_selector(".tab:has-text('followed.c') .dirty")
	run.shot("followed")
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('followed.c') .dirty", state="detached")
	check(run.request("fs.read", {"wsId": ws, "path": "src/followed.c"})["text"] == "int x;\nint y;", "saved under the new name")
	check(not exists(run, "src/follow.c"), "nothing written back to the old name")
	run.request("fs.delete", {"wsId": ws, "path": "src/followed.c"})


# ---------------------------------------------------------------------------------------------------------------
# Editing (Monaco on server documents)

def open_main_c(run, page):
	page.click(".tree-row:has-text('src')")
	page.wait_for_selector(".tree-row:has-text('main.c')")
	page.dblclick(".tree-row:has-text('main.c')")
	page.wait_for_selector(".monaco-editor .view-lines", timeout=30000)
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c') && ied_shell.views.entries.get('doc:src/main.c').instance && ied_shell.views.entries.get('doc:src/main.c').instance.sync.version > 0", timeout=20000)


def take_over(page):
	"""Edit Here, when a window of an earlier scenario still holds the lease (it lasts until two minutes idle, docs/11)."""
	if page.is_visible(".ed-bar button:has-text('Edit Here')"):
		page.click(".ed-bar button:has-text('Edit Here')")
		page.wait_for_selector(".ed-bar", state="hidden")


def editor_text(page):
	return page.evaluate("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue()")


def server_text(run):
	return run.request("fs.read", {"wsId": run.page.evaluate("ied_shell.wsId"), "path": "src/main.c"})["text"]


def settled(page):
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.sync.pending === 0", timeout=10000)


@scenario()
def edit_and_save(run):
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("// edited in IED\n")
	page.wait_for_selector(".tab:has-text('main.c') .dirty")
	run.shot("typed")
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(server_text(run).startswith("// edited in IED\n"), "saved to disk: %r" % server_text(run)[:40])
	check("Ln 2, Col 1" in run.texts(".statusbar .st")[2], "the status bar has the cursor: %s" % run.texts(".statusbar .st"))
	run.reload()
	open_main_c(run, page)
	take_over(page)
	check(editor_text(page).startswith("// edited in IED\n"), "the saved text after a reload")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	for _ in range(len("// edited in IED\n")):
		page.keyboard.press("Delete")
	settled(page)
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(not server_text(run).startswith("// edited"), "the file is as it was")


def restore(run, page, original):
	"""Puts main.c back as it was (through the server, so every window follows) and saves it."""
	sync = "ied_shell.views.entries.get('doc:src/main.c').instance.sync"
	settled(page)
	current = editor_text(page)
	lines = current.split("\n")
	whole = {"range": {"start": {"line": 0, "col": 0}, "end": {"line": len(lines) - 1, "col": len(lines[-1])}}, "text": original}
	run.request("testhooks.serverEdit", {"docId": page.evaluate(sync + ".docId"), "changes": [whole]})
	page.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue() === " + json.dumps(original), timeout=10000)
	page.evaluate(sync + ".save()")
	check(server_text(run) == original, "main.c is back as it was")


@scenario(viewports=("desktop",))
def two_windows_share_a_document(run):
	first = run.open()
	open_main_c(run, first)
	take_over(first)
	original = editor_text(first)
	first.click(".monaco-editor .view-lines")
	first.keyboard.press("Control+Home")
	first.keyboard.type("A")
	second = run.open(fresh=False)
	open_main_c(run, second)
	second.wait_for_selector(".ed-bar:has-text('another window is editing')")
	run.shot("read-only")
	first.keyboard.type("B")
	second.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue().startsWith('AB')", timeout=10000)
	second.click(".ed-bar button:has-text('Edit Here')")
	second.wait_for_selector(".ed-bar", state="hidden")
	first.wait_for_selector(".ed-bar:has-text('another window is editing')")
	second.click(".monaco-editor .view-lines")
	second.keyboard.press("Control+Home")
	second.keyboard.press("End")
	second.keyboard.type("C")
	first.wait_for_function("ied_shell.views.entries.get('doc:src/main.c').instance.model.getValue().split('\\n')[0].endsWith('C')", timeout=10000)
	run.page = first
	run.shot("mirrored")
	settled(second)
	check(editor_text(first) == editor_text(second), "both windows show the same text")
	restore(run, second, original)


@scenario(viewports=("desktop",))
def server_edits_while_typing(run):
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	original = editor_text(page)
	doc_id = page.evaluate("ied_shell.views.entries.get('doc:src/main.c').instance.sync.docId")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+End")
	for word in ("one ", "two ", "three "):
		page.keyboard.type(word)
		run.request("testhooks.serverEdit", {"docId": doc_id, "changes": [{"range": {"start": {"line": 0, "col": 0}, "end": {"line": 0, "col": 0}}, "text": "// ai\n"}]})
	settled(page)
	time.sleep(0.5)
	text = editor_text(page)
	check(text.count("// ai\n") == 3 and text.rstrip().endswith("one two three"), "both writers' text: %r" % text[-80:])
	page.keyboard.press("Control+S")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(server_text(run) == editor_text(page), "the editor and the server agree")
	run.shot("merged")
	restore(run, page, original)


# ---------------------------------------------------------------------------------------------------------------
# Phone

@scenario(viewports=("phone",))
def phone_panes(run):
	page = run.open()
	slots = run.texts(".phone-nav .pn > span:not(.ci)")
	check(slots[:3] == ["Editor", "Solution Explorer", "Output"] and slots[-1] == "More", "the slots: %s" % slots)
	page.wait_for_selector(".phone-pane .tree-row:has-text('README.md')")
	run.shot("explorer")
	page.click(".phone-nav .pn[data-pane='ied.output']")
	page.wait_for_selector(".phone-pane .output-text")
	run.shot("output")
	page.click(".phone-nav .pn[data-pane='ied.explorer']")
	page.click(".phone-pane .tree-row:has-text('README.md')")
	page.wait_for_selector(".phone-pane .monaco-editor .view-lines", timeout=30000)
	check(page.is_visible(".phone-head button[aria-label='Back']"), "a back button")
	check("README.md" in run.texts(".phone-head .t b"), "the pane title")
	run.shot("document")
	page.click(".phone-head button[aria-label='Back']")
	page.wait_for_selector(".phone-pane .tree-row")
	page.click(".phone-nav .pn[aria-label='More']")
	page.wait_for_selector(".menu.sheet")
	check("Keyboard Shortcuts" in run.texts(".menu.sheet .menu-item .label"), "More lists the rest")
	run.shot("more")
