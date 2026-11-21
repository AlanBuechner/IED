#include "Model/NotIncluded.h"
#include "Model/ProgramModel.h"

namespace Scripting
{
	namespace
	{
		const char* const c_VisibleHere = " is visible here: it is declared in ";
		const char* const c_ThisFile = ", which this file does not include";

		void ReplaceEvery(Core::String& text, const Core::String& from, const Core::String& to)
		{
			for (size_t at = text.find(from); at != Core::String::npos; at = text.find(from, at + to.size()))
				text.replace(at, from.size(), to);
		}

		bool IsHeader(const Core::String& file)
		{
			const Core::String extension = ".ch";
			return file.size() >= extension.size() && file.compare(file.size() - extension.size(), extension.size(), extension) == 0;
		}

		// The file whose include would make the symbol visible: a function's is where it is declared, not where its
		// body is.
		uint32 DeclaringUnit(const ProgramModel& model, const SymbolInfo& symbol)
		{
			if (symbol.kind != SymbolKind::Function || symbol.functions.Empty()) return symbol.definingUnit;
			std::vector<uint32> units;
			for (const FunctionRecord* function : symbol.functions)
				if (function) units.insert(units.end(), function->declUnits.begin(), function->declUnits.end());
			return units.empty() ? symbol.definingUnit : IncludableUnit(model, units);
		}

		// The declaration of `name` some file has, as a lookup from `scope` that sees every file finds it.
		const SymbolInfo* Unseen(const ProgramModel& model, const Core::String& name, SymbolKind kind, const Scope* scope)
		{
			const uint32 everyFile = 0;
			auto unseen = model.QuerySymbol(name, everyFile, scope);
			return unseen.Ok() && unseen.value->kind == kind ? unseen.value : nullptr;
		}
	}

	uint32 IncludableUnit(const ProgramModel& model, const std::vector<uint32>& units)
	{
		for (uint32 unit : units)
			if (IsHeader(model.SourceName(unit))) return unit;
		return units.empty() ? 0 : units.front();
	}

	uint32 UnseenUnit(const ProgramModel& model, const Core::String& name, SymbolKind kind, const Scope* scope)
	{
		const SymbolInfo* unseen = Unseen(model, name, kind, scope);
		if (!unseen) return 0;
		const bool filePrivate = unseen->isStatic && unseen->declaringScope && unseen->declaringScope->HoldsFileStatics();
		return filePrivate ? 0 : DeclaringUnit(model, *unseen);
	}

	Core::String NotIncluded(const ProgramModel& model, const Core::String& name, SymbolKind kind, const char* what, const Scope* scope)
	{
		// Another project's, which this one does not depend on: no include reaches it (15-projects.md rule 4).
		if (const SymbolInfo* unseen = Unseen(model, name, kind, scope))
			if (Core::String why = model.OutOfProjectReach(*unseen, name); !why.empty()) return why;
		return DeclaredElsewhere(model, name, what, UnseenUnit(model, name, kind, scope));
	}

	Core::String DeclaredElsewhere(const ProgramModel& model, const Core::String& name, const char* what, uint32 unit)
	{
		const Core::String file = unit == 0 ? Core::String() : model.SourceName(unit);
		if (file.empty()) return "";
		const Core::String start = "no " + Core::String(what) + " named " + name + c_VisibleHere + file;
		if (!IsHeader(file)) return start + ", a source file no file can include: declare it in a header";
		return start + c_ThisFile;
	}

	Core::String NotIncludedIn(const ProgramModel& model, uint32 unit, Core::String message)
	{
		const Core::String file = unit == 0 ? Core::String() : model.SourceName(unit);
		if (file.empty()) return message;
		ReplaceEvery(message, c_VisibleHere, " is visible in " + file + ": it is declared in ");
		ReplaceEvery(message, c_ThisFile, ", which " + file + " does not include");
		return message;
	}

	Core::String AnotherElsewhere(const ProgramModel& model, const Core::String& name, uint32 unit)
	{
		const Core::String file = unit == 0 ? Core::String() : model.SourceName(unit);
		if (file.empty()) return "";
		return " (another '" + name + "' is declared in " + file + c_ThisFile + ")";
	}
} // namespace Scripting
