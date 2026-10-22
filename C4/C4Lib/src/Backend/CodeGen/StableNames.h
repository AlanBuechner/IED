#pragma once
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

	/// ForConstant for many constants of a few types (a reflection image's tables): each type is encoded once.
	class ConstantNamer
	{
	public:
		std::string Name(std::string_view prefix, const llvm::Constant& value);

	private:
		void Append(std::string& out, const llvm::Constant& value);
		const std::string& TypeBytes(const llvm::Type& type);

		std::unordered_map<const llvm::Type*, std::string> m_Types;
	};
}
