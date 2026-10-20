#pragma once
#include "Model/Types.h"
#include "Core/Core.h"
#include <functional>

namespace Scripting
{
	struct FunctionRecord; // per-declaration identity (SymbolTable.h); a signature alone is not unique
	/// One member of an overload set (doc 08): a signature plus its minimum arity (params minus
	/// trailing defaults). A set with one member behaves exactly like the old single-symbol case.
	/// `record` is the per-declaration identity: the interned FunctionType is shared by every function
	/// with the same signature, so the record - not the signature - is what names a specific function.
	struct OverloadCandidate
	{
		const FunctionType* sig = nullptr;
		const FunctionRecord* record = nullptr; // the declaration this candidate came from (may be null for discovery)
		uint32 minArity = 0; // args below this are filled from parameter defaults at resolution
	};

	struct OverloadResolution
	{
		enum class Status { Resolved, Ambiguous, NoMatch };
		Status status = Status::NoMatch;
		const FunctionType* chosen = nullptr;
		const FunctionRecord* chosenRecord = nullptr; // the specific function `chosen` names (disambiguates same-signature functions)
		Core::String message; // human-readable: which candidates tied / what was expected
	};

	/// Why the only candidate of a call cannot take its arguments.
	struct CandidateMismatch
	{
		int32 argument = -1;  // the first argument it cannot take; -1 when the argument count is wrong
		Core::String message; // empty when the candidate takes the arguments
	};

	/// The ONE ranking mechanism for every call form (doc 08): free functions, methods, operators,
	/// constructors. Fills trailing defaults, ranks by total conversion cost (exact > numeric
	/// conversion), and reports ties as Ambiguous - never a silent wrong pick. This is an internal
	/// brick: the public surface is ProgramModel::QueryFunction / QueryOperator, which call it.
	/// Partial ordering of two function template instances (+1: the first's template is more specialized,
	/// -1: the second's, 0: neither); empty when the caller has no templates to order.
	using TemplateOrdering = std::function<int(const FunctionRecord*, const FunctionRecord*)>;

	class OverloadResolver
	{
	public:
		static OverloadResolution Resolve(const Core::Vector<OverloadCandidate>& candidates,
		                                  const Core::Vector<const Type*>& args, const TemplateOrdering& moreSpecialized = {});
		/// `F takes 2 arguments, not 3` or `argument 1 of F has type 'X' but parameter 'x' expects 'Y'`.
		static CandidateMismatch Explain(const OverloadCandidate& only, const Core::Vector<const Type*>& args, const Core::String& name);

	private:
		static int CandidateCost(const OverloadCandidate& c, const Core::Vector<const Type*>& args);
		/// Conversion cost for one argument against one parameter: 0 exact, 1 implicit numeric /
		/// enum-underlying, -1 not viable. Aliases unwrap (an alias of int32 matches an int32 arg).
		static int ParamCost(const Type* param, const Type* arg);
		static int BracedCost(const Type* param, uint32 entries);
	};

} // namespace Scripting
