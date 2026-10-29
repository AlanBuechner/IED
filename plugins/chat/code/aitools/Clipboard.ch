// The AI's clipboard (docs/13 `clipboard`): named clips a chat copies - lines of a file as the editors have it, of an
// earlier tool result (an fs_read result's line numbers left out) or text it gives - and pastes into any later call by
// writing {{clip:<name>}} in a string of its arguments, so a block it has seen is matched, moved or repeated without
// being typed again; {{!clip:<name>}} writes {{clip:<name>}} itself. The clips live with the chat (chat.json) and show
// in its details panel.

include "ChatService.ch"

namespace Ai
{
	const int64 c_ClipsMost = 20;
	const int64 c_ClipMostBytes = 64 * 1024;
	const int64 c_ClipNameMostBytes = 40;

	// What a copy takes of a text: lines startLine to endLine (0: from the first, to the last), then from the first `from`
	// in them to the end of the first `to` after it (empty: from their start, to their end).
	class ClipRange
	{
		public int64 m_StartLine;
		public int64 m_EndLine;
		public Core::String m_From;
		public Core::String m_To;
	}

	// The group's tools for a turn of `chat`, run on its thread with the lock taken for what they read and change.
	void AddClipboardTools(ToolRegistry& registry, Chats* chats, Chat* chat);
	// The part of `text` that `range` takes, whole lines with their breaks ("\r\n" read as "\n"); a `numbered` text's lines
	// go by the numbers of their "<n>\t" gutters, which are left out, and its "[...]" notes are skipped. False with `error`.
	bool CutClip(Core::StringView text, bool numbered, const ClipRange& range, Core::String& cut, Core::String& error);
	// What clipboard_copy says of a clip it made or added to: its size and source, how to paste it, its first and last
	// lines, whether it ends with a line break, and the clip `dropped` to make room.
	Core::String CopyReport(const Clip* clip, Core::StringView added, Core::StringView source, bool appended, Core::StringView dropped);
	// Whether every line of `text` is "<n>\t..." or a "[...]" note, as an fs_read result's are.
	bool LooksNumbered(Core::StringView text);
	// `text` with each {{clip:<name>}} replaced by that clip's text and {{!clip:<name>}} by {{clip:<name>}}; false with
	// `missing` the first name the clipboard does not have.
	bool PasteClips(Core::StringView text, const Core::Array<Clip*>& clips, Core::String& pasted, Core::String& missing);
	// `args` with the clips pasted into each of its strings, at any depth; `args` itself when nothing was pasted.
	bool PasteArguments(const Serialization::DomNode& args, const Core::Array<Clip*>& clips, Serialization::DomNode& pasted, Core::String& missing);
}
