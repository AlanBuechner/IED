#include "Model/NotIncluded.h"
#include "Model/ProgramModel.h"

namespace Scripting
{
	Core::String NotIncluded(const ProgramModel& model, const Core::String& name, SymbolKind kind, const char* what)
	{
		if (name.find("::") != Core::String::npos) return "";
		const SymbolInfo* unseen = model.Symbols()->GetRoot()->Find(name);
		if (!unseen || unseen->kind != kind || unseen->isStatic || unseen->definingUnit == 0) return "";
		const Core::String file = model.SourceName(unseen->definingUnit);
		if (file.empty()) return "";
		return "no " + Core::String(what) + " named " + name + " is visible here: it is declared in " + file + ", which this file does not include";
	}
} // namespace Scripting
