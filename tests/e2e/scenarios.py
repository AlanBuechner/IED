"""The shell's interactions, end to end (docs/15, docs/19): each scenario drives the page the way a person does and
screenshots the states that matter. Both servers run every scenario unless it names one."""

import json
import os
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
	check(run.texts(".menubar .mi")[:3] == ["File", "Edit", "View"], "the menu bar in VS order: %s" % run.texts(".menubar .mi"))
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
	page.wait_for_selector(".kb-row")
	rows = run.texts(".kb-row .kb-label")
	check("View: Command Palette..." in rows and "View: Output" in rows, "Settings > Environment > Keyboard: %s" % rows[:20])
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
		page.select_option("select[aria-label='Show output from']", "ied")
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
	page.wait_for_selector('.sb-act[data-activity="shell.home"]')
	page.wait_for_selector('.sb-act[data-activity="ied.chat"].on')
	page.wait_for_selector(".panel-title:has-text('Solution Explorer')", state="hidden")
	run.shot("disabled")
	page.click("button.mi:has-text('Extensions')")
	page.click(".menu .menu-item:has-text('Manage Plugins')")
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
	page.click(".toast button:has-text('Undo')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name)
	check(exists(run, "src/%s-renamed.c" % name), "Undo brought it back from the trash")
	page.click(".tree-row:has-text('%s-renamed.c')" % name)
	page.keyboard.press("Delete")
	page.click(".popup.dialog button:has-text('Delete')")
	page.wait_for_selector(".tree-row:has-text('%s-renamed.c')" % name, state="detached")
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


@scenario()
def find_in_files(run):
	"""Ctrl+Shift+F, a query, the results by file, and a match opened at its place (docs/10 fs.search)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	token = "findme_%s_%d" % (run.viewport, int(time.time()))
	run.request("fs.write", {"wsId": ws, "path": "src/find-%s.c" % run.viewport, "text": "int other;\n\tint %s = 1;\n" % token})
	page.click(".tree-row:has-text('README.md')")
	page.keyboard.press("Control+Shift+F")
	page.wait_for_selector(".fif-query")
	page.wait_for_function("document.activeElement && document.activeElement.classList.contains('fif-query')")
	page.keyboard.type(token)
	page.keyboard.press("Enter")
	page.wait_for_selector(".fif-results .tree-row:has-text('find-%s.c')" % run.viewport)
	check("1 match in 1 file" in page.text_content(".fif-status"), "the count: %s" % page.text_content(".fif-status"))
	run.shot("results")
	page.dblclick(".fif-results .tree-row:has-text('%s')" % token)
	page.wait_for_selector(".tab:has-text('find-%s.c')" % run.viewport)
	page.wait_for_function("document.querySelector('.statusbar') && document.querySelector('.statusbar').textContent.includes('Ln 2, Col 6')", timeout=20000)
	run.request("fs.delete", {"wsId": ws, "path": "src/find-%s.c" % run.viewport})


@scenario()
def go_to_all(run):
	"""Ctrl+, finds a file by a few letters of its name and opens it (docs/10 fs.files); Escape closes the picker."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	name = "gotoall%s%d.c" % (run.viewport, int(time.time()))
	run.request("fs.write", {"wsId": ws, "path": "src/deep/%s" % name, "text": "int x;\n"})
	page.click(".tree-row:has-text('README.md')")
	page.keyboard.press("Control+Comma")
	page.wait_for_selector(".picker .palette-input")
	page.keyboard.press("Escape")
	page.wait_for_selector(".picker", state="detached")
	page.keyboard.press("Control+Comma")
	page.wait_for_selector(".picker .palette-input")
	page.keyboard.type("gtall" + name[7:-2])
	page.wait_for_selector(".picker .menu-item.sel:has-text('%s')" % name)
	check("src/deep" in page.text_content(".picker .menu-item.sel"), "the folder beside the name")
	run.shot("picker")
	page.keyboard.press("Enter")
	page.wait_for_selector(".tab:has-text('%s')" % name)
	check(page.query_selector(".picker") is None, "the picker closes on a pick")
	run.request("fs.delete", {"wsId": ws, "path": "src/deep/%s" % name})


def active_tab(page):
	return page.evaluate("ied_shell.context.get('activeTab')")


@scenario(viewports=("desktop",))
def navigate_back_and_forward(run):
	"""Ctrl+- goes back to where the cursor was before a jump to another document, Ctrl+Shift+- forward again."""
	page = run.open()
	open_main_c(run, page)
	page.click(".monaco-editor .view-lines")
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'README.md', line: 0, col: 0 })")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:README.md'", timeout=20000)
	page.wait_for_function("ied_shell.views.entries.get('doc:README.md').instance.editor.hasTextFocus()", timeout=20000)
	page.keyboard.press("Control+Minus")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:src/main.c'", timeout=20000)
	page.keyboard.press("Control+Shift+Minus")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:README.md'", timeout=20000)
	check(active_tab(page) == "doc:README.md", "forward again")


@scenario(viewports=("desktop",))
def toggle_header_and_code(run):
	"""Ctrl+K, Ctrl+O between a C file and its header (the languages plugin's headerSourcePairs)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	for name in ("pair.c", "pair.h"):
		run.request("fs.write", {"wsId": ws, "path": "src/" + name, "text": "int pair;\n"})
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'src/pair.c', line: 0 })")
	page.wait_for_function("ied_shell.views.entries.get('doc:src/pair.c') && ied_shell.views.entries.get('doc:src/pair.c').instance.editor.hasTextFocus()", timeout=20000)
	page.keyboard.press("Control+K")
	page.keyboard.press("Control+O")
	page.wait_for_function("ied_shell.context.get('activeTab') === 'doc:src/pair.h'", timeout=20000)
	header = "ied_shell.views.entries.get('doc:src/pair.h').instance"
	page.wait_for_function("%s && %s.model && %s.sync.holdsLease && %s.editor.hasTextFocus()" % (header, header, header, header), timeout=20000)
	page.keyboard.press("Control+K")
	page.keyboard.press("Control+C")
	page.wait_for_function("%s.model.getValue().startsWith('//')" % header, timeout=10000)
	check(True, "Ctrl+K, Ctrl+C still comments the line")
	for name in ("pair.c", "pair.h"):
		run.request("fs.delete", {"wsId": ws, "path": "src/" + name})


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
	check("Ln 2, Col 1" in run.texts(".statusbar [data-item='ied.editor.status']")[0], "the status bar has the cursor: %s" % run.texts(".statusbar .st"))
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


@scenario(viewports=("desktop",))
def undo_to_saved_is_clean(run):
	"""Typing marks the tab unsaved; undoing back to the file's text clears the mark (docs/10 "dirty")."""
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	before = server_text(run)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("xy")
	page.wait_for_selector(".tab:has-text('main.c') .dirty")
	page.keyboard.press("Control+Z")
	page.wait_for_selector(".tab:has-text('main.c') .dirty", state="detached")
	check(editor_text(page) == before and server_text(run) == before, "the text is the file's, which was not written")


@scenario(viewports=("desktop",))
def track_changes_margin(run):
	"""A typed line is marked unsaved, then saved once saved; undone and saved again, the file is as it was and
	nothing is marked (docs/10 doc.baseline)."""
	page = run.open()
	open_main_c(run, page)
	take_over(page)
	before = editor_text(page)
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+Home")
	page.keyboard.type("// margin\n")
	page.wait_for_selector(".monaco-editor .tc-unsaved")
	run.shot("unsaved")
	page.keyboard.press("Control+S")
	# Earlier scenarios may have saved main.c already, so saved marks alone do not say this save landed.
	page.wait_for_selector(".monaco-editor .tc-unsaved", state="detached", timeout=10000)
	check(page.query_selector(".monaco-editor .tc-saved") is not None, "saved: the edited line is marked saved")
	run.shot("saved")
	for _ in range(20):
		if editor_text(page) == before:
			break
		page.keyboard.press("Control+Z")
	settled(page)
	page.keyboard.press("Control+S")
	page.wait_for_selector(".monaco-editor .tc-saved", state="detached")
	check(page.query_selector(".monaco-editor .tc-unsaved") is None and server_text(run) == before, "the file is as it was")


@scenario(viewports=("desktop",))
def file_outside_the_workspace(run):
	"""A file a language server named outside the workspace opens read-only at its place, its tab named by the file,
	the bar saying where it is (docs/10 "Files outside the workspace")."""
	page = run.open()
	name = "outside%d.h" % int(time.time())
	uri = run.request("testhooks.external", {"name": name, "text": "int outside;\n"})["uri"]
	page.evaluate("(uri) => ied_shell.commands.run('ied.editor.goTo', { path: uri, line: 0, col: 4 })", uri)
	page.wait_for_selector(".tab:has-text('%s')" % name)
	page.wait_for_selector(".ed-bar:has-text('outside the workspace')")
	model = "ied_shell.views.entries.get('doc:' + uri).instance.model"
	page.wait_for_function("(uri) => { const e = ied_shell.views.entries.get('doc:' + uri); return !!(e && e.instance && e.instance.model); }", arg=uri, timeout=20000)
	run.shot("external")
	page.keyboard.type("x")
	check(page.evaluate("(uri) => " + model + ".getValue()", uri) == "int outside;\n", "read-only: typing changes nothing")
	check("Ln 1, Col 5" in run.texts(".statusbar [data-item='ied.editor.status']")[0], "at its place: %s" % run.texts(".statusbar .st"))


@scenario(viewports=("desktop", "phone"))
def markdown_preview_ticks_a_task(run):
	"""The preview of a Markdown file shows its tasks, tables and code; ticking a box writes "[x]" into the document
	(J10, F-EDT-91/92)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/tasks-%s.md" % run.viewport
	run.request("fs.write", {"wsId": ws, "path": path, "text": "# Tasks\n\n- [ ] write the preview\n- [x] test it\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```c\nint x;\n```\n"})
	page.evaluate("(path) => ied_shell.commands.run('ied.markdown.preview', { path })", path)
	page.wait_for_selector(".md-preview input[data-task='2']")
	check(page.query_selector(".md-preview table td") is not None and page.query_selector(".md-preview pre") is not None, "the table and the code block")
	run.shot("preview")
	page.click(".md-preview input[data-task='2']")
	doc = "ied_shell.views.entries.get('mdpreview:' + path).instance.sync"
	page.wait_for_function("(path) => " + doc + ".pending === 0 && " + doc + ".dirty", arg=path, timeout=10000)
	page.wait_for_selector(".md-preview input[data-task='2'][checked]")
	page.evaluate("(path) => " + doc + ".save()", path)
	check("- [x] write the preview" in run.request("fs.read", {"wsId": ws, "path": path})["text"], "the box is ticked in the file")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario(viewports=("desktop",))
def launched_markdown_opens_with_its_preview(run):
	"""A launch URL's #open= (what `ied open notes.md` gives, docs/10): the file opens at its place with its preview
	beside it, where a box is ticked into the file (J10)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	path = "docs/launched.md"
	run.request("fs.write", {"wsId": ws, "path": path, "text": "# Notes\n\n- [ ] call back\n"})
	page.goto(run.target.url + "/#open=" + ws + "/" + path + ":3")
	run.reload()
	page.wait_for_selector(".tab:has-text('Preview launched.md')")
	page.wait_for_function("(path) => { const e = ied_shell.views.entries.get('doc:' + path); return !!(e && e.instance && e.instance.model); }", arg=path, timeout=20000)
	check(page.evaluate("location.hash") == "", "the fragment is read once")
	page.click(".tab:has-text('Preview launched.md')")
	page.click(".md-preview input[data-task='2']")
	sync = "ied_shell.views.entries.get('mdpreview:' + path).instance.sync"
	page.wait_for_function("(path) => " + sync + ".pending === 0 && " + sync + ".dirty", arg=path, timeout=10000)
	page.evaluate("(path) => " + sync + ".save()", path)
	check(run.request("fs.read", {"wsId": ws, "path": path})["text"] == "# Notes\n\n- [x] call back\n", "ticked in the file")
	run.request("fs.delete", {"wsId": ws, "path": path})


@scenario(viewports=("desktop",), targets=("mock",))
def build_from_the_toolbar(run):
	"""The startup project's toolbar (docs/06): its configuration dropdown shared by every window, Ctrl+Shift+B building
	with the status bar counting and the Output showing the build, and a failed build turning the status bar red - still
	red after a reload - a click on it opening the Error List."""
	page = run.open()
	page.wait_for_selector(".toolbar .tb-item select[aria-label='Configuration']")
	check(page.query_selector(".toolbar .tb-item button[aria-label='Build']") is not None, "the build button")
	page.select_option(".toolbar select[aria-label='Configuration']", "Debug")
	other = run.open(fresh=False)
	other.wait_for_function("document.querySelector(\".toolbar select[aria-label='Configuration']\") && document.querySelector(\".toolbar select[aria-label='Configuration']\").value === 'Debug'", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.click(".dock-group")
	page.keyboard.press("Control+Shift+B")
	page.wait_for_selector(".statusbar[data-state='building']")
	run.wait_text(".statusbar", "Build demo", timeout=10000)
	run.wait_text(".output-text .line", "Build demo | Debug | x64", timeout=10000)
	run.shot("building")
	page.wait_for_function("document.querySelector(\"select[aria-label='Show output from']\") && document.querySelector(\"select[aria-label='Show output from']\").value === 'build'", timeout=10000)
	run.wait_text(".statusbar", "Build succeeded", timeout=20000)
	check(page.query_selector(".statusbar[data-state='building']") is None, "the status bar is plain again")
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.wait_for_selector(".statusbar[data-state='failed']", timeout=20000)
	run.wait_text(".statusbar", "Build (with an error) failed", timeout=5000)
	run.shot("failed")
	run.reload()
	page.wait_for_selector(".statusbar[data-state='failed']", timeout=10000)
	run.wait_text(".statusbar", "Build (with an error) failed", timeout=5000)
	page.click(".statusbar [data-item='ied.tasks.status']")
	page.wait_for_selector(".tab.active:has-text('Error List'), .dock-tab.active:has-text('Error List'), [role='tab'][aria-selected='true']:has-text('Error List')", timeout=10000)


EDITOR = "ied_shell.views.entries.get('doc:src/main.c').instance.editor"


def current_line(page):
	"""The 1-based line the current-statement decoration is on, or None."""
	return page.evaluate("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d ? d.range.startLineNumber : null; })()")


@scenario(viewports=("desktop",), targets=("mock",))
def debug_a_program(run):
	"""F9 sets a breakpoint, F5 starts the startup project's configuration and stops there - the current statement shown
	in the editor, the Call Stack and Locals filled, the status bar in the debugging color - F10 steps, another window
	shows the same stop, and Shift+F5 ends the session (docs/25)."""
	page = run.open()
	open_main_c(run, page)
	page.evaluate(EDITOR + ".setPosition({ lineNumber: 6, column: 2 })")
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("F9")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-set")
	page.wait_for_selector(".toolbar button[aria-label='Start Debugging']")
	page.keyboard.press("F5")
	page.wait_for_function("(() => { const e = " + EDITOR + "; return e.getModel().getAllDecorations().some((d) => d.options.className === 'dbg-current-line'); })()", timeout=10000)
	check(current_line(page) == 6, "stopped at the breakpoint: line %s" % current_line(page))
	page.wait_for_selector(".statusbar[data-state='debugging']")
	page.wait_for_selector(".toolbar button[aria-label='Step Over (F10)']:not([disabled])")
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.callStack' })")
	run.wait_text(".cs-row", "main.c, line 6")
	check(len(page.query_selector_all("[aria-label='Call Stack'] .cs-row")) == 2, "two frames")
	run.shot("stopped")
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.locals' })")
	run.wait_text(".tree-row", "renderer")
	page.click(".tree-row:has-text('player') .tw")
	page.wait_for_function("[...document.querySelectorAll('.tree-row .lv-name')].some((e) => e.textContent === 'position')", timeout=10000)
	run.shot("locals")
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("F10")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	other = run.open(fresh=False)
	open_main_c(run, other)
	other.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.evaluate(EDITOR + ".focus()")
	page.keyboard.press("Shift+F5")
	page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
	check(current_line(page) is None, "the current statement is gone with the session")
	page.evaluate(EDITOR + ".setPosition({ lineNumber: 6, column: 2 })")
	page.keyboard.press("F9")
	page.wait_for_selector(".monaco-editor .bp-glyph", state="detached")


@scenario(viewports=("desktop",), targets=("mock",))
def debug_windows(run):
	"""Watch evaluates the workspace's expressions at each stop, a hover shows a value (DataTips), Threads lists the
	program's threads, and the Breakpoints window disables one (docs/25)."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 5}]})
	run.request("watch.set", {"wsId": ws, "expressions": []})
	open_main_c(run, page)
	page.evaluate("ied_shell.commands.run('ied.debug.start')")
	page.wait_for_selector(".statusbar[data-state='debugging']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; return e.getModel().getAllDecorations().some((d) => d.options.className === 'dbg-current-line'); })()", timeout=10000)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.watch' })")
	page.fill(".watch-add", "renderer")
	page.press(".watch-add", "Enter")
	page.wait_for_function("[...document.querySelectorAll('.tree-row')].some((r) => r.textContent.includes('renderer') && r.textContent.includes('0x000001f4a2b0e000'))", timeout=10000)
	page.fill(".watch-add", "nonsense")
	page.press(".watch-add", "Enter")
	page.wait_for_selector(".tree-row .lv-value.c-error", timeout=10000)
	run.shot("watch")
	box = page.evaluate("(() => { const e = " + EDITOR + "; const p = e.getScrolledVisiblePosition({ lineNumber: 6, column: 11 }); const r = e.getDomNode().getBoundingClientRect(); return { x: r.left + p.left + 4, y: r.top + p.top + p.height / 2 }; })()")
	page.mouse.move(box["x"], box["y"])
	page.wait_for_selector(".monaco-hover:has-text('player = ')", timeout=10000)
	run.shot("hover")
	page.mouse.move(5, 5)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.threads' })")
	run.wait_text(".cs-row", "main")
	page.keyboard.press("Control+Alt+B")
	page.wait_for_selector(".bp-grid.cs-row:has-text('main.c, line 6')")
	page.click(".bp-grid.cs-row input[type='checkbox']")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-disabled", timeout=10000)
	run.shot("breakpoints")
	page.evaluate("ied_shell.commands.run('ied.debug.stop')")
	page.wait_for_selector(".statusbar[data-state='debugging']", state="detached", timeout=10000)
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})
	run.request("watch.set", {"wsId": ws, "expressions": []})


@scenario(viewports=("phone",), targets=("mock",))
def debug_on_a_phone(run):
	"""On a phone the run controls are in the toolbar strip: Start Debugging stops at the breakpoint with the statement
	shown in the editor, Step Over moves it, the Call Stack is a pane, and Stop ends the session."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": [{"line": 5}]})
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'src/main.c', line: 5 })")
	page.wait_for_selector(".monaco-editor .bp-glyph.bp-set", timeout=20000)
	page.tap(".phone-toolbar button[aria-label='Start Debugging']")
	page.wait_for_selector(".phone-toolbar[data-state='debugging']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 6; })()", timeout=10000)
	page.wait_for_selector(".phone-toolbar button[aria-label='Step Over (F10)']:not([disabled])")
	run.shot("stopped")
	page.tap(".phone-toolbar button[aria-label='Step Over (F10)']")
	page.wait_for_function("(() => { const e = " + EDITOR + "; const d = e.getModel().getAllDecorations().find((d) => d.options.className === 'dbg-current-line'); return d && d.range.startLineNumber === 7; })()", timeout=10000)
	page.evaluate("ied_shell.commands.run('shell.showView', { id: 'ied.callStack' })")
	run.wait_text(".cs-row", "main.c, line 7")
	run.shot("call-stack")
	page.tap(".phone-toolbar button[aria-label='Stop Debugging (Shift+F5)']")
	page.wait_for_selector(".phone-toolbar[data-state='debugging']", state="detached", timeout=10000)
	run.request("bp.set", {"wsId": ws, "path": "src/main.c", "breakpoints": []})


def terminal_text(page):
	"""What the shown terminal's screen says (xterm's rows)."""
	return page.evaluate("[...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].map((r) => r.textContent).join('\n')")


def type_in_terminal(page, text):
	page.click(".term-view:not([hidden]) .xterm-screen")
	page.keyboard.type(text)
	page.keyboard.press("Enter")


@scenario(viewports=("desktop",))
def terminal_dock(run):
	"""The terminal dock under every activity (docs/15): New Terminal opens it with a shell on the server, typed commands
	run, another window shows the same screen and takes over typing, Ctrl+` collapses it to its header, Kill ends it."""
	page = run.open()
	page.wait_for_selector(".panel-dock .term-head")
	check(page.query_selector(".panel-dock.collapsed") is not None, "collapsed to its header at first")
	page.click(".term-head button[aria-label='New Terminal']")
	page.wait_for_selector(".panel-dock:not(.collapsed) .term-tab.on")
	page.wait_for_selector(".term-view:not([hidden]) .xterm-screen")
	marker = "ied-e2e-" + run.viewport
	type_in_terminal(page, "echo " + marker)
	page.wait_for_function("(m) => [...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].filter((r) => r.textContent.includes(m)).length >= 2", arg=marker, timeout=30000)
	run.shot("typed")
	other = run.open(fresh=False)
	other.wait_for_selector(".term-tab")
	other.click(".term-tab")
	other.wait_for_function("(m) => [...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].some((r) => r.textContent.includes(m))", arg=marker, timeout=20000)
	other.wait_for_selector(".term-view:not([hidden]) .term-bar:not([hidden])")
	other.click(".term-view:not([hidden]) .term-bar button")
	other.wait_for_selector(".term-view:not([hidden]) .term-bar", state="hidden")
	page.wait_for_selector(".term-view:not([hidden]) .term-bar:not([hidden])", timeout=10000)
	run.page = page
	page.bring_to_front()
	page.click(".term-head")
	page.keyboard.press("Control+`")
	page.wait_for_selector(".panel-dock.collapsed")
	page.keyboard.press("Control+`")
	page.wait_for_selector(".panel-dock:not(.collapsed)")
	page.click(".term-head button[aria-label='Kill Terminal']")
	page.wait_for_selector(".term-tab", state="detached", timeout=10000)
	check(page.is_visible(".term-empty"), "says how to open one")


@scenario(viewports=("phone",), targets=("mock",))
def terminal_on_a_phone(run):
	"""On a phone the terminal is a slide-up drawer from More, with a row of the keys a phone keyboard lacks."""
	page = run.open()
	page.tap(".phone-nav button[aria-label='More']")
	page.tap(".menu [role='menuitem']:has-text('Terminal')")
	page.wait_for_selector(".panel-dock.drawer.shown")
	page.tap(".term-head button[aria-label='New Terminal']")
	page.wait_for_selector(".term-view:not([hidden]) .xterm-screen")
	page.wait_for_selector(".term-keys button:has-text('Tab')")
	type_in_terminal(page, "echo from-the-phone")
	page.wait_for_function("[...document.querySelectorAll('.term-view:not([hidden]) .xterm-rows > div')].filter((r) => r.textContent.includes('from-the-phone')).length >= 2", timeout=20000)
	run.shot("drawer")
	page.tap(".term-head button[aria-label='Kill Terminal']")
	page.wait_for_selector(".term-tab", state="detached", timeout=10000)
	page.tap(".term-head button[aria-label='Hide the panel']")
	page.wait_for_selector(".panel-dock.drawer.shown", state="detached")


@scenario(viewports=("desktop",), targets=("mock",))
def output_links_open_places(run):
	"""A place an Output line names is underlined and a click opens it (VS): a compiler's error opens its file at the line."""
	page = run.open()
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.evaluate("ied_shell.commands.run('ied.output.show', { sourceId: 'build' })")
	page.wait_for_selector(".output-text .out-loc:has-text('player.c(11,61)')", timeout=20000)
	run.shot("links")
	page.click(".output-text .out-loc:has-text('player.c(11,61)')")
	page.wait_for_function("(() => { const e = ied_shell.views.entries.get('doc:src/game/player.c'); return !!(e && e.instance && e.instance.editor && e.instance.editor.getPosition() && e.instance.editor.getPosition().lineNumber === 11); })()", timeout=20000)


@scenario(viewports=("phone",), targets=("mock",))
def build_from_the_phone_strip(run):
	"""On a phone the toolbar views sit in a strip above the bottom nav (docs/15 "Phone"), colored like the status bar:
	a tap on Build shows the build's output, the strip is blue while it runs and red after a failed build; More offers
	the Command Palette for everything else."""
	page = run.open()
	page.wait_for_selector(".phone-toolbar select[aria-label='Configuration']")
	config = page.eval_on_selector(".phone-toolbar select[aria-label='Configuration']", "el => el.value")
	page.tap(".phone-toolbar button[aria-label='Build']")
	page.wait_for_selector(".phone-toolbar[data-state='building']")
	run.wait_text(".output-text .line", "Build demo | %s | x64" % config, timeout=10000)
	run.shot("building")
	page.wait_for_selector(".phone-toolbar[data-state='idle']", timeout=20000)
	run.request("task.run", {"wsId": "w1", "task": "build-broken"})
	page.wait_for_selector(".phone-toolbar[data-state='failed']", timeout=20000)
	run.shot("failed")
	page.tap(".phone-nav button[aria-label='More']")
	page.wait_for_selector(".menu [role='menuitem']:has-text('Command Palette'), .sheet :text('Command Palette')", timeout=5000)
	run.shot("more")


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
	check(slots[:4] == ["Chat", "Editor", "Git", "Solution Explorer"] and slots[-1] == "More", "the slots: %s" % slots)
	page.click(".phone-nav .pn[data-pane='ied.explorer']")
	page.wait_for_selector(".phone-pane .tree-row:has-text('README.md')")
	run.shot("explorer")
	page.click(".phone-nav .pn[aria-label='More']")
	page.click(".menu.sheet .menu-item:has-text('Output')")
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
	more = run.texts(".menu.sheet .menu-item .label")
	check("Keyboard Shortcuts" in more and "Settings" in more, "More lists the rest, Settings among it (no gear on a phone)")
	run.shot("more")


# ---------------------------------------------------------------------------------------------------------------
# Chat (docs/10 "Chats"): the mock plays its scripted turns; the real server talks to tools/fake-model.

def model_script(*steps):
	return {"description": "e2e", "stopClass": "none", "harness": "-", "responses": [{"chunkDelayMs": 20, "steps": [{"role": "assistant"}] + list(steps) + [{"usage": True}, {"done": True}]}]}


def open_chat(run):
	"""The Chat activity on a new Ask chat, picked by its tab; on the real server with the fake model."""
	page = run.open()
	if run.target.kind == "real":
		run.request("settings.set", {"tier": "app", "key": "ai.models", "value": {"fake": {"endpoint": run.target.model_url + "/v1", "watchdog": {"prefillRate": 1000000}}}})
	ws_id = page.evaluate("ied_shell.wsId")
	created = run.request("chat.create", {"wsId": ws_id, "mode": "ask"})
	run.show_activity("ied.chat")
	page.click(".chat-tab[data-chat='%s']" % created["chatId"])
	page.wait_for_selector(".chat-tab.on[data-chat='%s']" % created["chatId"])
	return page


def streaming(page):
	page.wait_for_function("(document.querySelector('.chat-turn:last-child .chat-text') || {textContent: ''}).textContent.length > 40", timeout=20000)


def send(page, text):
	page.fill(".chat-input", text)
	page.press(".chat-input", "Enter")


@scenario(viewports=("desktop", "phone"))
def chat_turn_with_approval(run):
	page = open_chat(run)
	if run.target.kind == "real":
		edit = {"path": "src/main.c", "edits": [{"old": "int main(void)", "new": "/* the entry point */\nint main(void)"}]}
		run.target.queue_model(
			model_script({"text": "I'll document the entry point:", "chunk": 6}, {"toolCall": {"name": "fs_edit", "arguments": edit}}, {"finish": "tool_calls"}),
			model_script({"text": "Done: `main` is documented.", "chunk": 6}, {"finish": "stop"}))
	send(page, "clamp the velocity")
	page.wait_for_selector(".chat-approval .chat-diff .dl.add", timeout=20000)
	check(page.is_visible(".chat-tab .glyph.attn") or run.phone, "the tab says the chat waits on its owner")
	run.shot("approval")
	page.click(".chat-approval .btn.primary")
	page.wait_for_selector(".chat-foot:has-text('Finished')", timeout=30000)
	states = page.eval_on_selector_all(".chat-tool, .chat-finish", "els => els.map(e => e.dataset.state || 'finish')")
	check("ok" in states and "awaiting_approval" not in states, "the edit ran: %s" % states)
	if not run.phone:
		check(page.is_visible(".chat-panel .chat-sec-row.file"), "the changed file is listed in the panel")
	run.shot("finished")


@scenario(viewports=("desktop", "phone"), targets=("mock",))
def chat_tool_image(run):
	"""A tool's result shows an image (docs/13 "Images in tool results"): the card shows it, open or closed. The real
	server's side is a server check (test_chat), which needs a plugin with code."""
	page = open_chat(run)
	send(page, "what is on the screen?")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=20000)
	page.wait_for_function("(() => { const i = document.querySelector('.chat-tool-images img'); return i && i.complete && i.naturalWidth === 320; })()", timeout=10000)
	check(page.get_attribute(".chat-tool-images a", "title") == "shots/screen.png", "the image's path on hover")
	run.shot("image")


@scenario(viewports=("desktop",))
def chat_reload_mid_turn(run):
	page = open_chat(run)
	text = " ".join("Sentence %d of a long answer." % i for i in range(1, 301))
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": text, "chunk": 8, "delayMs": 15}, {"finish": "stop"}))
	send(page, "write a long essay")
	streaming(page)
	page.reload()
	run.wait_ready()
	page.wait_for_selector(".chat-act", timeout=20000)
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=60000)
	if run.target.kind == "real":
		run.wait_text(".chat-turn:last-child .chat-text", "Sentence 300 of a long answer.")
	else:
		page.wait_for_function("document.querySelectorAll('.chat-turn:last-child .chat-text li').length === 40", timeout=10000)
	run.shot("finished")


@scenario(viewports=("desktop",))
def chat_stop_and_queue(run):
	page = open_chat(run)
	if run.target.kind == "real":
		run.target.queue_model(model_script({"text": "word " * 2000, "chunk": 5, "delayMs": 20}, {"finish": "stop"}), model_script({"text": "The queued answer.", "chunk": 6}, {"finish": "stop"}))
	send(page, "write a long essay")
	streaming(page)
	send(page, "and then summarize it")
	page.wait_for_selector(".chat-queued:has-text('and then summarize it')")
	run.shot("queued")
	page.click(".chat-row2 .btn:has-text('Stop')")
	page.wait_for_selector(".chat-foot:has-text('Stopped')", timeout=20000)
	check(page.is_visible(".chat-queued"), "a stopped turn leaves the queue for the owner")
	run.shot("stopped")
	page.click(".chat-queued button[aria-label='Send the queued message now']")
	page.wait_for_selector(".chat-turn:last-child .chat-foot:has-text('Finished')", timeout=30000)
	check(not page.is_visible(".chat-queued") and "and then summarize it" in run.texts(".chat-turn:last-child .chat-user"), "the queued message was sent")


# ---------------------------------------------------------------------------------------------------------------
# Settings (docs/07 "The settings window as built")

def open_settings(run, page):
	if run.phone:
		page.evaluate("ied_shell.commands.run('shell.settings')")
	else:
		page.click(".sidebar button[aria-label='Manage']")
		page.click(".menu .menu-item:has-text('Settings')")
	page.wait_for_selector(".settings-window .set-node")


@scenario(viewports=("desktop", "phone"))
def settings_window(run):
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='page:AI']")
	field = ".set-row[data-key='ai.maxSteps'] input"
	page.fill(field, "120")
	page.press(field, "Enter")
	page.wait_for_selector(".set-row[data-key='ai.maxSteps'] .set-source.set:has-text('User')")
	page.fill(field, "-1")
	page.press(field, "Enter")
	run.wait_text(".set-row[data-key='ai.maxSteps'] .set-message", "less than minimum")
	run.shot("refused")
	page.click(".set-row[data-key='ai.maxSteps'] button:has-text('Reset')")
	page.wait_for_selector(".set-row[data-key='ai.maxSteps'] .set-source:not(.set):has-text('Default')")
	page.fill(".set-search", "clangd")
	page.wait_for_selector(".set-row[data-key='cpp.clangd.path']")
	check(not page.is_visible(".set-row[data-key='ai.maxSteps']"), "search keeps only what matches")
	run.shot("search")


@scenario(viewports=("desktop",))
def model_settings(run):
	page = run.open()
	open_settings(run, page)
	page.click(".set-node[data-page='custom:ied.chat.models']")
	page.click(".set-bar button:has-text('Add model')")
	endpoint = run.target.model_url + "/v1" if run.target.kind == "real" else "http://127.0.0.1:8080/v1"
	page.fill(".set-model.editing input[aria-label='Name']", "local-test")
	page.fill(".set-model.editing input[aria-label='Endpoint']", endpoint)
	page.fill(".set-model.editing input[aria-label='Model id']", "fake-model")
	page.click(".set-model.editing button:has-text('Test connection')")
	page.wait_for_selector(".set-model.editing .set-probe.ok", timeout=15000)
	run.shot("tested")
	page.click(".set-model.editing button:has-text('Save')")
	page.wait_for_selector(".set-model[data-model='local-test']:not(.editing)")
	page.click(".set-node[data-page='custom:ied.chat.groups']")
	page.click(".set-bar button:has-text('New')")
	page.wait_for_selector(".set-role[data-role='primary'][data-state='on']")
	check(page.is_visible(".set-role[data-role='planning'][data-state='off']"), "a new group turns on primary alone")
	page.click(".set-role[data-role='planning'] .set-role-toggle")
	page.wait_for_selector(".set-role[data-role='planning'][data-state='on']")
	primary = page.eval_on_selector(".set-role[data-role='primary'] .set-role-model", "e => e.value")
	check(primary and page.eval_on_selector(".set-role[data-role='planning'] .set-role-model", "e => e.value") == primary, "a role turned on gets the primary's model")
	page.click(".set-bar button:has-text('Make default')")
	page.wait_for_selector(".set-group.on .ci")
	run.shot("groups")
	groups = run.request("settings.get", {"wsId": page.evaluate("ied_shell.wsId")})["values"]
	check(groups["ai.defaultGroup"]["value"] == "group" and groups["ai.groups"]["value"]["group"]["planning"]["model"] == primary, "written to the User tier: %s" % groups["ai.groups"])


# ---------------------------------------------------------------------------------------------------------------
# Git (docs/10 "Git", mockup 05): the mock's canned repository; the real server's is the fixture made a repository
# (targets.make_repository).

def open_git(run):
	page = run.open()
	if run.target.kind == "real":
		run.request("ws.setTrust", {"wsId": page.evaluate("ied_shell.wsId"), "trusted": True})
	run.show_activity("ied.git")
	if run.phone:
		page.click(".phone-nav .pn[data-pane='activity:ied.git']")
	page.wait_for_selector(".git-row:not(.local)", timeout=20000)
	return page


@scenario(viewports=("desktop",))
def git_stage_commit_branch_search(run):
	page = open_git(run)
	real = run.target.kind == "real"
	path = "src/e2e-notes.txt" if real else "src/game/player.c"
	if real:
		with open(os.path.join(run.target.workspace, "src", "e2e-notes.txt"), "w", encoding="utf-8") as f:
			f.write("from the e2e run" + chr(10))
	page.wait_for_selector(".git-row.local")
	check("main" in run.texts(".git-row .ref.head") if real else "port/logging" in run.texts(".git-row .ref.head"), "HEAD's branch labels its commit")
	run.shot("graph")
	page.click(".git-row.local")
	row = ".git-changes .git-file[data-path='%s']" % path
	page.hover(row)
	page.click(row + " button[aria-label='Stage %s']" % path)
	page.wait_for_selector(row + " button[aria-label='Unstage %s']" % path, state="attached")
	page.click(row)
	page.wait_for_selector(".git-diff .dl.add")
	run.shot("staged")
	page.fill(".git-message", "e2e: a commit")
	page.click(".git-composer button:has-text('Commit')")
	page.wait_for_selector(".git-row:has-text('e2e: a commit')")
	page.click(".git-row:has-text('e2e: a commit')")
	page.wait_for_selector(".cmeta .subject:has-text('e2e: a commit')")
	page.wait_for_selector(".cfiles .git-file[data-path='%s']" % path)
	page.wait_for_selector(".git-diff .dl")
	run.shot("details")
	page.click(".git-bar button:has-text('Branch')")
	page.fill(".dialog input", "e2e/branch")
	page.press(".dialog input", "Enter")
	page.wait_for_selector(".git-refs .row.sel-dim:has-text('e2e/branch')")
	page.fill(".git-search", "Fixture" if real else "Ring")
	page.wait_for_function("document.querySelectorAll('.git-row').length === 1")
	run.shot("search")
	repo = run.request("git.repos", {"wsId": page.evaluate("ied_shell.wsId")})["repos"][0]["repoId"]
	run.request("git.checkout", {"repoId": repo, "branch": "main" if real else "port/logging"})


@scenario(viewports=("phone",))
def git_on_a_phone(run):
	page = open_git(run)
	run.shot("graph")
	page.click(".git-row:not(.local) >> nth=0")
	page.wait_for_selector(".git-act.detail-open .cmeta .subject")
	run.shot("details")
	page.click(".git-back")
	page.wait_for_selector(".git-row:not(.local)")


# ---------------------------------------------------------------------------------------------------------------
# White space (editor.renderWhitespace, on by default): View White Space (Ctrl+R, Ctrl+W) turns it off in every open
# editor and records it in the User tier.

@scenario(viewports=("desktop",))
def view_white_space(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	shown = "monaco.editor.getEditors().filter((e) => e.getModel()).map((e) => e.getOption(monaco.editor.EditorOption.renderWhitespace))"
	check(page.evaluate(shown) == ["all"], "white space shows by default: %s" % page.evaluate(shown))
	run.shot("shown")
	page.click(".monaco-editor .view-lines")
	page.keyboard.press("Control+r")
	page.keyboard.press("Control+w")
	page.wait_for_function(shown + ".every((option) => option === 'none')")
	values = run.request("settings.get", {"wsId": page.evaluate("ied_shell.wsId")})["values"]
	check(values["editor.renderWhitespace"] == {"value": "none", "source": "app"}, "recorded in the User tier: %s" % values["editor.renderWhitespace"])
	run.shot("hidden")
	run.request("settings.reset", {"tier": "app", "key": "editor.renderWhitespace"})
	page.wait_for_function(shown + ".every((option) => option === 'all')")


# ---------------------------------------------------------------------------------------------------------------
# Keyboard (docs/15 "The editor's keys", docs/07 "Host settings"): Visual Studio's editor keys, and the owner's own
# shortcuts from Settings > Environment > Keyboard, which win and can take a default away.

def take_lease(page):
	"""Edit Here when another window (an earlier scenario's) holds the document."""
	if page.is_visible(".ed-bar button:has-text('Edit Here')"):
		page.click(".ed-bar button:has-text('Edit Here')")
		page.wait_for_selector(".ed-bar button:has-text('Edit Here')", state="hidden")


def badge(page, name):
	"""The class, label and hue of a file's glyph in the Solution Explorer."""
	return page.evaluate("""(name) => {
		const row = [...document.querySelectorAll('.tree-row')].find((r) => r.querySelector('.name') && r.querySelector('.name').textContent === name);
		const svg = row && row.querySelector('svg.fg');
		return svg ? [svg.getAttribute('class'), svg.querySelector('text') ? svg.querySelector('text').textContent : null, svg.style.getPropertyValue('--fg-hue')] : null;
	}""", name)


def expand(page, name):
	"""Opens a folder of the Solution Explorer (its glyph says whether it is)."""
	row = ".tree-row:has(.name:text-is('%s'))" % name
	page.wait_for_selector(row + " svg.fg")
	if page.query_selector(row + " svg.fg-folder"):
		page.click(row)
	page.wait_for_selector(row + " svg.fg-folder-open")


@scenario(viewports=("desktop", "phone"))
def file_icons_from_plugins(run):
	"""Files, folders and tabs show the icons the language plugins describe (docs/15 "Icons"): a C source "C", its
	header "h", Markdown gray, a picture's own codicon in its hue, folders tan; the shell knows none of them."""
	page = run.open()
	ws = page.evaluate("ied_shell.wsId")
	made = ["docs/icons-%s.%s" % (run.viewport, ext) for ext in ("c", "h", "png")]
	for path in made:
		run.request("fs.write", {"wsId": ws, "path": path, "text": ""})
	source, header, picture = (path.split("/")[1] for path in made)
	if run.phone:
		page.evaluate("ied_shell.showView('ied.explorer')")
	page.wait_for_selector(".tree-row:has(.name:text-is('README.md')) svg.fg-file")
	check(badge(page, "README.md")[1:] == ["MD", "var(--hue-gray)"], "Markdown: %s" % badge(page, "README.md"))
	expand(page, "docs")
	page.wait_for_selector(".tree-row:has(.name:text-is('%s')) svg.fg-file" % header)
	check(badge(page, source)[1:] == ["C", "var(--hue-blue)"], "a C source: %s" % badge(page, source))
	check(badge(page, header)[1:] == ["h", "var(--hue-violet)"], "its header: %s" % badge(page, header))
	color = page.evaluate("(name) => [...document.querySelectorAll('.tree-row')].find((r) => r.textContent.includes(name)).querySelector('.ci').style.color", picture)
	check(color == "var(--hue-teal)", "a picture: the plugin's codicon in its hue, %s" % color)
	run.shot("explorer")
	page.dblclick(".tree-row:has(.name:text-is('%s'))" % source)
	page.wait_for_selector(".phone-head svg.fg-file" if run.phone else ".tab:has-text('%s') svg.fg-file text:text-is('C')" % source)
	run.shot("tab")
	for path in made:
		run.request("fs.delete", {"wsId": ws, "path": path})


def line_count(page):
	return page.evaluate("monaco.editor.getEditors().find((e) => e.getModel() && e.getModel().uri.toString().endsWith('README.md')).getModel().getLineCount()")


@scenario(viewports=("desktop",))
def editor_keys_and_keyboard_page(run):
	page = run.open()
	page.click(".tree-row:has-text('README.md')")
	run.wait_text(".monaco-editor .view-lines", "workspace" if run.target.kind == "real" else "fixture", timeout=30000)
	page.click(".monaco-editor .view-lines")
	page.evaluate("ied_shell.commands.run('ied.editor.goTo', { path: 'README.md', line: 0, col: 0 })")
	take_lease(page)
	page.click(".monaco-editor .view-lines")
	before = line_count(page)
	page.keyboard.press("Control+d")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before + 1)
	page.keyboard.press("Control+Shift+l")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before)
	page.keyboard.press("Control+z")
	page.keyboard.press("Control+z")
	page.evaluate("ied_shell.commands.run('shell.keyboardShortcuts')")
	page.wait_for_selector(".kb-row")
	page.fill(".kb-search", "duplicate")
	page.click(".kb-row[data-command='ied.editor.duplicate'] .kb-key[data-chord='ctrl+d'] button")
	page.wait_for_selector(".kb-row[data-command='ied.editor.duplicate'] .kb-key.removed")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Add')")
	page.wait_for_selector(".kb-dialog")
	page.keyboard.press("Control+k")
	page.keyboard.press("Control+d")
	run.wait_text(".kb-dialog .kb-recorded", "Ctrl+K, Ctrl+D")
	check("Edit: Format Document" in page.inner_text(".kb-dialog .kb-clash"), "the dialog names the command that has the key")
	run.shot("record")
	page.keyboard.press("Escape")
	page.wait_for_selector(".kb-dialog", state="detached")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Add')")
	page.keyboard.press("Control+Shift+d")
	page.keyboard.press("Enter")
	page.wait_for_selector(".kb-row[data-command='ied.editor.duplicate'] .kb-key.user[data-chord='ctrl+shift+d']")
	run.shot("changed")
	values = run.request("settings.get", {})["values"]["keyboard.bindings"]["value"]
	check(values == [{"command": "ied.editor.duplicate", "key": "ctrl+d", "remove": True}, {"command": "ied.editor.duplicate", "key": "ctrl+shift+d", "when": "activeTabKind == 'doc'"}], "keyboard.bindings: %s" % values)
	page.click(".tab:has-text('README.md')")
	take_lease(page)
	page.click(".monaco-editor .view-lines")
	before = line_count(page)
	page.keyboard.press("Control+Shift+d")
	page.wait_for_function("(n) => monaco.editor.getEditors().some((e) => e.getModel() && e.getModel().getLineCount() === n)", arg=before + 1)
	page.keyboard.press("Control+z")
	page.evaluate("ied_shell.commands.run('shell.keyboardShortcuts')")
	page.fill(".kb-search", "duplicate")
	page.click(".kb-row[data-command='ied.editor.duplicate'] button:has-text('Reset')")
	page.wait_for_function("ied_shell.keybindings.labelFor('ied.editor.duplicate') === 'Ctrl+D'")
