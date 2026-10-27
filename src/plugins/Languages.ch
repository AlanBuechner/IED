// Which language a file is in: the `languages` contributions of the loaded plugins decide (docs/02), never the host. A
// language naming the file in `fileNames` wins over one naming its extension; among either, the plugin loaded last wins
// (a project-local plugin over a global one). Extensions match case-insensitively.

include "bedrock.ch"

namespace Ied
{
	const char* c_PlainText = "plaintext";

	// The language id for `path` (a workspace-relative path) among `contributes` nodes in load order, or kPlainText.
	Core::String LanguageFor(const Core::Array<Serialization::DomNode>& contributes, Core::StringView path);
}
