// video_view (docs/13 `media`): how a recording changes over time, shown to the model - a contact sheet of its frames
// and a heat map of where its brightness flickers or pulses from frame to frame, with the numbers (VideoFrames.ch). A
// recording is a folder of frames as PNG files, in the order of their names (what a program saves frame by frame:
// Claymore's --record). Its images are written into .ied/local/video/.

include "Tool.ch"

namespace Ai
{
	const int64 c_VideoMostFrames = 600;
	const int64 c_VideoSheetFrames = 8;     // the contact sheet's frames by default
	const int64 c_VideoMostSheetFrames = 16;
	const int32 c_VideoSheetColumns = 4;
	const int32 c_VideoSheetWidth = 1600;   // the contact sheet's width at most
	const int32 c_VideoHeatWidth = 1280;    // the heat map's
	const int64 c_VideoCellsReported = 6;
	const int64 c_VideoSeriesShown = 60;    // values of a series the text gives at most
	const float c_VideoSteadyChange = 2.0f; // brightness change (of 255) that shows as full heat at the least

	void AddVideoTool(ToolRegistry& registry);
	// The PNG files of `folder` (an absolute path), by name; empty when it has none or is no folder.
	Core::Array<Core::String> FrameFiles(Core::StringView folder);
	// `count` indices spread evenly over [0, total), the first and the last among them.
	Core::Array<int64> EvenlySpread(int64 total, int64 count);
	// A series as text, `values` of it at most (evenly picked), each with one decimal.
	Core::String SeriesText(const Core::Array<double>& series, int64 values);
}
