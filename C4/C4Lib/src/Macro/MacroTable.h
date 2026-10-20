#pragma once
#include <string>
#include <vector>
#include <map>

namespace Scripting
{
	/// A single C/C++-style macro definition (object-like or function-like).
	struct MacroDefinition
	{
		std::string name;
		bool isFunctionLike = false;     // `NAME(` with no space before the `(`; `NAME()` takes no arguments
		std::vector<std::string> params; // the named parameters, then "..." for a variadic macro
		std::string body;                // raw replacement text, expanded at use sites

		bool IsVariadic() const { return !params.empty() && params.back() == "..."; }
	};

	/// An ordered set of macro definitions. Ordered (std::map) so export and
	/// merge are deterministic. This is a file's effective macro state.
	class MacroTable
	{
	public:
		/// Define (or redefine) an object-like macro.
		void Define(const std::string& name, const std::string& body);
		/// Define (or redefine) a function-like macro.
		void DefineFunction(const std::string& name, const std::vector<std::string>& params, const std::string& body);

		/// Remove a macro if present.
		void Undef(const std::string& name);

		bool Has(const std::string& name) const;
		const MacroDefinition* Get(const std::string& name) const;

		/// Apply every definition in other on top of this table (other wins conflicts).
		void Merge(const MacroTable& other);

		/// This table without the definitions it started with: an object-like macro of `starting` with the same body.
		MacroTable Without(const std::map<std::string, std::string>& starting) const;

		bool Empty() const { return m_Defs.empty(); }
		std::size_t Size() const { return m_Defs.size(); }

		/// All definitions in name order (for export/debugging).
		const std::map<std::string, MacroDefinition>& All() const { return m_Defs; }

		/// Human-readable dump of the table for -export-macros.
		std::string ToExportText() const;

	private:
		std::map<std::string, MacroDefinition> m_Defs;
	};
}
