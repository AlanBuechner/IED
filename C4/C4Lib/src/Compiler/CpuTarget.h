#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Scripting
{
	/// The processor a build compiles for (D-62): an LLVM x86-64 CPU and the features enabled on it once the
	/// `-m` / `-mno-` flags and everything they imply are applied. One per build: every function of the program,
	/// its foreign C/C++ sources and its scripts are compiled for it, so they agree on how a vector is passed.
	class CpuTarget
	{
	public:
		/// The baseline x86-64 (SSE2), what a build gets without flags.
		CpuTarget();

		/// The target `flags` select, read as clang reads them: the last `-march=<cpu>` (an LLVM x86-64 CPU or `native`)
		/// names the processor, then each `-m<feature>` / `-mno-<feature>` applies in order. Fails, saying why, on an
		/// unknown CPU, an unknown feature or another flag.
		static std::optional<CpuTarget> FromFlags(const std::vector<std::string>& flags, std::string& error);
		/// The target an LLVM CPU name and feature string (`+avx2,-avx512f`) describe: a script host's.
		static std::optional<CpuTarget> FromLlvm(const std::string& cpu, const std::string& features, std::string& error);
		/// `-march=...`, `-m...` or `-mno-...`: a flag FromFlags reads.
		static bool IsTargetFlag(const std::string& arg);

		/// This target with each of `features` (names a `-m` flag takes: avx2, fma, bmi2) on, and what they imply: the
		/// target of a function annotated `[target(...)]`. Fails, saying why, on a name that is not one.
		std::optional<CpuTarget> With(const std::vector<std::string>& features, std::string& error) const;

		const std::string& Cpu() const { return m_Cpu; }
		/// An LLVM feature name: `avx2`, `fma`, `sse4.1`.
		bool Has(const std::string& feature) const;
		/// Every feature the CPU's defaults or a flag decided, `+name` or `-name`, sorted: function attributes.
		std::string LlvmFeatures() const;
		/// The flags it was made from, as written: what a foreign source is compiled with. Empty for FromLlvm.
		const std::vector<std::string>& Flags() const { return m_Flags; }
		/// The float lanes of the widest vector register: 4, 8 or 16 (`SIMD_FLOAT_LANES`).
		int FloatLanes() const;
		/// `SIMD_AVX2` = 1 and the rest for each enabled feature, and `SIMD_FLOAT_LANES`.
		std::map<std::string, std::string> Macros() const;

		bool operator==(const CpuTarget& other) const { return m_Cpu == other.m_Cpu && m_Features == other.m_Features; }

	private:
		std::string m_Cpu;
		std::map<std::string, bool> m_Features;
		std::vector<std::string> m_Flags;
	};
} // namespace Scripting
