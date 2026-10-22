#include "Compiler/CpuTarget.h"
#include <llvm/ADT/StringMap.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/X86TargetParser.h>
#include <algorithm>
#include <array>
#include <utility>

namespace Scripting
{
	namespace
	{
		constexpr const char* kBaselineCpu = "x86-64";
		constexpr const char* kNative = "native";
		constexpr const char* kMarch = "-march=";
		constexpr const char* kDisable = "no-";

		// The features a flag may name, each with the macro it defines when enabled.
		constexpr std::array<std::pair<const char*, const char*>, 19> kFeatures = { {
			{ "sse3", "SIMD_SSE3" }, { "ssse3", "SIMD_SSSE3" }, { "sse4.1", "SIMD_SSE4_1" }, { "sse4.2", "SIMD_SSE4_2" },
			{ "popcnt", nullptr }, { "avx", "SIMD_AVX" }, { "avx2", "SIMD_AVX2" }, { "fma", "SIMD_FMA" }, { "f16c", nullptr },
			{ "bmi", nullptr }, { "bmi2", nullptr }, { "lzcnt", nullptr }, { "avx512f", "SIMD_AVX512F" },
			{ "avx512vl", "SIMD_AVX512VL" }, { "avx512bw", "SIMD_AVX512BW" }, { "avx512dq", "SIMD_AVX512DQ" },
			{ "avx512cd", nullptr }, { "pclmul", "SIMD_PCLMUL" }, { "vpclmulqdq", "SIMD_VPCLMULQDQ" },
		} };

		bool IsFlagFeature(const std::string& name)
		{
			return std::any_of(kFeatures.begin(), kFeatures.end(), [&](const auto& f) { return name == f.first; });
		}

		std::string FeatureList()
		{
			std::string list;
			for (const auto& f : kFeatures) list += (list.empty() ? "" : " ") + std::string(f.first);
			return list;
		}

		bool IsX86_64Cpu(const std::string& cpu) { return llvm::X86::parseArchX86(cpu, true) != llvm::X86::CK_None; }

		std::map<std::string, bool> ToMap(const llvm::StringMap<bool>& features)
		{
			std::map<std::string, bool> out;
			for (const auto& entry : features) out[entry.getKey().str()] = entry.getValue();
			return out;
		}

		llvm::StringMap<bool> ToStringMap(const std::map<std::string, bool>& features)
		{
			llvm::StringMap<bool> out;
			for (const auto& [name, enabled] : features) out[name] = enabled;
			return out;
		}

		// Every feature `cpu` has, with the ones each of them implies.
		std::map<std::string, bool> FeaturesOf(const std::string& cpu)
		{
			llvm::SmallVector<llvm::StringRef, 64> names;
			llvm::X86::getFeaturesForCPU(cpu, names);
			llvm::StringMap<bool> features;
			for (llvm::StringRef name : names)
			{
				features[name] = true;
				llvm::X86::updateImpliedFeatures(name, true, features);
			}
			return ToMap(features);
		}

		// `feature` turned on or off, with what that implies (on: what it needs; off: what needs it).
		std::map<std::string, bool> WithFeature(const std::map<std::string, bool>& features, const std::string& feature, bool enabled)
		{
			llvm::StringMap<bool> updated = ToStringMap(features);
			llvm::X86::updateImpliedFeatures(feature, enabled, updated);
			updated[feature] = enabled;
			return ToMap(updated);
		}

		std::string HostCpu()
		{
			const std::string host = llvm::sys::getHostCPUName().str();
			return IsX86_64Cpu(host) ? host : kBaselineCpu;
		}

		bool IsMarch(const std::string& flag) { return flag.compare(0, std::char_traits<char>::length(kMarch), kMarch) == 0; }

		bool SelectCpu(std::string& cpu, std::map<std::string, bool>& features, const std::string& flag, std::string& error)
		{
			const std::string named = flag.substr(std::char_traits<char>::length(kMarch));
			if (named == kNative)
			{
				cpu = HostCpu();
				features = ToMap(llvm::sys::getHostCPUFeatures());
				return true;
			}
			if (!IsX86_64Cpu(named))
			{
				error = "'" + flag + "' names no x86-64 processor: use x86-64, x86-64-v2, x86-64-v3, x86-64-v4, native, or an LLVM CPU name such as haswell or znver4";
				return false;
			}
			cpu = named;
			features = FeaturesOf(named);
			return true;
		}

		bool ApplyFeatureFlag(std::map<std::string, bool>& features, const std::string& flag, std::string& error)
		{
			if (flag.size() <= 2 || flag.compare(0, 2, "-m") != 0)
			{
				error = "'" + flag + "' is not a target flag";
				return false;
			}
			std::string feature = flag.substr(2);
			const bool enabled = feature.compare(0, 3, kDisable) != 0;
			if (!enabled) feature = feature.substr(3);
			if (!IsFlagFeature(feature))
			{
				error = "'" + flag + "' names no processor feature: the features are " + FeatureList();
				return false;
			}
			features = WithFeature(features, feature, enabled);
			return true;
		}

		std::optional<std::map<std::string, bool>> ParseLlvmFeatures(const std::string& text, std::string& error)
		{
			std::map<std::string, bool> features;
			size_t start = 0;
			while (start < text.size())
			{
				size_t end = text.find(',', start);
				if (end == std::string::npos) end = text.size();
				const std::string entry = text.substr(start, end - start);
				if (entry.size() < 2 || (entry[0] != '+' && entry[0] != '-'))
				{
					error = "'" + entry + "' is not a feature: each is +name or -name";
					return std::nullopt;
				}
				features[entry.substr(1)] = entry[0] == '+';
				start = end + 1;
			}
			return features;
		}
	}

	CpuTarget::CpuTarget() : m_Cpu(kBaselineCpu), m_Features(FeaturesOf(kBaselineCpu)) {}

	bool CpuTarget::IsTargetFlag(const std::string& arg)
	{
		return arg.size() > 2 && arg.compare(0, 2, "-m") == 0;
	}

	// As clang reads them: the last -march names the processor, then each -m / -mno- flag applies in order, wherever
	// it stands, so C4 code and the foreign sources compiled with the same flags agree on every feature.
	std::optional<CpuTarget> CpuTarget::FromFlags(const std::vector<std::string>& flags, std::string& error)
	{
		CpuTarget target;
		for (const std::string& flag : flags)
			if (IsMarch(flag) && !SelectCpu(target.m_Cpu, target.m_Features, flag, error)) return std::nullopt;
		for (const std::string& flag : flags)
			if (!IsMarch(flag) && !ApplyFeatureFlag(target.m_Features, flag, error)) return std::nullopt;
		target.m_Flags = flags;
		return target;
	}

	std::optional<CpuTarget> CpuTarget::With(const std::vector<std::string>& features, std::string& error) const
	{
		CpuTarget target = *this;
		for (const std::string& feature : features)
		{
			if (!IsFlagFeature(feature))
			{
				error = "'" + feature + "' is no processor feature: the features are " + FeatureList();
				return std::nullopt;
			}
			target.m_Features = WithFeature(target.m_Features, feature, true);
		}
		return target;
	}

	std::optional<CpuTarget> CpuTarget::FromLlvm(const std::string& cpu, const std::string& features, std::string& error)
	{
		if (!IsX86_64Cpu(cpu))
		{
			error = "'" + cpu + "' names no x86-64 processor";
			return std::nullopt;
		}
		std::optional<std::map<std::string, bool>> parsed = ParseLlvmFeatures(features, error);
		if (!parsed) return std::nullopt;
		CpuTarget target;
		target.m_Cpu = cpu;
		target.m_Features = std::move(*parsed);
		return target;
	}

	bool CpuTarget::Has(const std::string& feature) const
	{
		auto it = m_Features.find(feature);
		return it != m_Features.end() && it->second;
	}

	std::string CpuTarget::LlvmFeatures() const
	{
		std::string out;
		for (const auto& [name, enabled] : m_Features)
			out += (out.empty() ? "" : ",") + std::string(enabled ? "+" : "-") + name;
		return out;
	}

	int CpuTarget::FloatLanes() const
	{
		if (Has("avx512f")) return 16;
		if (Has("avx")) return 8;
		return 4;
	}

	std::map<std::string, std::string> CpuTarget::Macros() const
	{
		std::map<std::string, std::string> macros;
		for (const auto& [feature, macro] : kFeatures)
			if (macro && Has(feature)) macros[macro] = "1";
		macros["SIMD_FLOAT_LANES"] = std::to_string(FloatLanes());
		return macros;
	}
} // namespace Scripting
