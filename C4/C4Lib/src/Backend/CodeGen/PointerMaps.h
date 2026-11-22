#pragma once
#include "Core/Types.h"
#include <optional>
#include <vector>

namespace llvm
{
	class Constant;
	class Module;
}

namespace C4Compiler
{
	class Type;

	/// Which words of a value of a type may hold a managed pointer (R16, docs/runtime/01): what the collector reads of an
	/// object of the type (c4_gc_type.pointers) and of a global root (c4_gc_root_range.pointers). Bit i of `bits` stands
	/// for the word at byte 8i. A word counts when any reading of it may be a pointer: a union's members, the members of an
	/// anonymous union, a `func`'s environment; a vptr (it points at the program's tables) and a function pointer do not.
	struct PointerMap
	{
		std::vector<uint64> bits;
		uint32 words = 0;   // the words a value spans

		/// The map of `type`'s values, or none when they are read whole: every word may hold a pointer (a scan reads the
		/// same), or the layout is not known or no whole number of words.
		static std::optional<PointerMap> Of(const Type* type);

		/// The map's words as a private constant of `module` (`c4.pointer_map`), for a record or a root range to point at.
		llvm::Constant* Emit(llvm::Module& module) const;
	};
}
