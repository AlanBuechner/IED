#include "CpuTargetTest.h"
#include "Compiler/BuildUtils.h"
#include "Compiler/CpuTarget.h"
#include "Macro/PredefinedMacros.h"
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/TargetParser/Host.h>
#include <optional>
#include <string>
#include <vector>

namespace Testing
{
	using namespace Scripting;

	namespace
	{
		CpuTarget Select(const std::vector<std::string>& flags)
		{
			std::string error;
			std::optional<CpuTarget> target = CpuTarget::FromFlags(flags, error);
			return target ? *target : CpuTarget();
		}

		std::string ErrorOf(const std::vector<std::string>& flags)
		{
			std::string error;
			CpuTarget::FromFlags(flags, error);
			return error;
		}

		bool Defines(const CpuTarget& target, const char* macro) { return target.Macros().count(macro) == 1; }
	}

	void CpuTargetTest::WithTurnsOnAFunctionsFeatures()
	{
		const CpuTarget baseline;
		std::string error;
		const std::optional<CpuTarget> wide = baseline.With({ "avx2", "bmi2" }, error);
		TEST_ASSERT(wide.has_value());
		TEST_ASSERT(wide->Has("avx2") && wide->Has("avx") && wide->Has("sse4.2") && wide->Has("bmi2"));
		TEST_ASSERT(wide->Cpu() == baseline.Cpu());
		TEST_ASSERT(!baseline.Has("avx2"));
		TEST_ASSERT(wide->LlvmFeatures().find("+avx2") != std::string::npos);
		TEST_ASSERT(baseline.With({}, error) == baseline);
		TEST_PASSED();
	}

	void CpuTargetTest::WithRejectsANameThatIsNoFeature()
	{
		std::string error;
		TEST_ASSERT(!CpuTarget().With({ "avx2", "avx3" }, error).has_value());
		TEST_ASSERT(error.find("'avx3' is no processor feature") != std::string::npos);
		TEST_ASSERT(!CpuTarget().With({ "" }, error).has_value());
		TEST_PASSED();
	}

	void CpuTargetTest::TheBaselineIsSse2()
	{
		const CpuTarget baseline;
		TEST_ASSERT(baseline.Cpu() == "x86-64");
		TEST_ASSERT(baseline.Has("sse2") && !baseline.Has("sse3") && !baseline.Has("avx"));
		TEST_ASSERT_EQ(baseline.FloatLanes(), 4);
		TEST_ASSERT(baseline.Macros().at("SIMD_FLOAT_LANES") == "4");
		TEST_ASSERT(!Defines(baseline, "SIMD_AVX") && !Defines(baseline, "SIMD_SSE4_1"));
		TEST_ASSERT(baseline.LlvmFeatures().find("+sse2") != std::string::npos);
		TEST_ASSERT(Select({}) == baseline);
		TEST_PASSED();
	}

	void CpuTargetTest::MarchSelectsTheCpusFeatures()
	{
		const CpuTarget v3 = Select({ "-march=x86-64-v3" });
		TEST_ASSERT(v3.Cpu() == "x86-64-v3");
		TEST_ASSERT(v3.Has("avx2") && v3.Has("fma") && v3.Has("bmi2") && v3.Has("sse4.2") && !v3.Has("avx512f"));
		TEST_ASSERT(Defines(v3, "SIMD_AVX2") && Defines(v3, "SIMD_FMA") && Defines(v3, "SIMD_SSE4_1") && !Defines(v3, "SIMD_AVX512F"));
		TEST_ASSERT_EQ(v3.FloatLanes(), 8);
		const CpuTarget haswell = Select({ "-march=haswell" });
		TEST_ASSERT(haswell.Cpu() == "haswell" && haswell.Has("avx2"));
		TEST_ASSERT(Select({ "-march=x86-64-v2" }).Has("sse4.2") && !Select({ "-march=x86-64-v2" }).Has("avx"));
		TEST_PASSED();
	}

	void CpuTargetTest::AFeatureTurnsOnWhatItNeeds()
	{
		const CpuTarget avx2 = Select({ "-mavx2" });
		TEST_ASSERT(avx2.Cpu() == "x86-64");
		TEST_ASSERT(avx2.Has("avx2") && avx2.Has("avx") && avx2.Has("sse4.2") && avx2.Has("ssse3"));
		TEST_ASSERT(!avx2.Has("fma"));                    // fma is a feature of its own
		const CpuTarget fma = Select({ "-mfma" });
		TEST_ASSERT(fma.Has("fma") && fma.Has("avx") && Defines(fma, "SIMD_FMA") && Defines(fma, "SIMD_AVX"));
		TEST_PASSED();
	}

	void CpuTargetTest::TurningAFeatureOffTurnsOffWhatNeedsIt()
	{
		const CpuTarget noAvx = Select({ "-march=x86-64-v3", "-mno-avx" });
		TEST_ASSERT(!noAvx.Has("avx") && !noAvx.Has("avx2") && !noAvx.Has("fma"));
		TEST_ASSERT(noAvx.Has("sse4.2"));
		TEST_ASSERT_EQ(noAvx.FloatLanes(), 4);
		TEST_ASSERT(noAvx.LlvmFeatures().find("-avx2") != std::string::npos);
		const CpuTarget noFma = Select({ "-march=x86-64-v3", "-mno-fma" });
		TEST_ASSERT(noFma.Has("avx2") && !noFma.Has("fma") && !Defines(noFma, "SIMD_FMA"));
		TEST_PASSED();
	}

	void CpuTargetTest::TheLastFlagWins()
	{
		TEST_ASSERT(Select({ "-mno-avx2", "-mavx2" }).Has("avx2"));
		TEST_ASSERT(!Select({ "-mavx2", "-mno-avx2" }).Has("avx2"));
		TEST_ASSERT(Select({ "-mavx2", "-mno-avx2" }).Has("avx"));
		TEST_ASSERT(Select({ "-mavx2", "-march=x86-64" }).Has("avx2"));   // as clang: -m flags apply to the last -march's processor
		TEST_ASSERT(Select({ "-march=x86-64-v3", "-march=x86-64" }).Cpu() == "x86-64");
		const std::vector<std::string> flags = { "-march=x86-64-v2", "-mavx" };
		TEST_ASSERT(Select(flags).Flags() == flags);
		TEST_PASSED();
	}

	void CpuTargetTest::Avx512HoldsSixteenFloats()
	{
		const CpuTarget v4 = Select({ "-march=x86-64-v4" });
		TEST_ASSERT(v4.Has("avx512f") && v4.Has("avx512vl") && v4.Has("avx512bw") && v4.Has("avx512dq"));
		TEST_ASSERT_EQ(v4.FloatLanes(), 16);
		TEST_ASSERT(Defines(v4, "SIMD_AVX512F") && Defines(v4, "SIMD_AVX512VL") && Defines(v4, "SIMD_AVX512BW") && Defines(v4, "SIMD_AVX512DQ"));
		TEST_ASSERT(v4.Macros().at("SIMD_FLOAT_LANES") == "16");
		const CpuTarget noF = Select({ "-march=x86-64-v4", "-mno-avx512f" });
		TEST_ASSERT(!noF.Has("avx512f") && !noF.Has("avx512vl") && noF.Has("avx2"));
		TEST_PASSED();
	}

	void CpuTargetTest::NativeIsTheBuildingMachine()
	{
		const CpuTarget native = Select({ "-march=native" });
		const std::string host = llvm::sys::getHostCPUName().str();
		TEST_ASSERT(native.Cpu() == host || native.Cpu() == "x86-64");
		TEST_ASSERT(native.Has("sse2"));
		TEST_ASSERT_EQ(native.Has("avx2"), llvm::sys::getHostCPUFeatures().lookup("avx2"));
		TEST_PASSED();
	}

	void CpuTargetTest::UnknownCpusAndFeaturesAreErrors()
	{
		TEST_ASSERT(ErrorOf({ "-march=pentium-turbo" }).find("names no x86-64 processor") != std::string::npos);
		TEST_ASSERT(ErrorOf({ "-march=i386" }).find("names no x86-64 processor") != std::string::npos);   // not 64-bit
		TEST_ASSERT(ErrorOf({ "-mturbo" }).find("names no processor feature: the features are sse3") != std::string::npos);
		TEST_ASSERT(ErrorOf({ "-mno-sse2" }).find("names no processor feature") != std::string::npos);    // the baseline stays
		TEST_ASSERT(ErrorOf({ "-O2" }).find("is not a target flag") != std::string::npos);
		TEST_ASSERT(CpuTarget::IsTargetFlag("-mavx2") && CpuTarget::IsTargetFlag("-march=native") && !CpuTarget::IsTargetFlag("-m"));
		TEST_PASSED();
	}

	void CpuTargetTest::TheLlvmFormRoundTrips()
	{
		for (const char* march : { "-march=x86-64", "-march=x86-64-v3", "-march=znver4" })
		{
			const CpuTarget target = Select({ march, "-mno-fma" });
			std::string error;
			std::optional<CpuTarget> read = CpuTarget::FromLlvm(target.Cpu(), target.LlvmFeatures(), error);
			TEST_ASSERT(read.has_value() && error.empty());
			TEST_ASSERT(*read == target);
			TEST_ASSERT(read->Macros() == target.Macros());
		}
		std::string error;
		TEST_ASSERT(CpuTarget::FromLlvm("x86-64", "", error).has_value());
		TEST_PASSED();
	}

	void CpuTargetTest::AMalformedLlvmFormIsAnError()
	{
		std::string error;
		TEST_ASSERT(!CpuTarget::FromLlvm("x86-64", "+avx2,fma", error));
		TEST_ASSERT(error.find("'fma' is not a feature: each is +name or -name") != std::string::npos);
		TEST_ASSERT(!CpuTarget::FromLlvm("abacus", "+avx2", error));
		TEST_ASSERT(error.find("'abacus' names no x86-64 processor") != std::string::npos);
		TEST_PASSED();
	}

	void CpuTargetTest::TheCommandLineSelectsTheTarget()
	{
		const char* argv[] = { "C4.exe", "-march=x86-64-v3", "-mno-fma", "main.cf" };
		const CompileOptions options = ParseArgs(4, const_cast<char**>(argv));
		TEST_ASSERT(options.error.empty());
		TEST_ASSERT(options.target.Has("avx2") && !options.target.Has("fma"));
		TEST_ASSERT(options.target.Flags() == (std::vector<std::string>{ "-march=x86-64-v3", "-mno-fma" }));
		TEST_ASSERT(options.inputs.Count() == 1);
		const char* bad[] = { "C4.exe", "-mavx3", "main.cf" };
		TEST_ASSERT(ParseArgs(3, const_cast<char**>(bad)).error.find("'-mavx3' names no processor feature") != std::string::npos);
		const char* plain[] = { "C4.exe", "main.cf" };
		TEST_ASSERT(ParseArgs(2, const_cast<char**>(plain)).target == CpuTarget());
		TEST_PASSED();
	}

	void CpuTargetTest::PredefinedMacrosCarryTheTarget()
	{
		const std::map<std::string, std::string> v3 = PredefinedMacros(false, Select({ "-march=x86-64-v3" }), {});
		TEST_ASSERT(v3.at("SIMD_AVX2") == "1" && v3.at("SIMD_FLOAT_LANES") == "8" && v3.at("ARCH_X64") == "1");
		const std::map<std::string, std::string> replaced = PredefinedMacros(false, Select({ "-march=x86-64-v3" }), { { "SIMD_FLOAT_LANES", "4" } });
		TEST_ASSERT(replaced.at("SIMD_FLOAT_LANES") == "4");   // a -D definition replaces a predefined macro
		TEST_ASSERT(PredefinedMacros(false, CpuTarget(), {}).count("SIMD_AVX") == 0);
		TEST_PASSED();
	}
} // namespace Testing
