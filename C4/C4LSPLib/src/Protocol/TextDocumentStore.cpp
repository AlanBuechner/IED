#include "Protocol/TextDocumentStore.h"

namespace C4LSP::Protocol
{
	void TextDocumentStore::Open(const std::string& uri, const std::string& text, int version)
	{
		Document doc;
		doc.text = text;
		doc.version = version;
		doc.hash = std::hash<std::string>{}(doc.text);
		m_Documents[uri] = std::move(doc);
	}

	void TextDocumentStore::Change(const std::string& uri, const std::vector<TextContentChangeEvent>& changes, PositionEncoding encoding)
	{
		Document& doc = m_Documents[uri];
		TextDocumentSync sync;
		sync.SetFullText(doc.text);
		sync.ApplyChanges(changes, encoding);
		doc.text = sync.GetText();
		doc.hash = std::hash<std::string>{}(doc.text);
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

	std::map<std::string, size_t> TextDocumentStore::TextHashes() const
	{
		std::map<std::string, size_t> hashes;
		for (const auto& [uri, doc] : m_Documents)
			hashes.emplace(uri, doc.hash);
		return hashes;
	}
} // namespace C4LSP::Protocol
