#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace llvm
{
	class Constant;
	class Type;
}

namespace Scripting::StableNames
{
	/// Names for the module's private data that depend on what the data is, never on the order the program created
	/// it in: an edit then renames nothing it did not change, and the partitions it did not touch keep their code and
	/// their cached objects (roadmap 5.3). `prefix.<hash of bytes>`.
	std::string ForBytes(std::string_view prefix, std::string_view bytes);

	/// `prefix.<hash of the constant>`: of its kinds, types and values and the names of the globals it refers to.
	std::string ForConstant(std::string_view prefix, const llvm::Constant& value);

	/// ForConstant for many constants that share parts (a reflection image's tables): a constant's hash is made of its
	/// own bytes and its parts' hashes, and each type's and each aggregate's hash is made once.
	class ConstantNamer
	{
	public:
		std::string Name(std::string_view prefix, const llvm::Constant& value);

	private:
		uint64_t Hash(const llvm::Constant& value);
		uint64_t TypeHash(const llvm::Type& type);

		std::unordered_map<const llvm::Type*, uint64_t> m_Types;
		std::unordered_map<const llvm::Constant*, uint64_t> m_Aggregates;
	};
}
