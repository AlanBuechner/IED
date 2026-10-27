// Messages between IED's clients and a language server, rewritten (docs/24 "Positions"): every position's character
// moved to the other side's encoding, every URI to the other side's form. A position is any object with numeric
// `line` and `character`; it belongs to the document the nearest `uri` / `targetUri` / `textDocument.uri` (or a key of
// a WorkspaceEdit's `changes`) names, else the message's own document - as `originSelectionRange` always does.
// Folding ranges' `startCharacter` / `endCharacter` and semantic tokens' `data` are converted too.

include "Positions.ch"

namespace Ied
{
	class Rewrite
	{
		public PositionEncoding m_From;
		public PositionEncoding m_To;
		// The lines of the document a URI (as the message names it) is, or null when its text is not known.
		public func<const LineIndex*(Core::StringView)> m_Lines;
		// A URI as the other side names it.
		public func<Core::String(Core::StringView)> m_MapUri;
		// The document positions without a URI of their own belong to (as the message names it).
		public Core::String m_Uri;
	}

	// `node` rewritten, as JSON text.
	Core::String RewriteJson(const Serialization::DomNode& node, const Rewrite& rewrite);
	// Semantic tokens' `data` (5 numbers per token, positions relative to the token before) rewritten for `lines`.
	Core::Array<int64> RewriteSemanticTokens(const Core::Array<int64>& data, const LineIndex& lines, PositionEncoding from, PositionEncoding to);
}
