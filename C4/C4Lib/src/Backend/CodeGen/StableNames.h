#pragma once
#include <string>
#include <string_view>

namespace llvm { class Constant; }

namespace Scripting::StableNames
{
	/// Names for the module's private data that depend on what the data is, never on the order the program created
	/// it in: an edit then renames nothing it did not change, and the partitions it did not touch keep their code and
	/// their cached objects (roadmap 5.3). `prefix.<hash of bytes>`.
	std::string ForBytes(std::string_view prefix, std::string_view bytes);

	/// `prefix.<hash of the constant's printed form>`, which names every global the constant refers to.
	std::string ForConstant(std::string_view prefix, const llvm::Constant& value);
}
