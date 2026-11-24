#pragma once
#include <string>
#include <vector>

namespace C4Compiler
{
	class CompilerInstance;
	class ProgramModel;
	struct FunctionRecord;
	struct SymbolInfo;

	/// What new values of globals reach (docs/compiler/07 stage 2b): the globals folded again - the edited ones, then on
	/// to every global whose initializer names one of them - and the bodies to lower again: those of the lowered functions
	/// that name one, in the order they were lowered.
	struct ValueReach
	{
		std::vector<SymbolInfo*> globals;
		std::vector<const FunctionRecord*> readers;
	};

	/// Where the globals `edited` (each folded to a value before) are named in the sources `compiler` holds now, the
	/// globals' own definitions taking their new parses already. Why their new values are not served by folding them
	/// again and lowering their readers again - a name read in no function body and in no initializer of a global
	/// folded again (a type's, a field's, a parameter's default, an annotation's), in a macro or a directive, in a
	/// [compile_time] function's body (a constant may run it), or by a global that starts at run time - else empty, with
	/// `reach` filled. A name is matched as a word, wherever it is declared: a reader of another of that name is lowered
	/// again too, which changes nothing, and the name where a variable of that name is declared reads nothing.
	std::string ReachOfValues(const CompilerInstance& compiler, const ProgramModel& model, const std::vector<SymbolInfo*>& edited,
		const std::vector<const FunctionRecord*>& lowered, ValueReach& reach);

	/// The globals' initializers folded again, as a cold build folds them (ConstEvaluator::FoldGlobalConstants); why not
	/// when one does not fold to a value of the kind it had.
	std::string FoldAgain(const ProgramModel& model, const std::vector<SymbolInfo*>& globals);
} // namespace C4Compiler
