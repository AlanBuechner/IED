#include "Backend/Reflection/ImageDescription.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/xxhash.h>
#include <cstdio>

namespace Scripting
{
	namespace
	{
		template <typename T>
		void Append(std::string& bytes, const T& value)
		{
			bytes.append(reinterpret_cast<const char*>(&value), sizeof(T));
		}

		void AppendText(std::string& bytes, const std::string& text)
		{
			Append(bytes, static_cast<uint64>(text.size()));
			bytes += text;
		}

		// Each value field by field: the padding of a value is not part of it.
		void AppendRow(std::string& bytes, const ImageRow& row)
		{
			Append(bytes, static_cast<uint64>(row.size()));
			for (const ImageValue& value : row)
			{
				Append(bytes, value.kind);
				Append(bytes, value.integer);
				Append(bytes, value.real);
			}
		}

		constexpr size_t kBytesPerValue = sizeof(ImageValue::Kind) + sizeof(uint64) + sizeof(double);
	}

	std::string ImageDescription::Key(const std::string& target) const
	{
		size_t values = m_Image.size();
		for (const ImageTable& table : m_Tables)
			for (const ImageRow& row : table.rows) values += row.size();
		std::string bytes;
		bytes.reserve(values * kBytesPerValue + target.size());
		AppendText(bytes, target);
		Append(bytes, static_cast<uint64>(m_Texts.size()));
		for (const std::string& text : m_Texts) AppendText(bytes, text);
		Append(bytes, static_cast<uint64>(m_Symbols.size()));
		for (const ImageSymbol& symbol : m_Symbols)
		{
			AppendText(bytes, symbol.name);
			Append(bytes, symbol.function);
			Append(bytes, symbol.hidden);
		}
		Append(bytes, static_cast<uint64>(m_Tables.size()));
		for (const ImageTable& table : m_Tables)
		{
			Append(bytes, table.shape);
			Append(bytes, static_cast<uint64>(table.rows.size()));
			for (const ImageRow& row : table.rows) AppendRow(bytes, row);
		}
		AppendRow(bytes, m_Image);
		const llvm::XXH128_hash_t hash = llvm::xxh3_128bits(llvm::arrayRefFromStringRef(bytes));
		char key[33];
		snprintf(key, sizeof(key), "%016llx%016llx", static_cast<unsigned long long>(hash.high64), static_cast<unsigned long long>(hash.low64));
		return key;
	}

	ImageValue ImageDescription::Text(std::string text)
	{
		m_Texts.push_back(std::move(text));
		return { ImageValue::Kind::Text, m_Texts.size() - 1 };
	}

	ImageValue ImageDescription::Symbol(std::string name, bool function, bool hidden)
	{
		m_Symbols.push_back({ std::move(name), function, hidden });
		return { ImageValue::Kind::Symbol, m_Symbols.size() - 1 };
	}

	ImageValue ImageDescription::Table(ImageShape shape, std::vector<ImageRow> rows)
	{
		if (rows.empty()) return Null();
		m_Tables.push_back({ shape, std::move(rows) });
		return { ImageValue::Kind::Table, m_Tables.size() - 1 };
	}
} // namespace Scripting
