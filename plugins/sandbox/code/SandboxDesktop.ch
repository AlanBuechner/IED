// A sandbox's desktop (docs/34 "Desktops"): a virtual screen (Xvfb, display :99) with a window manager (Openbox) in the
// container, which the AI and the owner see and drive - its frames streamed to the Sandbox activity, input sent with
// xdotool, programs launched on it (under gdb when debugging), screenshots for the AI. Here: the shell the container
// runs for each of those, and what their output means. Text in, text out; SandboxService runs them.

include "bedrock.ch"

namespace Sandbox
{
	class DesktopWindow
	{
		public Core::String m_Id;
		public Core::String m_Name;
		public int64 m_X;
		public int64 m_Y;
		public int64 m_Width;
		public int64 m_Height;
		public int64 m_Pid;
	}

	class DesktopState
	{
		public Core::Array<DesktopWindow> m_Windows;
		public Core::String m_Focused;     // the focused window's id
		public int64 m_MouseX;
		public int64 m_MouseY;
	}

	// The frames a desktop's capture loop sends, and who waits for them (the views' long polls).
	class DesktopStream
	{
		public Threading::Mutex m_Lock;
		public Threading::ConditionVariable m_Arrived;
		public Core::String m_Frame;       // the newest JPEG's bytes
		public int64 m_Seq;
		public int64 m_PullMs;             // the last view's pull; the loop stops when none came for c_StreamIdleMs
		public bool m_Running;
		public bool m_Stop;
		public Core::String m_Problem;
	}

	// Starts the virtual screen (width x height) and the window manager unless they run, waits for the screen, and prints
	// its size ("1280 800"); exit 1 with Xvfb's log when it does not start.
	Core::String DesktopStartScript(int64 width, int64 height);
	// Prints each visible window as "w<TAB>id<TAB>x<TAB>y<TAB>width<TAB>height<TAB>pid<TAB>name", then "f<TAB>focused id"
	// and "m<TAB>x<TAB>y" (the pointer).
	Core::String WindowsScript();
	DesktopState ParseWindows(Core::StringView text);
	// The windows as lines for the AI: "- "Packer" at (101, 120) 484x316, pid 42, focused".
	Core::String WindowsText(const DesktopState& state);

	// The capture loop: every changed screen as "F <bytes>\n" and the JPEG (quality 1-100), its shell's pid in `pidFile`.
	Core::String FrameLoopScript(int64 quality, Core::StringView pidFile);
	// Takes the frames out of `pending` (the loop's output so far): the newest whole one into `frame`; true when one came.
	bool TakeFrames(Core::String& pending, Core::String& frame);

	// The shell script and its arguments for `actions` (sandbox_input's: [{action, x, y, to_x, to_y, text, keys, amount,
	// window, ms}]) on a width x height screen, with `summary` ("click (412, 310); type \"hi\"") and where the pointer
	// ends (`lastX`, `lastY`, -1 when no action placed it). False with `problem` for an action that cannot be done.
	bool InputScript(const Serialization::DomNode& actions, int64 width, int64 height, Core::String& script, Core::Array<Core::String>& args, Core::String& summary, int64& lastX, int64& lastY, Core::String& problem);
	// The script that saves the screen (or the window named like `window`, or `region` x, y, w, h) as PNG to stdout.
	Core::String ScreenshotScript(Core::StringView window, int64 x, int64 y, int64 width, int64 height);

	// `text` as one shell word: 'text', a ' inside as '\''.
	Core::String ShellQuote(Core::StringView text);
	// `command` run under gdb, which prints the signal and every thread's backtrace when it crashes.
	Core::String DebugCommand(Core::StringView command);
	// What an exit code says of how a program ended: "" for 0..127, else "SIGSEGV (a segmentation fault)" and the like.
	Core::String SignalText(int64 exitCode);

	const char* c_Display = ":99";
	const int64 c_StreamIdleMs = 10000;
	const int64 c_FrameWaitMs = 2000;
	const int64 c_ControlLeaseMs = 90000;
}
