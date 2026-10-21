// Images in tool results (docs/13 "Images"): a tool's result may name image files in its workspace. A request shows a
// model that sees images (its profile's `vision`) the images of a step's results in a user message after them - in the
// OpenAI format a tool message carries text only - and tells one that does not their paths.

include "chat.ch"

namespace Ai
{
	const int64 kToolImageMaxBytes = 4 * 1024 * 1024;
	const int64 kToolImageTokens = 1000;   // what an image is assumed to take of the context

	// The images a tool named that a request can send: a type the format takes, a file inside the workspace, at most
	// kToolImageMaxBytes. Why each other one was left out goes to `notes`.
	Core::Array<IED::ToolImage> AcceptedImages(const IED::Workspace& ws, const Core::Array<IED::ToolImage>& images, Core::Array<Core::String>& notes);
	// What a model that is not sent images is told of them; empty for none.
	Core::String ImagesAsText(const Core::Array<IED::ToolImage>& images);
	// The user message showing the images `results` name, read from the workspace at `root`. The bytes of base64 it
	// holds, so an estimate can count the images as images rather than as text.
	int64 WriteImagesMessage(IED::JsonWriter& json, Core::StringView root, const Core::Array<ModelMessage*>& results);
	// The images the messages name.
	int64 CountImages(const Core::Array<ModelMessage*>& messages);
}
