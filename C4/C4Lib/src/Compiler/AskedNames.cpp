#include "Compiler/AskedNames.h"

namespace Scripting
{
	bool AskedNames::Has(const std::string& name) const
	{
		const bool held = m_Names->count(name) != 0;
		std::lock_guard<std::mutex> asking(m_Lock);
		m_Asked.emplace(name, held);
		return held;
	}

	std::vector<std::pair<std::string, bool>> AskedNames::Asked() const
	{
		std::lock_guard<std::mutex> asking(m_Lock);
		return std::vector<std::pair<std::string, bool>>(m_Asked.begin(), m_Asked.end());
	}
} // namespace Scripting
