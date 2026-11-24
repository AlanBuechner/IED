#pragma once
#include "Backend/ProgramBackend.h"
#include "Compiler/CompilerInstance.h"
#include <string>
#include <vector>

namespace C4Compiler
{
	/// The parses an edit replaced, kept while the model points into them: its types, symbols and templates keep the
	/// nodes they were made from.
	using RetiredParses = std::vector<PriorParse>;

	/// A build server's edit (docs/compiler/07 stages 2a and 2b), on the compiler and backend of the build before (`live`,
	/// which succeeded): `edited` compiled the sources as they are now (CompileAll) with `live`'s parses, so a file it
	/// parsed again is one whose text, macro layout or bindings changed. When the files and projects are the same and every
	/// file parsed again changed only bodies their own functions read and the initializers of its globals (FindFileEdit),
	/// `live` takes those files' parses, its model's functions and globals point at the new definitions, the globals whose
	/// values changed are folded again (and those folded from them), and the changed bodies and the bodies that read a
	/// changed value are lowered again in place (ProgramBackend::Relower): `live` is then the compile of the sources as they
	/// are now, lowered, ready to emit. Empty then; otherwise why not, and `live` may be half changed: a cold build follows.
	std::string SwapEdits(CompilerInstance& live, const CompilerInstance& edited, ProgramBackend& backend, RetiredParses& retired);
} // namespace C4Compiler
