// The media tools (docs/13 `media`): image_view shows the model an image of the workspace the way a tool result's
// images do (Images.ch), a model that does not take images is told its path; image_info says what an image file is
// (ImageInfo.ch) - textures included; video_view watches a recording of frames over time (Video.ch).

include "Tool.ch"

namespace Ai
{
	void AddMediaTools(ToolRegistry& registry);
	// The type a request sends an image file as, by its extension; empty for none it can send.
	Core::String ImageMime(Core::StringView path);
}
