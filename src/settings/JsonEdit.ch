// JSONC editing that keeps a file's comments and layout (docs/07 "Writing"): the top-level members of an object with
// where each sits in the text, and a member's value set, added or removed by splicing the text around it.
//     Core::String edited;
//     if (Ied::SetMember(text, Core::StringView("editor.tabSize"), Core::StringView("4"), edited, error)) ...

include "bedrock.ch"

namespace Ied
{
	class MemberSpan
	{
		public Core::String m_Key;
		public int64 m_KeyStart;     // the key's opening quote
		public int64 m_ValueStart;
		public int64 m_ValueEnd;     // just past the value
	}

	class TopLevel
	{
		public int64 m_Open;         // the object's '{'
		public int64 m_Close;        // its '}'
		public Core::Array<MemberSpan*> m_Members;
	}

	// The top-level members of the JSONC object `text` holds; false with `error` for text that is not one object.
	bool ScanMembers(Core::StringView text, TopLevel& top, Core::String& error);
	// `text` with member `key` set to `valueJson`: replaced in place, or added after the last member (a new object when
	// `text` is empty or blank).
	bool SetMember(Core::StringView text, Core::StringView key, Core::StringView valueJson, Core::String& edited, Core::String& error);
	// `text` without member `key` (its line, and the comma that went with it); true and `text` unchanged when it is not
	// there.
	bool RemoveMember(Core::StringView text, Core::StringView key, Core::String& edited, Core::String& error);
	// The 1-based line of a byte offset.
	int32 LineOf(Core::StringView text, int64 offset);
}
