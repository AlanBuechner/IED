#pragma once
#include "Protocol/LspTypes.h"
#include "Protocol/PositionCodec.h"
#include <optional>
#include <vector>

namespace C4LSP::Protocol
{
	/// One change from a `textDocument/didChange` notification: with a range, a splice of `text` there; without one,
	/// `text` is the whole new document.
	struct TextContentChangeEvent
	{
		std::optional<Range> range;
		std::string text;
	};

	/// Applies LSP document edits to produce the current full text of an open document;
	/// incremental edits are folded into the buffer rather than tracked per-edit.
	class TextDocumentSync
	{
	public:
		void SetFullText(const std::string& text) { m_Text = text; }
		const std::string& GetText() const { return m_Text; }

		/// Apply changes in order, each against the text the ones before it left; a range's columns count `encoding`'s
		/// units (the one the client and the server agreed on).
		void ApplyChanges(const std::vector<TextContentChangeEvent>& changes, PositionEncoding encoding = PositionEncoding::Utf8);

	private:
		/// Splice `text` into `buffer` at `range` (none: replace it all), converting 0-based LSP positions to byte offsets.
		static void ApplySingleChange(std::string& buffer, const TextContentChangeEvent& change, PositionEncoding encoding);

		/// Convert a 0-based (line, character) position to a byte offset in `buffer`.
		static size_t PositionToOffset(const std::string& buffer, const Position& pos, PositionEncoding encoding);

		std::string m_Text;
	};
} // namespace C4LSP::Protocol
