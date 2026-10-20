#include "MacroTable.h"
#include <sstream>

namespace Scripting
{
	void MacroTable::Define(const std::string& name, const std::string& body)
	{
		MacroDefinition def;
		def.name = name;
		def.body = body;
		m_Defs[name] = std::move(def);
	}

	void MacroTable::DefineFunction(const std::string& name, const std::vector<std::string>& params, const std::string& body)
	{
		MacroDefinition def;
		def.name = name;
		def.isFunctionLike = true;
		def.params = params;
		def.body = body;
		m_Defs[name] = std::move(def);
	}

	void MacroTable::Undef(const std::string& name)
	{
		m_Defs.erase(name);
	}

	bool MacroTable::Has(const std::string& name) const
	{
		return m_Defs.find(name) != m_Defs.end();
	}

	const MacroDefinition* MacroTable::Get(const std::string& name) const
	{
		auto it = m_Defs.find(name);
		return it == m_Defs.end() ? nullptr : &it->second;
	}

	void MacroTable::Merge(const MacroTable& other)
	{
		for (const auto& [name, def] : other.m_Defs)
			m_Defs[name] = def;
	}

	MacroTable MacroTable::Without(const std::map<std::string, std::string>& starting) const
	{
		MacroTable rest;
		for (const auto& [name, def] : m_Defs)
		{
			auto start = starting.find(name);
			if (start == starting.end() || def.isFunctionLike || start->second != def.body) rest.m_Defs[name] = def;
		}
		return rest;
	}

	std::string MacroTable::ToExportText() const
	{
		std::ostringstream out;
		out << "Macro table (" << m_Defs.size() << " definition(s))\n";
		for (const auto& [name, def] : m_Defs)
		{
			if (def.isFunctionLike)
			{
				out << "#define " << name << "(";
				for (std::size_t i = 0; i < def.params.size(); ++i)
					out << (i ? ", " : "") << def.params[i];
				out << ") " << def.body << "\n";
			}
			else
			{
				out << "#define " << name << " " << def.body << "\n";
			}
		}
		return out.str();
	}
}
