// URIs between IED and a language server (docs/24): the server gets `file:` URIs of absolute paths; clients get the
// workspace's files as `ied://<wsId>/<relative path>` (the URIs their editor models use) and other files as `file:`.

include "bedrock.ch"

namespace Ied
{
	// "file:///C:/Work/My%20Game/a.c" of "C:\Work\My Game\a.c": '/' separators, the path's bytes percent-encoded but
	// for letters, digits and "-._~/:" (UTF-8 is encoded byte by byte).
	Core::String FileUriOf(Core::StringView path);
	// The path a `file:` URI names, with the system's separators ("C:\..." for a drive letter); false for another
	// scheme or a bad escape.
	bool PathOfFileUri(Core::StringView uri, Core::String& path);
	// "ied://w1/src/main.c".
	Core::String IedUriOf(Core::StringView wsId, Core::StringView relativePath);
	// The workspace id and relative path of an "ied://" URI; false for anything else.
	bool ParseIedUri(Core::StringView uri, Core::String& wsId, Core::String& relativePath);
	// The text with every "%XX" decoded; false for a bad one.
	bool PercentDecodeUri(Core::StringView text, Core::String& decoded);
}
