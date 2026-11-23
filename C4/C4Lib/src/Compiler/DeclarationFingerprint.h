#pragma once
#include "Parser/AstNode.h"
#include "SourceManager/SourceManager.h"
#include "Core/Core.h"

namespace C4Compiler
{
	/// A file's declaration fingerprint (docs/compiler/07, stage 2a): a hash of its tokens - as the macro pass expanded them,
	/// without their places - outside the bodies of the functions whose body only their own lowering reads: not `inline`,
	/// not `[compile_time]`, not of a template. Two parses of a file with the same fingerprint declare the same things, so
	/// an edit between them changed only such bodies. `children`: the file's own top-level declarations, before includes
	/// were merged in (CompilationUnit::GetOriginalChildren).
	uint64 DeclarationFingerprint(const Source& source, uint32 sourceId, const MacroLayout* macros,
		const Core::Vector<Ref<AstNode>>& children);
} // namespace C4Compiler
