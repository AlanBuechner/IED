// The media tools (docs/13 `media`): image_view shows the model an image of the workspace the way a tool result's
// images do (Images.ch), a model that does not take images is told its path; image_info says what an image file is
// (ImageInfo.ch) - textures included; video_view watches a recording of frames over time (Video.ch).

include "Tool.ch"

namespace Ai
{
	void AddMediaTools(ToolRegistry& registry);
	// The type a request sends an image file as, by its extension; empty for none it can send.
	Core::String ImageMime(Core::StringView path);
	// The name image_view gives the copy of an image outside the workspace in the chat's media folder: "viewed-", a hash of its full path
	// (two files of one name do not share a copy) and its file name.
	Core::String ViewedCopyName(Core::StringView fullPath);
}
