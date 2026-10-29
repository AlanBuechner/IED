#include "Interop/ForeignCache.h"
#include "Compiler/FileIo.h"
#include "Compiler/RuntimeObjects.h"
#include "Compiler/Toolchain.h"
#include <cstdio>

namespace Scripting
{
	namespace
	{
		// What a compiler binary is: its path, size and modification time, so a new clang makes new objects.
		std::string CompilerIdentity(const std::string& compiler)
		{
			const std::optional<FileStamp> stamp = StampOf(compiler);
			return compiler + "|" + std::to_string(stamp ? stamp->size : 0) + "|" + std::to_string(stamp ? stamp->modified : 0);
		}

		uint64_t Mix(uint64_t key, const std::string& text) { return RuntimeContentKey(text + '\0', key); }

		uint64_t FlagsKey(const ForeignObject& fo, const std::string& compiler)
		{
			uint64_t key = Mix(0, CompilerIdentity(compiler));
			key = Mix(key, Toolchain::WindowsSysroot().generic_string());   // the MSVC and SDK kit it compiles against
			key = Mix(key, fo.srcPath);
			key = Mix(key, fo.language);
			for (const auto& list : { &fo.defines, &fo.includeDirs, &fo.targetFlags, &fo.compileFlags })
			{
				key = Mix(key, "|");
				for (const std::string& item : *list) key = Mix(key, item);
			}
			return key;
		}

		// The key of the object as its inputs are now; false when the depfile is missing (a compile that never
		// finished) or names a file that is gone.
		bool CurrentKey(const ForeignObject& fo, const std::string& compiler, uint64_t& key)
		{
			const std::string depfile = fo.objPath + ".d";
			if (!FileExists(depfile)) return false;
			key = FlagsKey(fo, compiler);
			for (const std::string& input : DepfileInputs(ReadFileBytes(depfile)))
			{
				if (!FileExists(input)) return false;
				key = RuntimeContentKey(ReadFileBytes(input), Mix(key, input));
			}
			return true;
		}

		std::string Hex(uint64_t key)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(key));
			return text;
		}
	}

	std::vector<std::string> DepfileInputs(const std::string& text)
	{
		std::string joined;
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r'))
			{
				while (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) ++i;
				joined += ' ';
				continue;
			}
			joined += text[i];
		}
		const size_t colon = joined.find(": ");
		std::vector<std::string> inputs;
		std::string current;
		for (size_t i = colon == std::string::npos ? joined.size() : colon + 2; i < joined.size(); ++i)
		{
			const char c = joined[i];
			if (c == '\\' && i + 1 < joined.size() && joined[i + 1] == ' ') { current += ' '; ++i; }
			else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { if (!current.empty()) inputs.push_back(current); current.clear(); }
			else current += c;
		}
		if (!current.empty()) inputs.push_back(current);
		return inputs;
	}

	bool ForeignObjectIsCurrent(const ForeignObject& fo, const std::string& compiler)
	{
		uint64_t key = 0;
		if (!FileExists(fo.objPath) || !CurrentKey(fo, compiler, key)) return false;
		std::string recorded = ReadFileBytes(fo.objPath + ".key");
		return recorded == Hex(key);
	}

	void RecordForeignObject(const ForeignObject& fo, const std::string& compiler)
	{
		uint64_t key = 0;
		if (!CurrentKey(fo, compiler, key))
		{
			RemoveFile(fo.objPath + ".key");
			return;
		}
		WriteFileBytes(fo.objPath + ".key", Hex(key));
	}
}
