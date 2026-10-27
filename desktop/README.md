# desktop/ - `ied-desktop`, the desktop app

IED's web client in native windows (`docs/27`): WebView2 (the Edge engine Windows ships) shows the same UI a browser
gets, with the keys a browser keeps (Ctrl+W/T/N, F5, Ctrl+R, F12) going to IED. It starts the user's `ied-server` when
none runs, and again if it dies while windows are open. Written in C4: Win32 and WebView2 are bound from their headers
through CBind, and the COM callbacks WebView2 makes are C4 structs.

```bat
ied-desktop                      IED's home
ied-desktop C:\game\src\main.cf:42   a window on that file, at a line
ied C:\game                      the CLI opens folders here when ied-desktop.exe sits beside ied.exe
```

Options: `--control <name>` (another server's pipe), `--data <dir>` (the app's window placement and WebView2 profile;
default `%LOCALAPPDATA%\IED\desktop`), `--debug-port <n>` (the windows' DevTools protocol on loopback, for tests).
`--register` / `--unregister` put this program in the user's Start menu as IED and make `.iedsln` / `.iedfed` files
open in it, or undo both (`shortcut`, `filetypes`);
`nuke install-ied` installs IED into `%LOCALAPPDATA%\Programs\IED`, registers that copy and puts the folder on your
PATH (`docs/21`). In a window, File > Open Folder (Ctrl+Shift+Alt+O, or the button on IED's home) picks a folder and
opens it in that window.

| File | What |
|---|---|
| `main` | the program: the command line, then the app |
| `app` | the windows, the WebView2 environment, reaching the server off the UI thread, the server watch's news |
| `window` (`Window.cf`, `View.cf`) | one window: Win32 (frame, placement, messages) and its WebView2 (settings, events, navigation policy) |
| `com` | COM callback objects written in C4 |
| `watch` | the thread that starts the server again (at most three times a minute) |
| `pages` | where navigations go, the page's `window.iedDesktop` script, the app's own status pages |
| `placement`, `options` | `window.json`; the command line |
| `shortcut` | the Start menu's `IED.lnk` (`IShellLinkW`, `IPersistFile`) |
| `folder` | File > Open Folder's picker (`IFileOpenDialog` in folder mode) |
| `filetypes` | `.iedsln` and `.iedfed` opening in this program (`HKCU\Software\Classes`) |
| `Native.ch`, `native/webview.h` | what CBind binds: `<windows.h>`, COM, DWM, the WebView2 SDK (`vendor/webview2`, from `setup.bat`) |
| `resources/` | the icon (`ied.svg`, rendered to `ied.ico` by `make_icon.py`), version and manifest, compiled into the exe |
| `tests/` | the parts that need no window (`nuke test ied-desktop`) |

The finding and starting of the server, and the control channel, are the `launcher` module's (`launcher/`), shared with
`ied`.
