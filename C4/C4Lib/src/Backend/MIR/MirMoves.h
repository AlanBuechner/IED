#pragma once
#include "Backend/MIR/MirBuilder.h"
#include <unordered_map>
#include <vector>

namespace Scripting
{
	/// A copy of a local's or by-value parameter's object (Lifecycle::CopyOf), which may move instead (D-05).
	struct MirTransfer
	{
		TransferNode* node = nullptr;
		uint32 slot = 0;
	};

	/// A variable's own scope-exit cleanup; a variable with no declaration (a parameter, a catch variable) begins at the
	/// start of `beginsWith`.
	struct MirLocalCleanup
	{
		DeferNode* cleanup = nullptr;
		BlockNode* beginsWith = nullptr;
	};

	/// What a body's lowering recorded for its last-use moves.
	struct MirMoveSites
	{
		std::vector<MirTransfer> transfers;
		std::unordered_map<uint32, MirLocalCleanup> cleanups; // by slot

		/// What the MIR builder keeps for MirMoves: a drop guard on every copy of a cleanup whose variable may move, and
		/// where such a variable begins when it has no declaration.
		MirBuildOptions BuildOptions() const;
	};

	/// D-05's guarantee for locals (A10), on a function's MIR: a copy of a local's or by-value parameter's object that is
	/// its last use moves the object instead, when its address never escaped.
	///
	/// - A use is any read or write of the variable; the copy is its last use when no path from it reaches another use
	///   before the variable is declared again. Uses in the other arm of a `?:` do not follow it, and a use inside a
	///   cleanup (a `defer`) other than the variable's own keeps every copy.
	/// - The address escapes when it is taken other than for the length of one expression (`&x`, a reference variable,
	///   a reference capture); a reference argument or a receiver does not count.
	/// - A moved variable gets a drop flag, set where the variable begins and cleared by the move; each copy of its
	///   cleanup runs only while the flag is set (its drop guard tests it), so the moved object is destroyed once, by
	///   its new owner.
	namespace MirMoves
	{
		void Apply(HIRFunction& fn, MirBody& mir, const MirMoveSites& sites, const Type* boolType);
	}
} // namespace Scripting
