// Where windows open (docs/27 "The window"): the placement of the last window closed, kept in <data>/window.json,
// for the next one; a further window cascades from the newest.

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

	Placement LoadPlacement(const FS::Path& folder);
	bool SavePlacement(const FS::Path& folder, const Placement& placement);
}
