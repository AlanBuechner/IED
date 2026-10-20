#include "Target/ThrownType.h"
#include <vector>

namespace C4DAP::Target
{
	namespace
	{
		// The x64 MSVC throw: ExceptionInformation holds the magic, the object, the ThrowInfo and the image base;
		// the tables hold offsets from that base. ThrowInfo.pCatchableTypeArray is at 12, a CatchableTypeArray's
		// first entry at 4, CatchableType.pType at 4, and a TypeDescriptor's name at 16.
		constexpr DWORD kThrowParameters = 4;
		constexpr uint64_t kThrowInfoTypes = 12;
		constexpr uint64_t kFirstCatchableType = 4;
		constexpr uint64_t kCatchableTypeDescriptor = 4;
		constexpr uint64_t kDescriptorName = 16;
		constexpr size_t kMaxNameBytes = 512;

		bool ReadOffset(const Process& process, uint64_t at, uint64_t base, uint64_t& out)
		{
			int32_t offset = 0;
			if (!process.Read(at, offset) || offset == 0) return false;
			out = base + static_cast<uint32_t>(offset);
			return true;
		}
	}

	std::string TypeDescriptorName(const std::string& decorated)
	{
		const std::string prefixes[] = { ".?AV", ".?AU", ".?AT" }; // class, struct, union
		for (const std::string& prefix : prefixes)
		{
			if (decorated.rfind(prefix, 0) != 0) continue;
			const size_t end = decorated.find("@@", prefix.size());
			if (end == std::string::npos || decorated.find('?', prefix.size()) != std::string::npos) return decorated;
			std::vector<std::string> innerFirst;
			size_t start = prefix.size();
			while (start < end)
			{
				const size_t at = decorated.find('@', start);
				const size_t stop = at == std::string::npos || at > end ? end : at;
				innerFirst.push_back(decorated.substr(start, stop - start));
				start = stop + 1;
			}
			std::string name;
			for (size_t i = innerFirst.size(); i-- > 0;) name += innerFirst[i] + (i > 0 ? "::" : "");
			return name;
		}
		return decorated;
	}

	std::string ThrownType(const Process& process, const EXCEPTION_RECORD& record)
	{
		if (record.ExceptionCode != kCppExceptionCode || record.NumberParameters < kThrowParameters) return "";
		const uint64_t throwInfo = record.ExceptionInformation[2];
		const uint64_t base = record.ExceptionInformation[3];
		uint64_t types = 0, firstType = 0, descriptor = 0;
		if (!throwInfo || !ReadOffset(process, throwInfo + kThrowInfoTypes, base, types)) return "";
		if (!ReadOffset(process, types + kFirstCatchableType, base, firstType)) return "";
		if (!ReadOffset(process, firstType + kCatchableTypeDescriptor, base, descriptor)) return "";
		bool complete = false;
		const std::string decorated = process.ReadText(descriptor + kDescriptorName, kMaxNameBytes, complete);
		return complete ? TypeDescriptorName(decorated) : "";
	}
} // namespace C4DAP::Target
