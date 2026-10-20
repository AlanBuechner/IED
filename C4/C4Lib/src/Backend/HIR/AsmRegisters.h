#pragma once
#include <optional>
#include <string>
#include <vector>

namespace Scripting::AsmRegisters
{
	/// What an x86-64 register holds: integers and addresses, or SSE / AVX values.
	enum class Kind { General, Vector };

	/// The kind of a register name (`"rax"`, `"eax"`, `"al"`, `"r9d"`, `"xmm12"`, `"zmm31"`), or nothing for a name
	/// x86-64 does not have.
	std::optional<Kind> KindOf(const std::string& name);

	/// The 64-bit (or xmm) register a name is part of: "eax", "ax" and "al" are "rax", "r9d" is "r9", "ymm3" is "xmm3".
	std::string Family(const std::string& name);

	/// The registers a C call may change on Windows x64 (`clobber_abi("C")`).
	const std::vector<std::string>& CallerSaved();
}
