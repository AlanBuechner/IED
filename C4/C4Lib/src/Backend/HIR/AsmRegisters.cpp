#include "Backend/HIR/AsmRegisters.h"
#include <unordered_set>

namespace Scripting::AsmRegisters
{
	namespace
	{
		// rax..rdi in every width, and their 8-bit high halves.
		const std::unordered_set<std::string>& LegacyGeneral()
		{
			static const std::unordered_set<std::string> names = {
				"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
				"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
				"ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
				"al", "bl", "cl", "dl", "sil", "dil", "bpl", "spl", "ah", "bh", "ch", "dh",
			};
			return names;
		}

		// A decimal number in [low, high] spelling the whole of `digits`.
		bool NumberIn(const std::string& digits, int low, int high)
		{
			if (digits.empty() || digits.size() > 2) return false;
			for (char c : digits)
				if (c < '0' || c > '9') return false;
			const int value = std::stoi(digits);
			return value >= low && value <= high && (digits.size() == 1 || digits[0] != '0');
		}

		// r8..r15 with an optional d (32-bit), w (16) or b (8) suffix.
		bool NumberedGeneral(const std::string& name)
		{
			if (name.size() < 2 || name[0] != 'r') return false;
			std::string digits = name.substr(1);
			if (!digits.empty() && (digits.back() == 'd' || digits.back() == 'w' || digits.back() == 'b')) digits.pop_back();
			return NumberIn(digits, 8, 15);
		}

		bool VectorRegister(const std::string& name)
		{
			if (name.size() < 4 || name[1] != 'm' || name[2] != 'm') return false;
			if (name[0] != 'x' && name[0] != 'y' && name[0] != 'z') return false;
			return NumberIn(name.substr(3), 0, 31);
		}
	}

	std::optional<Kind> KindOf(const std::string& name)
	{
		if (LegacyGeneral().count(name) || NumberedGeneral(name)) return Kind::General;
		if (VectorRegister(name)) return Kind::Vector;
		return std::nullopt;
	}

	std::string Family(const std::string& name)
	{
		static const char* const legacy[] = { "ax", "bx", "cx", "dx", "si", "di", "bp", "sp" };
		if (VectorRegister(name)) return "xmm" + name.substr(3);
		if (NumberedGeneral(name))
		{
			std::string base = name;
			if (base.back() == 'd' || base.back() == 'w' || base.back() == 'b') base.pop_back();
			return base;
		}
		for (const char* root : legacy)
		{
			const std::string r(root);
			const std::string low = r.size() == 2 && r[1] == 'x' ? std::string(1, r[0]) + "l" : r + "l";
			const std::string high = r.size() == 2 && r[1] == 'x' ? std::string(1, r[0]) + "h" : "";
			if (name == r || name == "e" + r || name == "r" + r || name == low || (!high.empty() && name == high)) return "r" + r;
		}
		return name;
	}

	const std::vector<std::string>& CallerSaved()
	{
		static const std::vector<std::string> registers = {
			"rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5",
		};
		return registers;
	}
}
