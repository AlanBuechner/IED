#include "LinkStamp.h"
#include "Compiler/FileIo.h"
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/xxhash.h>

namespace
{
	std::string StateOf(const std::string& path)
	{
		const std::optional<Scripting::FileStamp> stamp = Scripting::StampOf(path);
		if (!stamp) return path + " missing\n";
		return path + " " + std::to_string(stamp->size) + " " + std::to_string(stamp->modified) + "\n";
	}

	std::string Hash(const std::string& text)
	{
		const llvm::XXH128_hash_t hash = llvm::xxh3_128bits(llvm::arrayRefFromStringRef(text));
		return std::to_string(hash.high64) + "-" + std::to_string(hash.low64);
	}
}

LinkStamp::LinkStamp(std::string path, const std::vector<std::string>& inputs, const std::vector<std::string>& arguments,
	std::vector<std::string> outputs)
	: m_Path(std::move(path)), m_Outputs(std::move(outputs))
{
	std::string read;
	for (const std::string& input : inputs) read += StateOf(input);
	for (const std::string& argument : arguments) read += argument + "\n";
	m_Inputs = Hash(read);
}

bool LinkStamp::Current() const { return Scripting::ReadFileBytes(m_Path) == Contents(); }

void LinkStamp::Record() const { Scripting::WriteFileBytes(m_Path, Contents()); }

void LinkStamp::Forget() const { Scripting::RemoveFile(m_Path); }

std::string LinkStamp::Contents() const
{
	std::string contents = m_Inputs + "\n";
	for (const std::string& output : m_Outputs) contents += StateOf(output);
	return contents;
}
