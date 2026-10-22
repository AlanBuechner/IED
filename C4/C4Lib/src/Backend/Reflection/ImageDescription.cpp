#include "Backend/Reflection/ImageDescription.h"

namespace Scripting
{
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
