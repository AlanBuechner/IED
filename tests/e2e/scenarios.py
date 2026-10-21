"""The shell's interactions, end to end (docs/15, docs/19): each scenario drives the page the way a person does and
screenshots the states that matter. Both servers run every scenario unless it names one."""

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
	run.wait_text(".doc-line .tx", "workspace" if run.target.kind == "real" else "fixture")
	check(page.is_visible(".chip:has-text('Read-only')"), "the document says it is read-only")
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
	run.wait_text(".doc-line .tx", "workspace" if run.target.kind == "real" else "fixture")
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
	page.wait_for_selector(".phone-pane .doc-line")
	check(page.is_visible(".phone-head button[aria-label='Back']"), "a back button")
	check("README.md" in run.texts(".phone-head .t b"), "the pane title")
	run.shot("document")
	page.click(".phone-head button[aria-label='Back']")
	page.wait_for_selector(".phone-pane .tree-row")
	page.click(".phone-nav .pn[aria-label='More']")
	page.wait_for_selector(".menu.sheet")
	check("Keyboard Shortcuts" in run.texts(".menu.sheet .menu-item .label"), "More lists the rest")
	run.shot("more")
