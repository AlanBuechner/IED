// Where windows open (docs/27 "The window"): the placement of the last main window closed, kept in <data>/window.json,
// for the next one; a further window cascades from the newest. A window docked to activities of its own (docs/15 "Windows")
// keeps its own, <data>/windows/<its id>.json, and opens at the pointer when it was dropped there.

include "bedrock.ch"

namespace Desktop
{
	const int32 c_CascadeStep = 32;
	const int32 c_DefaultWidth = 1440;
	const int32 c_DefaultHeight = 900;
	const int32 c_MinWidth = 480;
	const int32 c_MinHeight = 320;

	// A window's outer rectangle in screen pixels, and whether it was maximized.
	struct Placement
	{
		public bool m_Known;   // false: none kept, let the system choose
		public int32 m_X;
		public int32 m_Y;
		public int32 m_Width;
		public int32 m_Height;
		public bool m_Maximized;
	}

	// window.json's text to a placement; unknown when it is not one (or too small to be a real window).
	Placement ParsePlacement(Core::StringView json);
	Core::String PlacementJson(const Placement& placement);
	// `from` moved down and right by kCascadeStep.
	Placement Cascaded(const Placement& from);
	// `from`, as large as it was when not maximized, with its caption under the point (x, y) of the screen.
	Placement GrabbedAt(const Placement& from, int32 x, int32 y);

	Placement LoadPlacement(const FS::Path& folder);
	bool SavePlacement(const FS::Path& folder, const Placement& placement);
	// The same, of the window docked as `id` (Relaunch.ch `DockWindowOf`).
	Placement LoadDockedPlacement(const FS::Path& folder, Core::StringView id);
	bool SaveDockedPlacement(const FS::Path& folder, Core::StringView id, const Placement& placement);
}
