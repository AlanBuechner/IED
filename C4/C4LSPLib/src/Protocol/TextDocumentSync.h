#pragma once
#include "Protocol/LspTypes.h"
#include <vector>

namespace C4LSP::Protocol
{
	/// One change from a `textDocument/didChange` notification. For full sync the range is
	/// ignored and `text` is the whole new document; for incremental sync it is a splice.
	struct TextContentChangeEvent
	{
		Range range;
		std::string text;
	};

	/// Applies LSP document edits to produce the current full text of an open document;
	/// incremental edits are folded into the buffer rather than tracked per-edit.
	class TextDocumentSync
	{
	public:
		void SetFullText(const std::string& text) { m_Text = text; }
		const std::string& GetText() const { return m_Text; }

		/// Apply one or more changes (full sync sends a single change with the full text).
		void ApplyChanges(const std::vector<TextContentChangeEvent>& changes);

	private:
		/// Splice `text` into `buffer` at `range`, converting 0-based LSP positions to byte offsets.
		static void ApplySingleChange(std::string& buffer, const TextContentChangeEvent& change);

		/// Convert a 0-based (line, character) position to a byte offset in `buffer`.
		static size_t PositionToOffset(const std::string& buffer, const Position& pos);

		std::string m_Text;
	};
} // namespace C4LSP::Protocol
