#include "Backend/CodeGen/StableNames.h"
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constant.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/xxhash.h>
#include <cstdio>

namespace Scripting::StableNames
{
	std::string ForBytes(std::string_view prefix, std::string_view bytes)
	{
		char hash[24];
		snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(llvm::xxh3_64bits(llvm::StringRef(bytes.data(), bytes.size()))));
		return std::string(prefix) + "." + hash;
	}

	std::string ForConstant(std::string_view prefix, const llvm::Constant& value)
	{
		std::string printed;
		llvm::raw_string_ostream out(printed);
		value.print(out);
		out.flush();
		return ForBytes(prefix, printed);
	}
}
