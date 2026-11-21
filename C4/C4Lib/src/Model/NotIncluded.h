#pragma once
#include "Core/Core.h"
#include "Model/SymbolTable.h"
#include <vector>

namespace Scripting
{
	class ProgramModel;

	// What to say of a name a file cannot see because the file that declares it is not among its includes (docs/language/01
	// section 1.4). The file is named as an include of it is written.

	/// The unit that declares `name` as a `kind` - found from `scope` outward as the asking file would look for it, a
	/// qualified name by its path - whatever the asking file includes; 0 when nothing does, or when it is a file-private
	/// static (including its file would not make it visible).
	uint32 UnseenUnit(const ProgramModel& model, const Core::String& name, SymbolKind kind, const Scope* scope);

	/// Of the files that declare one name, the one to include: the first header (a source file cannot be included),
	/// else the first; 0 for none.
	uint32 IncludableUnit(const ProgramModel& model, const std::vector<uint32>& units);

	/// `no variable named kLimit is visible here: it is declared in limits.ch, which this file does not include`, when
	/// UnseenUnit finds such a declaration; empty otherwise. `what` names the kind in the message. A name only a
	/// source file declares ends `..., a source file no file can include: declare it in a header`. A declaration of a
	/// project the asking one does not depend on says that instead: no include reaches it.
	Core::String NotIncluded(const ProgramModel& model, const Core::String& name, SymbolKind kind, const char* what, const Scope* scope = nullptr);

	/// The same sentence for a declaration known to be in `unit`; empty when the unit has no file (a builtin).
	Core::String DeclaredElsewhere(const ProgramModel& model, const Core::String& name, const char* what, uint32 unit);

	/// ` (another 'Round' is declared in Math/scalar.ch, which this file does not include)`: said after an error about
	/// the declarations of the name the file does see, since the one meant may be the unseen one. Empty for unit 0.
	Core::String AnotherElsewhere(const ProgramModel& model, const Core::String& name, uint32 unit);

	/// `message` for a failure found in `unit` but reported in another file - a template instance's, reported where the
	/// instance is written: its sentences name the template's file (`is visible in box.ch`, `which box.ch does not
	/// include`) instead of "here" and "this file".
	Core::String NotIncludedIn(const ProgramModel& model, uint32 unit, Core::String message);
} // namespace Scripting
