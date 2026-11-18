// statusbar.test.js - the status bar's devices (shell/statusbar.js, docs/11 "Presence"): one per device however many
// tabs or windows it has, each named, this device first and marked; and the name the desktop app gives itself
// (ws/identity.js).

import { test, assertEqual, assertTrue } from "./harness.js";
import { connectedDevices, devicesTitle } from "../src/shell/statusbar.js";
import { resolveIdentity, DESKTOP_APP_NAME } from "../src/ws/identity.js";

const DESKTOP = { deviceId: "d-me", deviceClass: "desktop", deviceName: DESKTOP_APP_NAME, clientId: "c-1" };

test("statusbar: one device per device id, its windows counted, this device first", () => {
	const devices = connectedDevices([
		{ deviceId: "d-pane", deviceClass: "tablet", deviceName: "Chrome on Windows", clientId: "c-2" },
		DESKTOP,
		{ deviceId: "d-me", deviceClass: "desktop", deviceName: DESKTOP_APP_NAME, clientId: "c-3" },
	], "d-me");
	assertEqual(devices.length, 2);
	assertEqual(devices[0].deviceId, "d-me", "this device leads");
	assertTrue(devices[0].own);
	assertEqual(devices[0].windows, 2);
	assertEqual(devices[1].name, "Chrome on Windows");
	assertTrue(!devices[1].own);
});

test("statusbar: the tooltip names each device, with its class and windows", () => {
	const title = devicesTitle(connectedDevices([
		DESKTOP,
		{ deviceId: "d-me", deviceClass: "phone", clientId: "c-4" },
		{ deviceId: "d-pane", deviceClass: "tablet", deviceName: "Chrome on Windows", clientId: "c-2" },
	], "d-me"));
	assertEqual(title, "2 devices connected\nIED desktop app - this device (desktop, phone, 2 windows)\nChrome on Windows (tablet)");
});

test("statusbar: a device that gave no name and has no sign-in's is still listed", () => {
	const devices = connectedDevices([{ deviceId: "d-x", deviceClass: "phone", clientId: "c-9" }], "d-me");
	assertEqual(devicesTitle(devices), "1 device connected\nUnnamed device (phone)");
	assertEqual(devicesTitle(connectedDevices([], "d-me")), "0 devices connected");
});

function fakeWindow(extra) {
	const store = () => {
		const values = new Map();
		return { getItem: (k) => (values.has(k) ? values.get(k) : null), setItem: (k, v) => values.set(k, v) };
	};
	return Object.assign({ localStorage: store(), sessionStorage: store(), innerWidth: 1400 }, extra);
}

test("statusbar: the desktop app names itself to the server; a browser leaves it to its sign-in", async () => {
	const app = await resolveIdentity(fakeWindow({ iedDesktop: {} }));
	assertEqual(app.deviceName, DESKTOP_APP_NAME);
	const browser = await resolveIdentity(fakeWindow({}));
	assertEqual(browser.deviceName, undefined);
	assertEqual(browser.deviceClass, "desktop");
});
