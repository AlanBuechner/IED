#pragma once
#include "Compiler/CompilationUnit.h"
#include <string>
#include <vector>

namespace C4Compiler
{
	struct FunctionDefinitionNode;

	/// A function definition an edit kept (docs/compiler/07 stage 2a): its node in the parse before and in the parse
	/// after, and whether its body reads otherwise now - its tokens or their places (`moved`: only their places).
	struct KeptDefinition
	{
		const FunctionDefinitionNode* before = nullptr;
		const FunctionDefinitionNode* after = nullptr;
		bool bodyChanged = false;
		bool moved = false;
	};

	/// What an edit of one file changed, when it changed only bodies their own functions alone read; else why not.
	struct BodyEdit
	{
		std::vector<KeptDefinition> definitions; // every function definition of the file, in its order
		SourceLocation movedFrom;                // where (before) the first thing outside the bodies moved; line 0: nothing moved
		std::string refusal;                     // empty: a body-only edit
	};

	/// The edit from `before` to `after`, two parses of one file. A body-only edit keeps the file's declaration
	/// fingerprint and parses without errors; outside the bodies their own functions alone read (BodyIsItsOwn) its text
	/// and its tokens are the same - at the same lines and columns, or, when a body gained or lost lines, with nothing but
	/// function definitions of that kind and function declarations after the first that moved (their bodies are lowered
	/// again where they are now); a changed body writes no lambda and declares no local type or function, and a moved one
	/// declares none: lowering it again would make them anew in the model.
	BodyEdit FindBodyEdit(const CompilationUnit& before, const CompilationUnit& after);
} // namespace C4Compiler
