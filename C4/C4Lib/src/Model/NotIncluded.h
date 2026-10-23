#pragma once
#include "Core/Core.h"
#include "Model/SymbolTable.h"

namespace Scripting
{
	class ProgramModel;

	/// `no variable named kLimit is visible here: it is declared in a.cf, which this file does not include`, when a
	/// file-scope name of `kind` exists in a file the asking one does not include (a file-private static excepted:
	/// including its file would not make it visible); empty otherwise. `what` names the kind in the message.
	Core::String NotIncluded(const ProgramModel& model, const Core::String& name, SymbolKind kind, const char* what);
} // namespace Scripting
