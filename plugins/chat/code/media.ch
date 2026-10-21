// The media tools (docs/13 `media`): image_view shows the model an image of the workspace the way a tool result's
// images do (images.ch), a model that does not take images is told its path; image_info says what an image file is
// (imageinfo.ch) - textures included. The group's other tools are to come.

include "tool.ch"

namespace Ai
{
	void AddMediaTools(ToolRegistry& registry);
	// The type a request sends an image file as, by its extension; empty for none it can send.
	Core::String ImageMime(Core::StringView path);
}
