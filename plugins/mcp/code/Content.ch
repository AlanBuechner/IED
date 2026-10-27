// What a tools/call answer shows the model (docs/04 "MCP" A call): its content blocks in order - text as it is, an
// image as a picture (a file the caller writes, sent as a tool image), audio and a resource's blob as files it names, an
// embedded text resource as its text, a resource link with its URI - or structuredContent as JSON when there is no
// content; isError makes it an error result.

include "api/Plugin.ch"

namespace Mcp
{
	const int64 c_ResultMostBytes = 262144;

	enum class PartKind { Text, Image, File, Link }

	class ContentPart
	{
		public PartKind m_Kind;
		public Core::String m_Text;    // Text: the text; File: a resource's URI or "audio"; Link: the URI
		public Core::String m_Mime;
		public Core::String m_Data;    // Image, File: base64
		public Core::String m_Name;    // Link: its name and description
	}

	class CallContent
	{
		public Core::Array<ContentPart*> m_Parts;
		public bool m_IsError;
	}

	CallContent ReadCallResult(const Serialization::DomNode& result);
	// A resources/read answer's contents: a text one as "[resource <uri>]" and its text, a blob as a file to save (an
	// image too: a mention carries text only).
	CallContent ReadResourceResult(const Serialization::DomNode& result);
	// The text the model is told: the parts in order, a File part as "[<what> saved as <path> (<mime>)]" with paths[i]
	// (the part's index; empty: "[<what> (<mime>) could not be saved]"); images are left to the tool's images. Cut at
	// kResultMostBytes with a note.
	Core::String CallText(const CallContent& content, const Core::Array<Core::String>& paths);
	// A file name's extension for a MIME type: png, jpg, gif, webp, wav, mp3, ogg, txt, json, bin.
	const char* ExtensionOf(Core::StringView mime);
	// Whether the chat can show an image of `mime` to a model (PNG, JPEG, GIF, WebP).
	bool ShownImage(Core::StringView mime);
}
