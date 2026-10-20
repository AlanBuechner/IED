#pragma once
#include "Protocol/TextDocumentSync.h"
#include <map>
#include <string>
#include <functional>

namespace C4LSP::Protocol
{
	/// Tracks the current contents of every open document. The compiler parses whole files, so
	/// incremental edits are folded into each buffer and full text is handed out on recompile.
	class TextDocumentStore
	{
	public:
		void Open(const std::string& uri, const std::string& text, int version);
		/// Apply a `didChange`: replace (full sync) or splice (incremental) the buffer.
		void Change(const std::string& uri, const std::vector<TextContentChangeEvent>& changes, bool fullSync);
		void Close(const std::string& uri);

		bool IsOpen(const std::string& uri) const { return m_Documents.find(uri) != m_Documents.end(); }
		const std::string* GetText(const std::string& uri) const;
		int GetVersion(const std::string& uri) const { auto it = m_Documents.find(uri); return it == m_Documents.end() ? 0 : it->second.version; }

		/// Call `apply(uri, text)` for each open document (used to push overrides into a recompile).
		void ForEachDocument(const std::function<void(const std::string&, const std::string&)>& apply) const;

	private:
		struct Document { std::string text; int version = 0; };
		std::map<std::string, Document> m_Documents;
	};
} // namespace C4LSP::Protocol
