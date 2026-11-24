#pragma once
#include "Compiler/CompilationUnit.h"
#include <string>
#include <vector>

namespace C4Compiler
{
	struct FunctionDefinitionNode;
	struct VariableDefinitionNode;

	/// A function definition an edit kept (docs/compiler/07 stage 2a): its node in the parse before and in the parse
	/// after, and whether its body reads otherwise now - its tokens or their places (`moved`: only their places).
	struct KeptDefinition
	{
		const FunctionDefinitionNode* before = nullptr;
		const FunctionDefinitionNode* after = nullptr;
		bool bodyChanged = false;
		bool moved = false;
	};

	/// A global an edit kept (docs/compiler/07 stage 2b) - a variable of a namespace, or a static member of a class that is
	/// no template: its definition in the parse before and in the parse after, and whether an initializer of it has other
	/// tokens now.
	struct KeptGlobal
	{
		const VariableDefinitionNode* before = nullptr;
		const VariableDefinitionNode* after = nullptr;
		bool valueChanged = false;
	};

	/// What an edit of one file changed, when it changed only bodies their own functions alone read and the initializers of
	/// its globals; else why not.
	struct FileEdit
	{
		std::vector<KeptDefinition> definitions; // every function definition of the file, in its order
		std::vector<KeptGlobal> globals;         // every global of the file, in its order
		SourceLocation movedFrom;                // where (before) the first thing outside them moved; line 0: nothing moved
		std::string refusal;                     // empty: such an edit
	};

	/// The edit from `before` to `after`, two parses of one file. Such an edit parses without errors; outside the bodies
	/// their own functions alone read (BodyIsItsOwn), the bodies of inline functions (the partitions that copy one take it
	/// from its function lowered again) and the globals' initializers its text and its tokens are the same - at
	/// the same lines and columns (but the rest of an edited global's own definition), or, when a body gained or lost lines,
	/// with nothing but function definitions of that kind and function declarations after the first that moved (their
	/// bodies are lowered again where they are now); a changed body writes no lambda and declares no local type or
	/// function, and a moved one declares none: lowering it again would make them anew in the model.
	FileEdit FindFileEdit(const CompilationUnit& before, const CompilationUnit& after);
} // namespace C4Compiler
