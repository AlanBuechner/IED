#include "Protocol/TextDocumentStore.h"

namespace C4LSP::Protocol
{
	void TextDocumentStore::Open(const std::string& uri, const std::string& text, int version)
	{
		Document doc;
		doc.text = text;
		doc.version = version;
		m_Documents[uri] = std::move(doc);
	}

	void TextDocumentStore::Change(const std::string& uri, const std::vector<TextContentChangeEvent>& changes, bool fullSync)
	{
		Document& doc = m_Documents[uri];
		if (fullSync)
		{
			// The single change carries the entire new text; the range is ignored.
			doc.text = changes.empty() ? doc.text : changes.front().text;
		}
		else
		{
			TextDocumentSync sync;
			sync.SetFullText(doc.text);
			sync.ApplyChanges(changes);
			doc.text = sync.GetText();
		}
		++doc.version;
	}

	void TextDocumentStore::Close(const std::string& uri)
	{
		m_Documents.erase(uri);
	}

	const std::string* TextDocumentStore::GetText(const std::string& uri) const
	{
		auto it = m_Documents.find(uri);
		return it == m_Documents.end() ? nullptr : &it->second.text;
	}

	void TextDocumentStore::ForEachDocument(const std::function<void(const std::string&, const std::string&)>& apply) const
	{
		for (const auto& [uri, doc] : m_Documents)
			apply(uri, doc.text);
	}
} // namespace C4LSP::Protocol
